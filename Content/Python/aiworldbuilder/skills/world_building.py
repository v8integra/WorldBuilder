import unreal

from toolset_registry.agent_skill import agent_skill

_INSTRUCTIONS = """\
# Purpose

How to build worlds with the AI World Builder toolsets: landscapes that are real terrain, painted layers, foliage
and PCG, checked with your own eyes (captures) and with collision traces. Follow this workflow for any request to
create or change terrain, landscape materials/paint, forests or other world layout.

---

# Golden rules

1. **Real terrain, never props.** Shape terrain only with the landscape tools. Never fake hills, volcanoes or cliffs
   with static meshes. If a request can't be done with the tools, say which tool is missing.
2. **Units.** All tools take **meters** and **degrees** in world space. Unreal's own values are in centimeters;
   divide by 100 when reading raw editor values.
3. **Every edit is undoable.** Each tool call is one undo step (per tile on big areas). AI terrain goes into the
   **"AI Sculpt"** edit layer and AI paint into **"AI Paint"**, separate from hand work. Never edit the user's own
   layers.
4. **Read results.** Every tool returns `bSuccess` and a `message`. On failure the message says why and what to do;
   follow it instead of retrying blindly.
5. **Look at your work.** Capture tools save PNG files and return their paths: open the files and look before
   saying something is done.
6. **Use the dedicated tools.** Generic actor-spawning tools are not safe for landscape-coupled actors (water bodies);
   use WaterTools for all water.
7. **Small steps on heavy content.** The user's GPU can hang on very dense or very heavy scenes (it has happened).
   Prefer several moderate calls over one huge one, and capture between steps.

---

# Workflow

## 1. Inspect
- `WorldBuilderDiagnosticsToolset.DescribeWorld` first: landscapes, height range found and possible, edit/paint
  layers, foliage, PCG volumes, lighting.
- `LandscapeInspectTools.GetHeightStats` / `SampleHeightGrid` to understand the terrain you will change.
- `WorldCaptureTools.ExportHeightPreview` for an exact top-down height + slope map.

## 2. Plan
- Write down features with coordinates (meters), sizes and heights before calling tools.
- **Check the height limits.** Each landscape has `minPossibleHeightM` / `maxPossibleHeightM` (from its Z scale).
  Sculpt tools refuse results outside them. For a new world, pick `CreateLandscape(maxHeightM=...)` for the tallest
  planned feature plus headroom.
- In World Partition levels, tools only edit loaded regions; if a tool says components are not loaded, ask the user
  to load that region.

## 3. Create or sculpt
- New world: `LandscapeCreateTools.CreateLandscape`, then `GenerateTerrain` per region (Mountains, RollingHills,
  Canyons, Islands, Plains) with `edgeBlendM` so regions merge. Use `erosionIterations` (20-100) on regions up to 4 km.
- Features: `LandscapeSculptTools.ApplyShape` (Mountain, Volcano, Crater, Hill, Plateau, Mesa),
  `ApplyLineShape` (Valley, Ridge), `CarvePath` (rivers: order points downhill; roads: `bLowerOnly=false`),
  `Flatten` (building sites, lake beds), `Smooth`, `AddNoise`, `RaiseLower`.
- Blend modes: `Add` keeps underlying detail; `Max` only raises; `Min` only lowers; `Replace` sets the shape.
- Water needs ground shaped for it first: carve river beds with `CarvePath`, dig lake basins with `ApplyShape` Crater /
  `Flatten` below the shore, build cliffs for waterfalls, keep coasts a few meters above sea level.

## 3b. Water
- **Only use `WaterTools`** to add water. Never spawn Water Body actors with generic actor tools: their default
  "Affects Landscape" brush crashes the editor. WaterTools always turns it off and leaves shaping to the sculpt tools.
- `CreateOcean(seaLevelM)` (one per level), `CreateLake(outline or center+radius, waterLevelM)` for lakes and ponds,
  `CreateRiver(points, widthM, waterDepthM)` with the same points as `CarvePath` (upstream first; check the message for
  uphill warnings), `CreateWaterfall(top, bottom)` over an existing cliff (adds a plunge pool),
  `CreateCustomWater(center, level, size)` for pools, fountains, hot springs, moats and underground/cave water.
- Rivers that end in a lake or the ocean blend into it. `ListWaterBodies` / `RemoveWaterBody` to manage them.
- **Save All before adding water** to a big world, then capture to check that the water sits inside its banks.

## 4. Capture and review
- `WorldCaptureTools.CaptureOrbit` around each new feature (3-4 views), `CaptureTopDown` for layout.
- Open the PNGs. Check proportions, seams at region edges, spikes, unwanted clipping, and that features are where
  you planned. Fix with sculpt tools, then capture again (at most 3 review cycles per feature before reporting).

## 5. Paint
- `LandscapePaintTools.ListPaintLayers`. If layers have no Layer Info, run `CreateLayerInfos([])`. If the landscape
  has no layered material, build one with Epic's MaterialTools (LandscapeLayerBlend with named layers) first.
- `PaintByRules` with ordered rules: base layer first (no limits), then more specific ones. Typical:
  grass base; dirt on 18-35 degree slopes; rock above 35 degrees; snow above the snow line; sand below water level + a
  few meters. Base thresholds on `GetHeightStats`, not guesses. Use `heightBlendM` 10-30 and `slopeBlendDeg` 5-10
  with `edgeNoise` 0.3-0.5 for natural transitions.
- `PaintLayer` for local touch-ups (paths, clearings). Capture again.

## 6. Scatter
- Foliage: `FoliageScatterTools.ScatterFoliage` with slope/height/paint-layer rules and `excludeAreas` for clearings
  (villages, craters, paths). Skeletal trees (Fab/Quixel Megaplants) are converted to static meshes automatically
  (no wind). `RemoveFoliage` clears a circle.
- **Density guide:** light meshes (rocks, simple trees) 200-600 per hectare; heavy trees (Megaplants: millions of
  triangles) start at 50-150 per hectare on regions of a few hundred meters, capture, then extend. Never raise
  `maxInstances` for heavy trees without checking performance first.
- PCG: `PCGWorldTools.SpawnPCGVolume` applies a graph to a region (generated on demand, never on level load);
  `GeneratePCG` to regenerate, `CleanupPCG` to remove. Build or edit graphs with Epic's PCGToolset; graphs need static
  meshes, so run `MeshConversionTools.ConvertSkeletalToStaticMesh` on skeletal trees first.

## 7. Verify
- `DescribeWorld` again: counts and layers as expected.
- `WorldBuilderDiagnosticsToolset.TraceGround` at key spots (crater floor, village site, mountain path): must hit landscape,
  walkable where the player should walk. If a trace hits a tree where a path should be, clear it.
- Final `CaptureOrbit` / `CaptureTopDown`, then tell the user to **save all** (new assets: layer infos, foliage
  types, converted meshes, PCG graphs) and to try Play-in-Editor.

---

# Reporting

Finish with: what was built (with coordinates), the captures you looked at and what you saw, any refused steps and
why, and what the user should save or check.
"""


@agent_skill
class AIWorldBuilderWorkflowSkill(unreal.AgentSkill):
    """AIWorldBuilder workflow: how to build and change worlds (landscapes, terrain features, painting, foliage, PCG)
    with the AI World Builder toolsets. Apply this skill whenever creating or modifying terrain, landscape painting,
    forests or world layout."""

    instructions = _INSTRUCTIONS
