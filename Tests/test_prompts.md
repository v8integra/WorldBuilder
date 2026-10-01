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
