# Manual Test Prompts

## Phase 1 — Plugin skeleton

**Inspector** (`http://127.0.0.1:8000/mcp`, Streamable HTTP):
1. `list_toolsets` → includes `AIWorldBuilderToolsets.WorldBuilderDiagnosticsToolset` and `aiworldbuilder.toolsets.info.WorldBuilderInfoTools`.
2. `describe_toolset` on each → descriptions and tool schemas look right.
3. `call_tool` with toolset_name `aiworldbuilder.toolsets.info.WorldBuilderInfoTools`, tool_name `ping`, arguments `{}` → `"AIWorldBuilder 0.1.0"`.
4. `call_tool` with toolset_name `AIWorldBuilderToolsets.WorldBuilderDiagnosticsToolset`, tool_name `GetPluginStatus`, arguments `{}` → version 0.1.0, correct level name, LandscapeCount 0 in an empty level.

(Inspector form: type names without quotes. Only `arguments` is JSON.)

**Claude Code** (with the Unreal MCP server connected):
- "What world builder tools do you have?"
- "Check the AI World Builder plugin status."

**Expected Output Log lines:** `AIWorldBuilder: Python toolsets registered.`

## Phase 2 — Landscape inspection (read-only)

Setup: open the Open World level `TestLandscape` (its landscape actor is labelled `Landscape`).

**Unit tests:** Tools → Session Frontend → Automation → filter `AIWorldBuilder` → run `AIWorldBuilder.Core.LandscapeMath` → green.

**Inspector:**
1. `describe_toolset` → `AIWorldBuilderToolsets.LandscapeInspectTools` shows 5 tools; note the argument key casing.
2. `ListLandscapes` → TestLandscape with plausible size (Open World template ≈ 2 km), resolution, component counts, edit layers.
3. `GetLandscapeInfo` (empty arguments) → same landscape + material + paint layers.
4. `SampleHeight` at a point you can check: place the cursor on the terrain, read the location in the viewport/Details, compare.
5. `SampleHeightGrid` 500 m, 8×8 around the origin → numbers vary smoothly; min/max sensible.
6. `GetHeightStats` with `{}` → whole-landscape stats; with a 200 m region over a hand-sculpted bump → max height ≈ the bump.
7. Error cases: `SampleHeight` at (100000, 100000) → clear "outside every landscape" message; `SampleHeightGrid` with GridCount 100 → "between 2 and 64".

**Claude Code:**
- "Describe my landscape. What's the height at the center?" → compare against the editor.
- "Where is the highest point and how steep is the terrain around it?"

## Phase 3 — Sculpting

Setup: `TestLandscape` level. Optional: set the landscape's Scale Z to 200 (Details → Transform) for the 400 m volcano test.

**Unit tests:** Session Frontend → Automation → `AIWorldBuilder` → all green (LandscapeMath + TerrainMath.FalloffAndBlend/Shapes/Geometry).

**Inspector (quick smoke test, small volcano that fits at Scale Z 100):**
`ApplyShape` `{"shapeType":"Volcano","centerXM":0,"centerYM":0,"radiusM":600,"heightM":120,"lavaChannels":3}`
→ `bSuccess`, `bCreatedEditLayer: true` (first time), `bCollisionVerified: true`. Landscape mode shows an "AI Sculpt" edit layer.

**Height-limit refusal (Scale Z 100):** `ApplyShape` Volcano radius 1500, height 400 → refused with a message naming the Scale Z needed; nothing changes.

**Claude Code (plan test):**
1. "Build a volcano 400 m tall with a 1.5 km base radius and a crater in the middle of my map."
2. Select it in the viewport: it is the Landscape actor (not a mesh).
3. Ctrl+Z undoes it; Ctrl+Y redoes it.
4. Play-in-editor: walk up the slope into the crater — collision must work.
5. "Flatten an area on the volcano's east flank for a village."

**Extra:** "Carve a river from the north-west hills down to the basin", "Add a ridge line along the southern edge", "Smooth the crater rim a little".

## Phase 4 — Eyes (captures and height previews)

**Unit tests:** Session Frontend → Automation → `AIWorldBuilder` → all green (TerrainMath.Geometry now also checks preview colours).

**Inspector:**
1. `ExportHeightPreview` `{}` → two PNG paths; open them: grey height map and coloured slope map of the whole landscape; volcano visible; image top = +X.
2. `CaptureOrbit` `{"targetXM":0,"targetYM":0,"distanceM":1500,"count":3,"targetHeightAboveGroundM":60}` → 3 PNGs of the volcano from 3 sides; viewport camera returns to where it was.
3. `CaptureTopDown` `{"centerXM":0,"centerYM":0,"sizeM":2000}` → overhead PNG matching the height map's orientation.
4. Images match what the viewport shows (same lighting).

**Claude Code (plan test):** "Build a mountain range across the north of the map, capture three angles, look at the images, and fix anything that looks wrong."
Check that Claude actually opens the PNGs and describes/fixes what it sees.

## Phase 5 — Creating landscapes and heightmaps

**Unit tests:** Session Frontend → Automation → `AIWorldBuilder` → all green (new: TerrainMath.Generation).

**Setup:** File → New Level → **Empty Open World** (or Empty Level) and save it, e.g. `TestCreate`.

**Inspector smoke tests:**
1. `CreateLandscape` `{"sizeXKm":2,"sizeYKm":2,"maxHeightM":400}` → ~2016 m square, height range ±400 m, streaming proxies in a WP level. Ctrl+Z removes it, Ctrl+Y restores.
2. `GenerateTerrain` `{"preset":"RollingHills","amplitudeM":80}` → whole landscape becomes hills.
3. `ExportHeightmap` `{}` → a 16-bit PNG in Saved/AIWorldBuilder/Heightmaps.
4. `RaiseLower` somewhere (change terrain), then `ImportHeightmap` `{"filePath":"<exported path>"}` → terrain restored exactly (round trip).

**Claude Code (plan test):**
1. In an empty level: "Create a 4 km × 4 km landscape with mountains in the north, rolling hills in the south, and a lake basin in the middle."
   Expect: CreateLandscape (maxHeightM sized for the mountains) → GenerateTerrain regions → ApplyShape/Flatten for the basin → captures to check.
2. Separately: "Create an 8 km × 8 km landscape with rolling hills." → tiles (≥ 4) and still responsive; watch memory in Task Manager.
3. Only after both work: try larger (e.g. 16 km at 2 m spacing). 50 km needs the region workflow (not supported yet).
