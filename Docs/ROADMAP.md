# Roadmap: from idea to published game

**Goal:** someone with an idea, but no game-development or coding experience, describes their game, and an AI agent
builds it in Unreal Engine with them, all the way to a packaged, publishable build.

**Status (2026-10-08):** plan phases 0–8 are done (world building: landscapes, terrain, painting, foliage, PCG, captures,
workflow skill), and Phase 9 (water) is built. This document proposes what comes next. It's a draft for discussion,
not a commitment. Each phase follows the same pattern as before: verify the engine APIs in source, build small focused
tools, test in the editor, then document.

## Principles

1. **Reuse Epic's toolsets** (Blueprint, UMG, StateTree, Sequencer, GAS, Niagara, AutomationTest, Editor/PIE) and only
   fill the gaps. Our tools should be the *game-level* layer on top: "add a health pickup", not "add node X".
2. **Everything is real, editable Unreal content.** We produce Blueprints, assets and levels a human can open and
   understand, and never opaque generated blobs.
3. **Safe by default.** Undo for every step, Save All before risky operations, caps on heavy content, no
   crash-prone engine paths (the lessons from the GPU hangs and the water-brush crash).
4. **The design document is the memory.** Each game keeps a design doc and task list in the project, so the AI can
   resume across sessions and the user can see and steer the plan.
5. **The human decides; the AI builds.** Creative choices, store accounts, money, legal and publishing actions always
   stay with the user.

## Proposed phases

| # | Phase | What the AI can do afterwards | Builds on |
|---|---|---|---|
| 10 | **Game foundation** | Start from a genre template (first or third person, top-down, side-scroller, vehicle), then set up the Game Mode, player character, input mapping, camera and player start | Epic templates, EditorToolset, BlueprintTools |
| 11 | **Game design doc + task tracking** | Interview the user, write the design doc (pillars, core loop, levels, art direction), break it into tasks, and track progress in the project | AgentSkills, project files |
| 12 | **Gameplay building blocks** | Pickups, health and damage, doors and switches, checkpoints, collectibles, scoring, timers, win and lose conditions, generated as readable Blueprints from tested recipes | BlueprintTools / Blueprint DSL, GAS toolsets |
| 13 | **Characters and NPCs** | Import or choose characters, set up animation, add AI enemies, companions and wildlife with patrol, chase and flee behaviour, navigation meshes and spawners | StateTree / BehaviorTree toolsets, AIModule |
| 14 | **User interface** | Main menu, HUD (health, score, minimap), pause and settings menus, and game-over screens | UMG toolset |
| 15 | **Story and cinematics** | Dialogue, quests and objectives, cutscenes and camera fly-throughs | Conversation and Sequencer toolsets |
| 16 | **Audio** | Ambient soundscapes per biome (water, wind, forest), footsteps by surface, music by game state, UI sounds | MetaSounds (new tools needed) |
| 17 | **Lighting, atmosphere, VFX** | Time of day, weather, fog and mood presets, plus effects such as fire, smoke, waterfall spray and magic | Epic's outdoor-lighting skill, Niagara toolsets |
| 18 | **Roads, caves, buildings** | Landscape splines for roads and paths, cave and overhang geometry, modular buildings and village layouts | New tools (landscape splines, geometry scripting) |
| 19 | **Save/load, settings, localization** | Save games, graphics and audio settings, translated text | String tables, new tools |
| 20 | **Playtest and QA** | Run PIE sessions, capture and review gameplay, automated smoke tests (can the player reach the goal? fall through the world?), and performance budgets with a GPU-safety check for each target spec | EditorAppToolset (StartPIE), AutomationTestToolset, captures |
| 21 | **Package and publish** | Build, cook and package for Windows (later consoles and mobile), produce store assets (screenshots, trailer capture, description drafts) and a release checklist | UAT BuildCookRun; the user handles store accounts and submission |

## Cross-cutting work

- **Asset sourcing:** guided use of Fab and Megascans content and licence notes (the user adds the content; the AI configures and places it). Also conversion pipelines like the Megaplant one.
- **Reliability:** autosave before risky steps, crash-loop recovery advice, and the GPU safety lessons applied to every heavy tool.
- **Performance on mid-range hardware** (for example an RTX 3070): Nanite assemblies for trees, HLODs, density budgets, and faster multi-tile terrain edits.
- **Larger worlds:** the World Partition region workflow for maps over 8 km.

## Open questions for the user

1. Which kinds of games matter most first (genre, single- vs multiplayer, target platforms)?
2. How much should the AI decide on its own versus ask at each step?
3. Should this stay one plugin, or become a family (World Builder, Game Builder, Publisher)?
