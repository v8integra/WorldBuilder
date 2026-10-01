# AI World Builder — Tool Reference

All tools are reached through Unreal MCP's tool-search meta-tools (`list_toolsets` → `describe_toolset` → `call_tool`).
Units: all distances are **meters** in world space unless stated otherwise.

`call_tool` takes three fields: `toolset_name` (string), `tool_name` (string), `arguments` (object).
In the MCP Inspector form, type names **without quotes**; only the `arguments` box takes JSON (`{}` when there are none).

_Last updated: Phase 1._

---

## `aiworldbuilder.toolsets.info.WorldBuilderInfoTools` (Python)
Basic plugin information.

### `ping() -> str`
Confirms the Python toolsets are loaded.
```json
call_tool { "toolset_name": "aiworldbuilder.toolsets.info.WorldBuilderInfoTools", "tool_name": "ping", "arguments": {} }
→ "AIWorldBuilder 0.1.0"
```

---

## `AIWorldBuilderToolsets.WorldBuilderDiagnosticsToolset` (C++)
Plugin and level diagnostics. Call this first in a session.

### `GetPluginStatus() -> FWorldBuilderPluginStatus`
| Field | Type | Meaning |
|---|---|---|
| bSuccess | bool | Status read OK |
| Message | string | One-line summary |
| Version | string | Plugin version |
| LevelName | string | Open level |
| LandscapeCount | int | `ALandscape` actors in the level |
| bWorldPartitionEnabled | bool | Level uses World Partition |

```json
call_tool { "toolset_name": "AIWorldBuilderToolsets.WorldBuilderDiagnosticsToolset", "tool_name": "GetPluginStatus", "arguments": {} }
→ {"returnValue":{"bSuccess":true,"message":"AI World Builder 0.1.0 loaded. Level 'Lvl_FirstPerson' has 0 landscape(s); World Partition enabled.","version":"0.1.0","levelName":"Lvl_FirstPerson","landscapeCount":0,"bWorldPartitionEnabled":true}}
```
