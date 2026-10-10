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

## Phase 8 — Workflow tools and polish

**Inspector:**
1. `GetPluginStatus` → version **1.0.0**.
2. `DescribeWorld` `{}` on TestWorld → message lists the landscape (size, terrain range, limits, material, edit layers incl. AI Sculpt / AI Paint, paint layers), foliage counts, PCG volumes, lighting, player starts.
3. `TraceGround` on open ground → `bHitLandscape: true`, `bWalkable: true`; inside the forest at a tree → hits the tree (or landscape if foliage has no collision); on a cliff → `bWalkable: false`.
4. `ToolsetRegistry.AgentSkillToolset` → `ListSkills` includes **AIWorldBuilderWorkflowSkill**; `GetSkills` returns its instructions.

**Claude Code (plan end-to-end test, new empty level):**
"Build a 4 km volcanic island: a central volcano with a crater, beaches, a jungle on the lower slopes, a flattened village site on the east side, and a river from the highlands to the sea. Show me screenshots when you're done."
Expect: DescribeWorld → skill → CreateLandscape (maxHeightM sized) → GenerateTerrain Islands → ApplyShape Volcano → Flatten village → CarvePath river → captures/review → paint (sand/grass/rock) → ScatterFoliage with village + crater exclusions → TraceGround checks → final captures. Then PIE walk.

## Phase 9 — Water (after the 2026-10-08 water-brush crashes)

Prereq: Save All. Remove any water bodies created earlier with generic tools (`ListWaterBodies` warns about ones that affect the landscape).
1. `CarvePath` a river bed, then `CreateRiver` with the same points → river inside its banks; no "Water" edit layer appears in Landscape mode; no crash after 30 s.
2. `ApplyShape` Crater (or Flatten) a basin, `CreateLake` `{"outlineM":[],"centerXM":..,"centerYM":..,"radiusM":80,"waterLevelM":..}`.
3. `CreateOcean` `{"seaLevelM":0}` on an island map → sea around the island, dry land above 0 m.
4. `CreateWaterfall` over a cliff (make one with ApplyShape Mesa) → falling water + pool.
5. `CreateCustomWater` small pool → visible water plane at the given height.
6. Ctrl+Z each; `RemoveWaterBody`. Save, reopen → water persists, no crash.
**Claude Code:** "Add water to the volcano island: ocean at sea level, fill the river channel, and a small lake in the highland basin."

## Phase 10 — Game design interview and project memory

Prereq: restart the editor (Python only, no C++ build). Python tool argument names stay snake_case in JSON (e.g. `task_id`), unlike the camelCase C++ tools.
1. `list_toolsets` → includes `aiworldbuilder.toolsets.game_design.GameDesignTools`; `ListSkills` → includes the game design interview skill.
2. `get_project_memory` `{}` → "No game project yet…".
3. `get_genre_template` `{"genre":"ark"}` → survival template JSON; `{"genre":"racing"}` → generic.
4. `start_game_project` `{"name":"Test","genre":"survival","pitch":"Test pitch"}` → `<Project>/AIGameBuilder/project.json` + `GameDesign.md` created. Calling it again → refused.
5. `seed_plan_from_template` `{}` → 37 tasks, 10 assets. Again → 0 added.
6. `record_decision` twice on the same topic (once `ai_default`, once `user`) → second replaces the first; `list_decisions` shows only the latest.
7. `update_task` `{"task_id":"T-001","status":"done","notes":"ok"}`, `update_asset` `{"asset_id":"A-002","status":"placeholder","path":"/Engine/BasicShapes/Cone"}` → reflected in `GameDesign.md`.
8. Manual tests above create a throwaway project: run them in a scratch project, or call `start_game_project` with `overwrite=true` when starting the real game (never delete the memory of a real game).
**Claude Code:** "I want to make a survival game. Interview me." → asks the must-ask questions in small batches with recommendations, records answers and AI defaults, writes the design doc, seeds the plan, lists the AI defaults and most useful assets. New session: "Let's continue my game." → resumes from `get_project_memory` without re-interviewing.

## Phase 11 — Game foundation (runtime module)

Prereq: C++ build (new runtime module; the plugin is no longer editor-only). Open a level with a landscape (e.g. TestWorld) and Save All first.
1. `GetGameFoundationStatus` `{}` → reports the current (First Person template) game mode and player starts.
2. `SetupGameFoundation` `{"perspective":"ThirdPerson"}` → creates `/Game/AIGameBuilder/Core/BP_AGB_*` and `Input/IA_AGB_*`, `IMC_AGB_Default`; message names the previous game mode; character uses SKM_Manny_Simple + ABP_Unarmed.
3. `PlacePlayerStart` `{"xM":..,"yM":..,"yawDeg":0}` on flat ground → one player start standing on the terrain.
4. `SpawnInteractableLight` a few meters in front of it.
5. Play (PIE): mannequin animates; WASD/mouse; Space jumps; hold Shift sprints (faster); C crouches; V switches to first person and back (own body hidden in first person); looking at the lamp within 2.5 m shows "[E] Turn off light"; E toggles it.
6. Co-op check: Play options → Number of Players 2, Net Mode "Play As Listen Server". In the client window: sprint is smooth (no rubber-banding), the lamp toggled by one player changes for both, camera switch on one player makes their character turn with the camera in the other window.
7. `SetupGameFoundation` `{"perspective":"FirstPerson","characterMeshPath":"none"}` → Play: placeholder cylinder body, starts in first person. Then `{"perspective":"ThirdPerson"}` → "auto" brings the mannequin back.
8. `SetStartupMap` `{"mapPath":"current"}` → Project Settings > Maps & Modes shows the level for both maps.
**Claude Code:** "Continue my game." (after Phase 10's interview) → sets up the foundation from the Perspective decision, places a player start near the planned start, marks M1 tasks done, asks you to playtest.

## Phase 12 — Items and inventory

Prereq: C++ build, then run `SetupGameFoundation` `{"perspective":"ThirdPerson"}` again → message says `IMC_AGB_Default` was kept and bindings were added for the new actions.
1. `CreateItem` `{"itemId":"wood","displayName":"Wood","category":"Resource","maxStack":100,"weight":0.5,"description":"Chopped from trees.","meshPath":"auto","iconPath":"auto","equipSlot":"None","tags":["Fuel"],"stats":[]}` → `/Game/AIGameBuilder/Items/DA_Item_wood`.
2. Also: `stone` (Resource, 100, 1.0), `berries` (Food, 20, 0.1, stats `[{"name":"Hunger","value":10}]`), `stone_axe` (Tool, 1, 2.5, equipSlot `MainHand`, meshPath e.g. `/Engine/BasicShapes/Cylinder`), `leather_cap` (Armor, 1, 0.5, equipSlot `Head`).
3. `ListItems` `{}` → 5 items. Calling `CreateItem` again with the same id updates it (no duplicate).
4. `SpawnItemPickup` wood x25, stone x10, berries x5 and the axe near the player start; `SetStartingItems` `{"items":[{"itemId":"berries","count":3}]}`.
5. Play: berries are on the hotbar at start. Walk to a pickup: "[E] Pick up Wood (25)"; E → it tops up matching stacks, else fills a free hotbar slot, else the inventory. The axe lands on the hotbar; select its slot (1-0 or wheel) → held in the right hand (cylinder placeholder).
6. Tab: inventory screen with cursor, look locked. Click a stack then another slot → moves; onto the same item → merges; onto a different item → swaps; Shift+click → half. Leather cap only fits the Head slot. Q over a stack → dropped in front of the player as a pickup. Tab closes.
7. Q in game drops one of the selected hotbar item.
8. Co-op (2 players, listen server): each player has their own inventory; a pickup taken by one disappears for both; dropped items appear for both; the other player sees the held axe.
**Claude Code:** "Create the starting resource items from the design (wood, stone, fiber, flint, berries) and a stone axe; scatter some pickups near the start." → uses ItemTools, then marks the M2 tasks.

## Phase 13 — Survival stats

Prereq: C++ build; run `SetupGameFoundation` `{"perspective":"ThirdPerson"}` again (adds the Use action: left mouse / right trigger).
1. `GetSurvivalConfig` `{}` → built-in Normal rules, no config assigned. `SetupSurvival` `{"difficulty":"Normal"}` → `/Game/AIGameBuilder/Core/DA_SurvivalConfig`, assigned to BP_AGB_Character.
2. For quick testing: `SetVitalStat` `{"statId":"Hunger","displayName":"auto","maxValue":-1,"startValue":30,"changePerSecond":0,"minutesToEmpty":2}` (and similar for Thirst with startValue 40).
3. Play: bottom-left bars (Health, Stamina, Food, Water) and the air temperature. Sprint → stamina drains, stops sprinting at 0, regenerates after 1 s. Jumping costs stamina.
4. Eat: select berries (Hunger 10) on the hotbar, left-click → Food +10, one berry used. In the Tab screen, hover berries + E also eats.
5. Drink: walk to a lake/river shore, look at the water within ~2.5 m → "[E] Drink water" → Water +20.
6. Fall damage: jump from a cliff over ~6 m → health drops. Starve (wait for Food 0) → health drains.
7. Temperature: climb high (or `SetTemperatureRules` `{"baseTemperatureC":2}`) → "Cold!" and slow health loss; `SpawnHeatSource` next to you → warm again. A Head item with stat `Insulation` 10 also helps.
8. Die (falls or very cold): ragdoll, "You died" screen with the cause, respawn at the player start after 5 s with full stats and starting items; your items are in a bag where you died: "[E] Take <name>'s items".
9. Co-op (listen server): each player's bars are their own; a client's death/respawn works; bags can be taken by either player.
10. Restore: `SetupSurvival` `{"difficulty":"Normal"}` resets the test values.
**Claude Code:** "Set up survival rules from the design and make berries and water restore food and thirst." → SurvivalTools + ItemTools (stats), then marks the M3 tasks.

## Phase 14 — Harvesting

Prereq: C++ build; open TestWorld (trees from Phase 7); run `SetupGameFoundation` `{"perspective":"ThirdPerson"}` again (assigns the swing animation).
1. `ListWorldMeshes` `{}` → the Megaplant tree meshes (Foliage) with counts and `bHittable`; any rock/bush meshes.
1b. Meshes not hittable: `SetupHarvestCollision` trees `{"meshPaths":[...],"shape":"Trunk","mode":"Solid"}`, rocks ("Box" or "auto", Solid), bushes ("Box", "HarvestOnly"); `ListWorldMeshes` again → bHittable true; Save All. In Play: you can no longer walk through trees and rocks, but still through bushes.
2. Items: `CreateItem` `stone_axe` again with tags `["Tool.Axe"]` and stats `[{"name":"HarvestPower","value":2}]`; a `stone_pickaxe` (Tool, MainHand, tags `["Tool.Pickaxe"]`, HarvestPower 2); `fiber` (Resource, 100, 0.1) if missing.
3. `CreateResource` `{"resourceId":"tree","displayName":"Tree","meshPaths":[<tree meshes>],"toolTags":["Tool.Axe"],"bAllowHands":false,"health":10,"yieldPerHit":[{"itemId":"wood","minCount":1,"maxCount":2}],"yieldWhenDepleted":[{"itemId":"wood","minCount":4,"maxCount":6}],"regrowMinutes":2,"bFallWhenDepleted":true}` → message says how many placed trees are now harvestable.
4. `SpawnResourceNode` a rock: first `CreateResource` "rock" with `/Engine/BasicShapes/Sphere`, toolTags `["Tool.Pickaxe"]`, bAllowHands true, yields stone; then place one near the start.
5. Play, bare hands at a tree: crosshair "Tree: Needs a tool: Axe" (orange); left click → "Needs a tool: Axe". With the axe selected: "Tree" (white); each swing → "+2 Wood" (power 2), about 5 swings → the tree topples (if the mesh has simple collision) and disappears; wood in the hotbar/inventory.
6. The rock: by hand → +1 stone per hit; with the pickaxe → +2 per hit; depleted → it disappears, back after the regrow time.
7. Wait 2 minutes → the tree grows back.
7b. Swing stays in place (no lunge). `SetItemHandling` on the axe/pickaxe until they sit right in the hand (rotate 90/180 on one axis at a time). A felled tree topples, settles within ~3 s and sinks into the ground (no rolling).
8. Co-op (listen server): a tree felled by one player disappears for both; a client joining later sees it gone; it regrows for both.
**Claude Code:** "Make the trees, rocks and bushes in TestWorld harvestable: axe for trees (wood), pickaxe for rocks (stone, flint), hands for bushes (fiber, berries)." → ListWorldMeshes, ItemTools, CreateResource, then marks the M4 tasks.

### Phase 14b — Asset pack list (Python) and smoother tree falls
1. Restart the editor (Python changed). Ask Claude in UE: "Run seed_plan_from_template again and show me the asset packs I should add." → 10 survival pack suggestions (essential: melee/tool animations, tools & weapons, nature pack, landscape surfaces), search terms filled from your setting/art style, already-present content can be marked added; existing tasks are not duplicated.
2. `GameDesign.md` has an "Asset packs to add" table; `get_project_memory` lists essential packs not added yet.
3. After adding a pack in Fab: "I added <pack> at /Game/<folder>" → `update_asset_pack(..., "added", path)` and the AI sets it up.
4. Felling a tree: it topples away from you over ~2 s smoothly (no physics stutter), rests 1 s, sinks. Same in co-op on both screens.

## Phase 15 — Crafting

Prereq: C++ build. Items from earlier phases (wood, stone, fiber, berries...); give wood the tag `Fuel` (CreateItem again with tags ["Fuel"], stats [{"name":"BurnSeconds","value":45}]).
1. `CreateStation` `{"stationId":"campfire","displayName":"Campfire","meshPath":"auto","meshScale":1,"bNeedsFuel":true,"warmthC":20,"heatRadiusM":6,"lightIntensity":60,"craftRangeM":4,"placeableItemId":"auto"}` → DA_Station_campfire + item "campfire" (Placeable). Same for "workbench" (no fuel, no light).
2. Recipes: `stone_axe` (3 wood + 2 stone, 4 s, by hand, Default); `campfire` (5 wood + 3 stone, 3 s, by hand); `workbench` (10 wood + 4 stone, 5 s, by hand, Discover); `cooked_berries` or `cooked_meat` (1 berries → 1 cooked, 5 s, station campfire); `stone_pickaxe` (station workbench). `ListCrafting` shows them.
3. Play, Tab: crafting panel on the right; ingredients show have/need (green/red). Click stone axe → queue entry with progress bar, ingredients gone; finished → "+1 Stone Axe". Shift+click → 5 queued (only as many as you can afford). Click the queue entry → cancelled, ingredients back.
4. Discover: before carrying wood and stone, "Workbench" is missing from the list; pick both up → "New recipe: Workbench".
5. Craft a campfire, select it on the hotbar, left-click the ground → campfire placed (not on steep ground or right next to another station). E on it → crafting screen titled "Campfire (out of fuel)"; cooked recipe says "light Campfire"; "Add fuel" uses one wood → "burning 0:45", light and warmth (temperature rises); cook. Walk away mid-craft → "(waiting for Campfire)"; come back → continues. Fuel runs out → light off, crafting pauses.
6. Workbench recipe shows "needs Workbench" until one is placed nearby.
7. `SpawnStation` `{"stationId":"campfire","xM":..,"yM":..,"yawDeg":0,"fuelSeconds":600}` → a lit campfire in the level.
8. Co-op: each player has their own queue and known recipes; a campfire lit by one is lit for both; both can cook at it.
**Claude Code:** "Set up crafting from the design: campfire and workbench stations, tier-1 tool recipes, cooking at the campfire." → CraftingTools (+ ItemTools for missing items), then marks the M5 tasks.

