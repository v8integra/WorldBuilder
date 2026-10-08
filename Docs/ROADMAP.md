# Roadmap: from an empty project to a published game

## Vision

Anyone with a game idea can build that game in Unreal Engine without learning the engine, coding or game-development
tools. The user **describes** the game and **supplies the assets** they want; the AI does everything else: project
setup, gameplay systems, worlds, UI, audio hookup, testing and packaging.

It is a **step-by-step build**, like any serious AI-assisted product: the AI and the user work through the game one
milestone at a time, the AI shows its work (captures, playtests), and the user steers.

**Status (2026-10-08):** phases 0–9 are done. The AI can build complete worlds: landscapes, terrain features, painted
layers, foliage, PCG, water, captures and collision checks. That foundation was deliberately first: there's no point
having gameplay without a world to put it in.

---

## How the AI works with the user

### Ask, decide or adjust
| Situation | What the AI does |
|---|---|
| **Defining choices** (genre mix, setting, tone, art style, single- or multiplayer, scope, platforms, monetization) | **Asks.** It offers 2–4 concrete options with a recommendation, never an open-ended "what do you want?" |
| **Generic or simple choices** (input bindings, standard HUD layout, starting stat values, stack sizes, respawn rules, menu flow) | **Decides on its own** using genre conventions, records the choice in the decision log as an AI default, and moves on |
| **The user disagrees** | **Adjusts** the system, updates the decision log, and checks for knock-on effects (for example, longer days change hunger tuning) |
| **Something isn't possible yet** | **Says so** and names the missing tool or asset, instead of faking it |

### Project memory (kept inside each game project)
- **Game Design Document**: pillars, core loop, setting, systems, progression, content list. The AI writes it from an
  interview with the user, then keeps it up to date.
- **Decision log**: every decision, who made it (the user or an AI default), and why. Defaults can be changed at any time.
- **Task board**: milestones, then tasks, with status, so any session can resume where the last one stopped.
- **Asset wishlist**: what the game needs and what's still missing. Until the user adds real assets, the AI uses
  **placeholders** (engine shapes, grey-box meshes, solid-color materials) and swaps them in automatically afterwards.

---

## Architecture: how a full game stays reliable

The world tools write engine data directly. Gameplay is different: an AI that writes inventory, crafting or
networking logic from scratch for every game produces fragile, hard-to-test games. Instead:

**Decided 2026-10-08:** game systems live **in this plugin** as a new `Runtime` module (the plugin stops being
`EditorOnly`; the world-building modules stay editor-only), and every system is **replication-ready from day one**.

1. **A runtime Game Systems framework** (a new `Runtime` C++ module in this plugin, which ships with the game) provides
   tested, **genre-agnostic, data-driven, multiplayer-ready** building blocks: interaction, items, inventory,
   stats, crafting, building, creatures, combat, save/load and so on. Each block is a component plus data assets.
2. **Editor toolsets configure them.** The AI composes and tunes systems through tools ("create item Stone Axe",
   "add recipe", "spawn wolves in the forest biome") and data (DataAssets / DataTables) rather than code.
3. **Blueprints for the custom parts.** Anything unique to a game (a boss mechanic, a special trap) is generated as a
   readable Blueprint on top of the framework, using Epic's BlueprintTools.
4. **Reuse Epic's toolsets** wherever they exist: Blueprint, UMG, StateTree, Sequencer, GAS, Niagara, AutomationTest,
   PIE. We only fill gaps.
5. **Replication-aware from day one.** Survival games are usually multiplayer, and MMOs depend on it. Retrofitting
   networking later is the most expensive mistake in game development, so every system is built with it in mind.

The same building blocks serve many genres: a survival game, an RPG and a sandbox game all need items, inventory,
stats and save/load. **Genre packs** are presets and templates that compose blocks, not separate engines.

---

## Track A: survival game (first target)

Each phase ends the way the world phases did: build, test in the editor or PIE, document, and only then move on.

| # | Phase | What the AI can do afterwards |
|---|---|---|
| 10 | **Design interview + project memory** | Interview the user (setting, pillars, core loop, solo or co-op, art style); write the design doc, decision log, task board and asset wishlist into the project; fill generic gaps with defaults |
| 11 | **Game foundation** | Runtime module skeleton; game mode, player character (first or third person, switchable), Enhanced Input, camera, interaction system (look at and press E); set up maps and a player start; placeholder character |
| 12 | **Items and inventory** | Data-driven item definitions (icon, mesh, stack size, weight, category), inventory and hotbar components, world pickups and drops, equip slots |
| 13 | **Survival stats** | Health, hunger, thirst, stamina and temperature; eating and drinking (including from WaterTools water); damage, death and respawn; tunable decay |
| 14 | **Harvesting and resources** | Trees, rocks and bushes from **our own foliage and PCG output** become harvestable (with tool requirements, yields and regrowth); resource nodes; gathering animations |
| 15 | **Crafting** | Recipes as data, a crafting menu, crafting stations (campfire, workbench, forge), crafting times and unlock progression |
| 16 | **Building** | Snap-based structures (foundations, walls, roofs, doors), placement preview, structural support, damage and repair, storage containers |
| 17 | **Creatures and AI** | Wildlife (passive, skittish, aggressive), spawning by biome **from world-builder data** (height, slope, paint layer), StateTree behaviours, loot, taming later |
| 18 | **Combat and equipment** | Melee and ranged weapons, armor, durability, hit reactions; player-versus-environment first |
| 19 | **World systems** | Day and night, weather, seasons (optional), temperature by biome, altitude and time of day, sky and lighting presets (building on Epic's outdoor-lighting skill) |
| 20 | **Save and persistence** | Save and load the player, inventory, built structures, harvested and regrown nodes, creatures and world time; autosave |
| 21 | **User interface** | Main menu, HUD, inventory, crafting and building screens, pause and settings (graphics, audio, controls), death screen; consistent placeholder style the user can re-skin |
| 22 | **Audio and VFX** | Biome ambience, water, footsteps by surface, UI sounds, music states; fire, smoke, waterfall spray, hit effects (Niagara) |
| 23 | **Multiplayer** | Listen server and dedicated server co-op, joining, player persistence, a server settings file |
| 24 | **Playtest, QA and balance** | AI playtests in PIE (walk the map, gather, craft, survive a night), automated smoke tests, performance budgets checked against the target hardware, balance passes driven by telemetry |
| 25 | **Package and publish** | Windows packaging (cook, build, package), crash reporting, a store asset kit (screenshots, a trailer camera path, description drafts), and a release checklist. The user handles store accounts, payments and submission. |

**Milestone after phase 16:** a playable single-player survival loop (gather, craft, build, survive).
**Milestone after phase 23:** a co-op survival game ready for playtesting with friends.

---

## Track B: more genres (genre packs)

These reuse the Track A building blocks and add what each genre needs.

| Genre pack | Adds | Notes |
|---|---|---|
| **Sandbox / voxel** (Minecraft-like) | A voxel world (chunked, block-based terrain that can be edited at runtime), block placement and breaking, procedural world generation | Needs a **voxel terrain system**: Unreal landscapes can't be edited block by block at runtime. Options: our own chunked voxel module, or a supported third-party voxel plugin. |
| **RPG / MMORPG** (WoW-like) | Classes, talents and abilities (GAS), quests and objectives, NPC dialogue, vendors, dungeons, parties, loot tables, levelling | Single-player and co-op RPG first. A true **MMO** also needs persistent servers, accounts, databases and hosting costs, so it gets its own phase with clear cost and scale guidance. |
| **Action / shooter** | Weapons, aiming, recoil, enemy AI squads, objectives | |
| **Platformer / adventure** | Movement abilities, collectibles, puzzles, checkpoints, camera rails | |
| **Strategy / builder** | RTS camera, selection, resource economy, unit AI | |
| **Racing / vehicles** | Chaos vehicles, tracks (landscape splines), lap logic | |

Cross-genre games (survival RPGs, sandbox survival and so on) come from combining packs.

---

## Cross-cutting work (ongoing)

- **Asset onboarding:** the user drops assets in (Fab, Megascans, Marketplace, their own); the AI identifies, converts
  (as with the Megaplant pipeline), configures and places them, and keeps licence notes in the design doc.
- **World-builder backlog:** roads and paths (landscape splines), caves and overhangs, buildings and villages, softer
  paint transitions, lighter trees (Nanite assemblies), wind animation, faster multi-tile edits, maps over 8 km, and
  waterfall polish.
- **Reliability:** autosave before risky steps, crash-loop recovery guidance, and the GPU and engine safety lessons
  (water brushes, PCG generating on load) built into every tool.
- **Performance on mid-range hardware** (for example an RTX 3070) as a standing target.

---

## Ground rules

- Every AI action stays undoable, inspectable and editable by a human. No opaque generated blobs.
- The user owns all creative and business decisions. The AI recommends and records; it doesn't decide those silently.
- Publishing, payments, legal agreements and store submissions are always done by the user. The AI prepares
  everything they need.
