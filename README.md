# AI World Builder

An Unreal Engine 5.8 editor plugin that gives AI agents (Claude Code or any MCP client) real world-building tools through
Epic's built-in **Unreal MCP** server: create and sculpt landscapes, generate terrain, paint layers, scatter foliage, run
PCG, and look at the results through captures — all as real engine data (landscape heights, weight layers, foliage
instances), never stand-in meshes. Every change is undoable.

- Tool reference: [`Docs/TOOLS.md`](Docs/TOOLS.md)
- Engine API findings and gotchas: [`Docs/API_NOTES.md`](Docs/API_NOTES.md)
- Manual test prompts per phase: [`Tests/test_prompts.md`](Tests/test_prompts.md)
- Build plan: [`AIWorldBuilder_Plan.md`](AIWorldBuilder_Plan.md)

## Requirements

- Unreal Engine **5.8** (tested on 5.8.3), Windows.
- A **C++ project** (a Blueprint-only project needs one C++ class added first so it can compile plugins:
  Tools → New C++ Class → None → Create).
- Visual Studio 2022/2026 (or Build Tools) with the C++ game development workload and the **.NET Framework 4.8.1
  Developer Pack** (the *developer pack*, not just the runtime).
- Engine plugins enabled in the project (Edit → Plugins):
  - **Unreal MCP** (`ModelContextProtocol`)
  - **Toolset Registry** and **All Toolsets**
  - **Python Editor Script Plugin**, **PCG** and **Water** (enabled automatically as dependencies of this plugin)
  - **Procedural Vegetation Editor** — only if you use Fab/Quixel **Megaplant** trees (their materials live there)

## Install

1. Copy (or clone) this repository to `<YourProject>/Plugins/AIWorldBuilder/`.
   To develop the plugin in a separate folder, link it instead (PowerShell):
   `New-Item -ItemType Junction -Path "<YourProject>\Plugins\AIWorldBuilder" -Target "<path to this repo>"`
2. Close the editor, then build the editor target, e.g.:
   `"<UE_5.8>\Engine\Build\BatchFiles\Build.bat" <Project>Editor Win64 Development -Project="<path>\<Project>.uproject" -WaitMutex`
   (or open the `.uproject` and accept the rebuild prompt).
3. Open the project. The Output Log should show `AIWorldBuilder: Python toolsets and agent skills registered.` and
   `LogModelContextProtocol` listing the toolsets.

## Connect an AI client

The Unreal MCP server runs inside the editor at `http://127.0.0.1:8000/mcp` (Streamable HTTP, loopback only).

- **MCP Inspector** (for testing): `npx @modelcontextprotocol/inspector`, transport *Streamable HTTP*, URL above,
  Connect, then use `list_toolsets` / `describe_toolset` / `call_tool` (fields `toolset_name`, `tool_name`, `arguments`).
- **Claude Code**: add the server (e.g. `claude mcp add --transport http unreal http://127.0.0.1:8000/mcp`), or use
  Epic's *Unreal Engine Skills* Claude Code plugin. Optionally paste [`Docs/CLAUDE_SNIPPET.md`](Docs/CLAUDE_SNIPPET.md)
  into your project's `CLAUDE.md`.

Agents should start with `AIWorldBuilderToolsets.WorldBuilderDiagnosticsToolset.DescribeWorld` and read the
**AIWorldBuilder workflow** agent skill (served by Epic's `AgentSkillToolset`: `ListSkills` / `GetSkills`).

## Toolsets

| Toolset | Purpose |
|---|---|
| `WorldBuilderDiagnosticsToolset` | Plugin status, `DescribeWorld`, `TraceGround` (collision check) |
| `LandscapeInspectTools` | List landscapes, sample heights, height and slope stats (read-only) |
| `LandscapeCreateTools` | Create landscapes, generate terrain presets, import/export 16-bit heightmaps |
| `LandscapeSculptTools` | Mountains, volcanoes, craters, plateaus, mesas, valleys, ridges, flatten, smooth, noise, carve paths |
| `LandscapePaintTools` | Paint layers, Layer Info creation, brush paint, rule-based auto-paint |
| `FoliageScatterTools` | Scatter and remove foliage by slope, height, paint layer and exclusion zones |
| `PCGWorldTools` | Spawn, generate and clean up PCG volumes over regions |
| `MeshConversionTools` | Convert skeletal meshes (e.g. Megaplants) to static meshes for foliage/PCG |
| `WaterTools` | Oceans, lakes, rivers, waterfalls, custom/underground water (Epic Water plugin, crash-safe) |
| `GameFoundationTools` | Game mode, player character (3rd/1st person, Enhanced Input, sprint, crouch, interaction), player starts, startup maps |
| `ItemTools` | Item definitions, pickups, starting items (inventory, hotbar and equipment are on the player character) |
| `SurvivalTools` | Health, stamina, hunger, thirst and custom stats, temperature, drinking, fall damage, death bags and respawn |
| `WorldCaptureTools` | Viewport captures (view, look-at, orbit, top-down) and exact height/slope maps (PNG files) |
| `aiworldbuilder...GameDesignTools` (Python) | Game project memory: design doc, decision log, task board, asset wishlist, genre templates |
| `aiworldbuilder...WorldBuilderInfoTools` (Python) | `ping` |

## Safety notes

- AI terrain edits go to the **AI Sculpt** edit layer and paint to **AI Paint**; hide or delete those layers to remove
  all AI changes. Ctrl+Z undoes each call.
- Height edits that would exceed the landscape's height range are refused (nothing changes) unless `bAllowClipping`.
- `ScatterFoliage` refuses more than `maxInstances` (default 50,000). Heavy trees (Megaplants) can hang mid-range GPUs:
  scatter them in small regions at low density.
- PCG volumes spawned by the plugin generate **on demand**, never on level load.
- New assets (layer infos, foliage types, converted meshes) must be saved (Save All).

## Not yet supported

- Landscapes larger than 8191 samples per side (~8 km at 1 m; use larger spacing) — the 50 km World Partition
  "region" workflow.
- Landscape splines/roads, caves/overhangs (need geometry), wind-animated (skinned) foliage, runtime generation.
