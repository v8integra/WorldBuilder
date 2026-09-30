# AI World Builder — Unreal Engine 5.8 MCP Toolset Plugin

**Build plan for Claude Code.** Read this whole file before starting. Work **one phase at a time**. At the end of each phase, stop, tell the user exactly how to test it in the editor, and wait for their confirmation before moving to the next phase.

---

## 1. Goal

Build an Unreal Engine 5.8 editor plugin named **`AIWorldBuilder`** that adds new MCP tools to Epic's built-in **Unreal MCP** server. With it, an AI agent (Claude Code or any MCP client) can build real worlds: create and sculpt landscapes, paint terrain layers, scatter foliage with PCG, and look at its own results through screenshots.

The stock UE 5.8 toolsets (SceneTools, ActorTools, MaterialInstanceTools, ObjectTools, etc.) have **no landscape tools**. That gap is why the AI previously faked a volcano with a cone mesh that had no collision. Everything this plugin builds must be **real engine data**: landscape heights, weight layers, and foliage/PCG instances, never stand-in meshes.

### Non-goals (for now)
- No custom MCP server. We extend Epic's existing server through the **Toolset Registry**.
- No runtime or shipping-build features. Editor only.
- No networking or authentication. Epic's server is loopback-only by design.

---

## 2. How Unreal MCP works in 5.8 (verified from Epic's docs)

- The **Unreal MCP** plugin (`ModelContextProtocol`) embeds an MCP server in the editor at `http://127.0.0.1:8000/mcp` by default.
- Tools come from the **Toolset Registry** plugin. The **All Toolsets** plugin enables Epic's defaults.
- A **toolset** is a class deriving from:
  - Python: `unreal.ToolsetDefinition`, decorated with `@unreal.uclass()`. Each tool is a `@staticmethod` decorated with `@toolset_registry.tool_call`. Type hints plus Google-style docstrings (`Args:` / `Returns:`) generate the JSON schema.
  - C++: `UToolsetDefinition`, marked `UCLASS(BlueprintType, Hidden)`. Each tool is a `static UFUNCTION(meta = (AICallable))`. Doc comments become the schema descriptions. Use `meta = (AIIgnore)` to hide helpers.
- Python toolsets are discovered from any plugin's `Content/Python/` folder at startup.
- After changes, run `ModelContextProtocol.RefreshTools` in the editor console. **Adding a new C++ `UFUNCTION` requires a full editor restart.** Live Coding only updates existing function bodies.
- Tool calls run **serially on the game thread**. Never design tools that expect parallel calls, and keep each call reasonably fast.
- Tool-search mode is on by default: agents see `list_toolsets`, `describe_toolset`, and `call_tool`. Good toolset and tool descriptions are therefore critical, because they are how the agent finds our tools.
- Reference examples in the engine:
  - Python: `Engine/Plugins/Experimental/ToolsetRegistry/Content/Python/toolset_registry/toolsets/core/actor.py`
  - C++: `Engine/Plugins/Experimental/Toolsets/GASToolsets/Source/GASToolsets/Private/AttributeSetToolset.h`
- Debugging: `npx @modelcontextprotocol/inspector`, connected to `http://127.0.0.1:8000/mcp` over Streamable HTTP. Log category: `LogModelContextProtocol`.

**Rule: verify before you code.** Epic marks this feature as experimental, and APIs may differ from what this plan assumes. Before writing code for any phase, open the engine source files listed above (and the landscape and PCG headers mentioned below) in the user's UE 5.8 install and confirm the actual class names, module names, and function signatures. If something in this plan doesn't match the source, trust the source and tell the user what differed.

---

## 3. Architecture

```
Claude Code (or any MCP client)
        │  MCP over HTTP (Epic's server, unchanged)
        ▼
Unreal MCP plugin  ──►  Toolset Registry
                              │ discovers
                              ▼
                 AIWorldBuilder plugin (ours)
        ┌──────────────┬───────────────┬──────────────┐
        │ Python       │ C++ toolsets  │ C++ core     │
        │ toolsets     │ (AICallable)  │ library      │
        │ (simple,     │ landscape,    │ heightmath,  │
        │  orchestration)│ capture     │ shapes, noise│
        └──────────────┴───────────────┴──────────────┘
                              │
                              ▼
      Landscape · Edit Layers · PCG · Foliage · Viewport capture
```

### Language choice
- **C++** for anything that touches landscape height and weight data, heavy per-sample math, or APIs not exposed to Python. This covers the landscape and capture toolsets.
- **Python** for thin orchestration tools and anything the Python API already handles well (PCG graph execution, actor placement), where fast iteration helps.

### Plugin layout
```
<Project>/Plugins/AIWorldBuilder/
  AIWorldBuilder.uplugin
  Source/
    AIWorldBuilderCore/          (Editor module: math, shapes, noise, landscape helpers)
    AIWorldBuilderToolsets/      (Editor module: UToolsetDefinition classes)
  Content/Python/aiworldbuilder/
    __init__.py
    toolsets/                    (Python ToolsetDefinition classes)
  Docs/
    TOOLS.md                     (auto-maintained list of every tool + example calls)
  Tests/
    test_prompts.md              (manual test prompts per phase)
```

`.uplugin` dependencies: ToolsetRegistry, ModelContextProtocol, Landscape, PCG, and PythonScriptPlugin. Confirm exact plugin and module names from the engine source in Phase 0. Both modules are `Type: Editor`.

---

## 4. Global rules for every tool

1. **Undo support.** Every mutating tool wraps its work in `FScopedTransaction` and calls `Modify()` on affected objects, so the user can Ctrl+Z any AI action. Name transactions clearly (e.g., "AI: Apply Volcano").
2. **Structured returns.** Return `USTRUCT`s or dataclasses, not free-form strings. Always include `success`, a short `message`, and useful data (bounds changed, min/max height, sample count).
3. **Validate inputs.** Clamp sizes, reject regions outside the landscape, and return a clear error instead of crashing. Never let a bad argument take down the editor.
4. **World units.** Tools accept **meters** in world space (easier for the AI to reason about) and convert internally to Unreal centimeters and landscape sample coordinates. State the unit in every parameter description.
5. **Chunking for large worlds.** The target is maps up to **50 km × 50 km** with World Partition. Any tool that writes heights or weights must cap the samples per call (start with 2049 × 2049) and return an error suggesting the agent split the region if it's exceeded. Only load or touch the landscape proxies inside the target region.
6. **Descriptions matter.** Each toolset docstring explains when to use it. Each tool docstring says what it does, its units, and one example. This is how the agent finds tools in tool-search mode.
7. **Real terrain, not props.** Tool descriptions for the landscape toolsets should say explicitly: "Use these tools to shape terrain. Do not approximate terrain features with static meshes."
8. **Keep `Docs/TOOLS.md` updated** at the end of every phase.

### Landscape height encoding (verify in source)
Landscape heights are `uint16`. `32768` is zero. World Z (cm) = `(value − 32768) × LandscapeScaleZ / 128` + landscape actor Z. Put these conversions in **one** helper in `AIWorldBuilderCore` and unit-test them.

### Writing heights (verify in source)
Prefer the editor-side edit interface (e.g., `FLandscapeEditDataInterface` with `GetHeightData` / `SetHeightData`, and the weight-data equivalents), writing into the **active edit layer** when edit layers are enabled. After writing, trigger the landscape's layer and content update so collision, normals, and grass all refresh. Check `LandscapeEdit.h`, `LandscapeProxy.h`, and `Landscape.h` in 5.8 for the correct calls. Also look at `ALandscapeProxy::Import` and `LandscapeImportHeightmapFromRenderTarget` as alternatives.

---

## 5. Phases

Each phase lists deliverables, then **Test in UE** steps for the user. Stop after each phase.

### Phase 0 — Environment check (no plugin code yet)
- Confirm the UE 5.8 install path and the project path with the user.
- Confirm **Unreal MCP**, **All Toolsets**, and **Toolset Registry** are enabled, and that the server is running (Output Log shows the bind address).
- Read the two reference toolset files listed in section 2 and summarize the exact patterns.
- Locate and read the 5.8 landscape headers (`Landscape.h`, `LandscapeProxy.h`, `LandscapeEdit.h`, edit-layer APIs) and the PCG component headers. Write findings to `Docs/API_NOTES.md`: real function names, signatures, and module names.
- If the `unreal-mcp` Claude Code plugin with the `create-toolset` skill is available, note it and use it for scaffolding.

**Test in UE:** The user runs `npx @modelcontextprotocol/inspector`, connects to the server, and confirms the stock toolsets are listed. Claude Code shares `API_NOTES.md` with the user.

### Phase 1 — Plugin skeleton and hello-world tools
- Create the plugin with both modules, `.uplugin`, and build files.
- Python toolset `WorldBuilderInfoTools` with `ping() -> str` returning the plugin version.
- C++ toolset `UWorldBuilderDiagnosticsToolset` with one `AICallable` function, `GetPluginStatus`, returning a struct: version, the number of landscapes in the current level, and whether World Partition is enabled.
- Compile the project.

**Test in UE:** Restart the editor. Run `ModelContextProtocol.RefreshTools`. In the Inspector, confirm both toolsets appear, then call both tools. In Claude Code, ask: *"What world builder tools do you have?"*

### Phase 2 — Landscape inspection (read-only)
C++ toolset **`LandscapeInspectTools`**:
- `ListLandscapes()` — name, world bounds (m), resolution, component count, scale, edit layers, whether World Partition is used.
- `GetLandscapeInfo(landscapeName)` — detailed info, including the material and paint layers available.
- `SampleHeight(x_m, y_m)` — ground height at a world point.
- `SampleHeightGrid(centerX_m, centerY_m, sizeM, gridCount)` — a small grid of heights (max 64 × 64) so the AI can "feel" the terrain.
- `GetHeightStats(region)` — min, max, and average height plus slope stats.

**Test in UE:** Open a level with a landscape. Ask Claude: *"Describe my landscape. What's the height at the center?"* Compare the answer against the editor.

### Phase 3 — Sculpting (the volcano phase)
C++ toolset **`LandscapeSculptTools`**. All tools write real heights, support undo, and take a `blendMode` parameter: `Add`, `Max`, `Min`, `Replace`, or `Blend(alpha)`.
- `ApplyShape(shapeType, centerX_m, centerY_m, radius_m, height_m, params, blendMode)`, with shape types:
  - `Mountain` — radial falloff with noise ridges.
  - `Volcano` — cone profile, crater depth and radius, rim sharpness, ridge noise, optional lava channel gaps.
  - `Crater`, `Hill`, `Plateau` (flat top with a cliff edge), `Mesa`, `Valley` (along a line), `Ridge` (along a line).
- `RaiseLower(center, radius_m, delta_m, falloff)`
- `Flatten(center, radius_m, targetHeight_m, falloff)` — for building sites and towns.
- `Smooth(center, radius_m, strength, iterations)`
- `AddNoise(region, amplitude_m, frequency, octaves, seed)` — fBm detail.
- `CarvePath(points_m[], width_m, depth_m, falloff)` — for rivers and roads following a polyline.
- Put the shape math in `AIWorldBuilderCore` as pure functions, with unit tests.
- Falloff options: `Linear`, `Smooth` (smoothstep), `Sphere`, `Tip`.

**Test in UE:**
1. Ask: *"Build a volcano 400 m tall with a 1.5 km base radius and a crater in the middle of my map."*
2. Confirm in the editor that it is **landscape**, not a mesh (select it; it should be the Landscape actor).
3. Press Ctrl+Z and confirm it undoes, then redo it.
4. Play-in-editor: walk your character up the slope and into the crater. **Collision must work.**
5. Ask for a flattened area on the volcano's flank for a village.

### Phase 4 — Eyes: screenshots and height previews
C++ toolset **`WorldCaptureTools`**:
- `CaptureView(cameraX_m, cameraY_m, cameraZ_m, pitch, yaw, fov, width, height)` — renders a view to PNG via a scene capture or high-res screenshot, saves it to `<Project>/Saved/AIWorldBuilder/Captures/`, and returns the file path. Claude Code can then open the image to look at it.
- `CaptureOrbit(target, distance_m, count)` — N views circling a point.
- `CaptureTopDown(region)` — an orthographic overhead view.
- `ExportHeightPreview(region, width)` — a grayscale height image plus a colored slope map as PNG.
- Also try returning the image directly in the MCP tool result, if 5.8's toolset return types support images. Verify first; the file path is the fallback.

**Test in UE:** Ask Claude to build a mountain range, capture three angles, **look at the images**, and fix anything that looks wrong. Confirm the images match what you see in the viewport.

### Phase 5 — Creating landscapes and heightmaps
C++ toolset **`LandscapeCreateTools`**:
- `CreateLandscape(sizeX_km, sizeY_km, quadsPerComponent, sectionsPerComponent, useWorldPartition, material)` — picks a valid Unreal landscape resolution closest to the requested size and reports the real size it chose.
- `ImportHeightmap(filePath, region, zScale)` — 16-bit PNG or RAW, including tiled input.
- `ExportHeightmap(region, filePath)`
- `GenerateTerrain(region, preset, seed, params)` — procedural base terrain. Presets: `RollingHills`, `Mountains`, `Islands`, `Canyons`, `Plains`. Use fBm plus ridged noise, and optionally a simple thermal/hydraulic erosion pass implemented in Core.
- Large-map strategy: generate in tiles and write chunk by chunk (section 4, rule 5).

**Test in UE:** Start with an empty level. Ask: *"Create a 4 km × 4 km landscape with mountains in the north, rolling hills in the south, and a lake basin in the middle."* Then, separately, test an 8 km map to check the chunking. Only attempt a 50 km map after smaller sizes are stable, and watch memory use.

### Phase 6 — Painting terrain layers
C++ toolset **`LandscapePaintTools`**:
- `ListPaintLayers(landscape)`
- `PaintLayer(layerName, center, radius_m, strength, falloff)`
- `PaintByRules(region, rules[])` — auto-painting, e.g., rock where slope > 35°, snow above 1,800 m, grass below, sand within 5 m of the water height. This is the key "AI painting" tool.
- Weight layers must be normalized correctly (verify how 5.8 handles weight blending).
- If the landscape has no layer infos, the tool explains what's missing and offers `CreateLayerInfos(layerNames[])`.

**Test in UE:** Use a landscape material with grass, rock, dirt, and snow layers. Ask Claude to auto-paint the volcano and mountains realistically, then capture views to check the result.

### Phase 7 — Foliage and PCG scattering
Python toolset **`PCGWorldTools`**, with C++ where Python falls short:
- `ListPCGGraphs()`
- `SpawnPCGVolume(graphAsset, region, seed, parameterOverrides)`
- `SetPCGParameters(volume, params)`
- `GeneratePCG(volume)` / `CleanupPCG(volume)`
- `ScatterFoliage(meshAssets[], region, density, rules)` — a fallback path using foliage instances directly, with rules for slope, height, and layer masks (e.g., "trees only on the grass layer, not above 1,500 m").
- Provide one ready-made example PCG graph in plugin content: a forest scatter driven by a density texture, slope, and height. This is the "store the recipe, not the result" idea.

**Test in UE:** Ask for a dense pine forest on the lower volcano slopes, clearing around the village, and no trees in the crater. Walk through it in play-in-editor and check performance.

### Phase 8 — Workflow tools and polish
- `DescribeWorld()` — a summary of landscapes, PCG volumes, major actors, and lighting, so the agent can orient itself at the start of a session.
- `RunPlayInEditor()` / `StopPlayInEditor()` if feasible, plus a collision probe tool: `TraceGround(x_m, y_m)` confirms collision exists at a point.
- A Claude Code skill or `CLAUDE.md` snippet with a recommended workflow: inspect → plan → sculpt → capture → review → paint → scatter → capture → verify collision.
- Final `Docs/TOOLS.md`, plus a README with install steps.

**Test in UE:** End-to-end prompt: *"Build a 4 km volcanic island: a central volcano with a crater, beaches, a jungle on the lower slopes, a flattened village site on the east side, and a river from the highlands to the sea. Show me screenshots when you're done."*

---

## 6. Later ideas (don't build yet)
- Water plugin integration (rivers and lakes as water bodies).
- Road and spline tools.
- Neural and procedural compression experiments (store rules and density maps instead of baked content).
- Runtime generation for smaller shipped game sizes.
- Terrain presets learned from real-world elevation data.

---

## 7. Instructions to Claude Code

- Start with **Phase 0**. Do not skip ahead.
- At the end of each phase: build, fix all compile errors, update `Docs/TOOLS.md`, write the phase's test prompts to `Tests/test_prompts.md`, then **stop** and give the user clear numbered steps for testing in the editor (including whether an editor restart is needed).
- When an engine API doesn't match this plan, check the 5.8 source, adapt, and record the difference in `Docs/API_NOTES.md`.
- Prefer small, focused tools over one giant do-everything tool.
- Never approximate terrain with meshes. If a request can't be done with the current tools, say which tool is missing instead of faking it.
