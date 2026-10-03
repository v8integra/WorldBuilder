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

## Phase 6 — Painting terrain layers

**Pre-check (done 2026-10-02):** Claude in UE built `/Game/Landscape/M_Terrain` (Grass/Rock/Dirt/Snow LandscapeLayerBlend) with Epic's MaterialTools and assigned it; it could not create Layer Infos or paint → covered by this phase.

**Inspector:**
1. `ListPaintLayers` `{}` → Grass, Rock, Dirt, Snow `bInMaterial: true`, `bHasLayerInfo: false`.
2. `CreateLayerInfos` `{"layerNames":[]}` → 4 assets in /Game/Landscape/LayerInfos, blend Advanced. Save all. Landscape mode → Paint shows the 4 layers with their Layer Infos.
3. `PaintLayer` `{"layerName":"Dirt","centerXM":0,"centerYM":0,"radiusM":60}` → dirt patch; Ctrl+Z undoes.
4. `PaintByRules` with the TOOLS.md example → grass base, rock on steep volcano/mountain sides, snow on peaks.
5. `ExportHeightPreview` + `CaptureOrbit` → slope map red areas ≈ rock in the render.

**Claude Code (plan test):** "Auto-paint the volcano and mountains realistically, then capture views to check the result."
Watch: does it list layers first, create Layer Infos if missing, choose sensible thresholds from GetHeightStats, and look at captures?

## Phase 7 — Foliage and PCG

Meshes: a tree pack from Fab/Megascans, or `/Engine/BasicShapes/Cone.Cone` (scale 3-6) as a stand-in pine.

**Inspector:**
1. `ScatterFoliage` (TOOLS.md example) → instances placed, `rejected` counts sensible; FT_Cone asset created; Ctrl+Z removes them.
2. `RemoveFoliage` `{"centerXM":0,"centerYM":0,"radiusM":100,"meshPaths":[]}` → clearing; `ListFoliage` counts drop.

**Ready-made PCG graph (Claude in UE + Epic's PCGToolset):**
"Using the PCG toolset, create a PCG graph at /Game/PCG/PCG_ForestScatter that samples the landscape surface (about 0.03 points per m²),
keeps points on slopes under 30° and below a maximum height, randomizes rotation and scale, prunes overlaps, and spawns /Engine/BasicShapes/Cone.Cone
(or my tree mesh). Expose density, max slope and max height as graph parameters. Then use AI World Builder's SpawnPCGVolume to apply it over the
volcano's lower slopes and capture a view."
→ graph builds and compiles; volume spawns and generates; captures show it. Copy the finished graph into the plugin (Content/PCG) to ship it.

**Claude Code (plan test):** "Add a dense pine forest on the lower volcano slopes, keep a clearing around the village, and no trees in the crater.
Then capture views." Walk through it in PIE and check performance (stat fps / stat unit).

### Phase 7b — Megaplants, conversion and safety (after the 2026-10-02 GPU-hang crashes)
Prereq: enable the **Procedural Vegetation Editor** plugin (Megaplant master materials live there).
1. `ConvertSkeletalToStaticMesh` on `Tree_Hornbeam_01_B` (not yet converted) → `SM_Tree_Hornbeam_01_B` created & saved, Nanite on; call again → reused.
2. `ScatterFoliage` with a skeletal path (e.g. Tree_Baltic_Pine_Sapling_01_A), densityPerHectare 80, a ~300 m region → converts/reuses, places trees.
3. `ScatterFoliage` over the whole map at densityPerHectare 2000 → refused by maxInstances, nothing placed.
4. `SpawnPCGVolume` → in Details the PCG component's Generation Trigger = Generate On Demand; save, reopen the level → no regeneration on load.
5. Claude Code: "Plant a hornbeam forest using the Megaplant trees in the forest area" → it should convert/reuse meshes, use modest density, capture to check.
