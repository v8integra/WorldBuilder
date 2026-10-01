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
