# AI World Builder — Tool Reference

All tools are reached through Unreal MCP's tool-search meta-tools (`list_toolsets` → `describe_toolset` → `call_tool`).
Units: all distances are **meters** in world space unless stated otherwise.

`call_tool` takes three fields: `toolset_name` (string), `tool_name` (string), `arguments` (object).
In the MCP Inspector form, type names **without quotes**; only the `arguments` box takes JSON (`{}` when there are none).

Every result has `bSuccess` and `message`; on failure `message` says why and what to do.
Optional `landscapeName` arguments take the Outliner label; omit it (default `"auto"`) when the level has one landscape
(point-based tools then pick the landscape containing the point).

_Last updated: Phase 2._

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

---

## `AIWorldBuilderToolsets.LandscapeInspectTools` (C++, read-only)
Find landscapes and measure terrain. Heights are read from the landscape's **collision surface**
(full-resolution editor heightfield when available), so they match what a player walks on. Unloaded
World Partition regions return no data and are reported in `message` / `missingCount`.

### `ListLandscapes()`
Per landscape: `name`, `boundsMinM`/`boundsMaxM`, `sizeXM`/`sizeYM`, `resolutionX`/`resolutionY` (samples),
`sampleSpacingM`, `componentCount` / `loadedComponentCount`, `componentSizeQuads`, `sectionsPerComponent`,
`locationM`, `scale`, `minPossibleHeightM`/`maxPossibleHeightM` (clip range from Z scale), `editLayers[]`
(`name`, `bIsEditingLayer`, `bVisible`, `bLocked`), `bUsesWorldPartition`, `streamingProxyCount`.
```json
call_tool { "toolset_name": "AIWorldBuilderToolsets.LandscapeInspectTools", "tool_name": "ListLandscapes", "arguments": {} }
```

### `GetLandscapeInfo(landscapeName = "auto")`
`summary` (as above) + `materialPath` + `paintLayers[]` (`name`, `bHasLayerInfo`, `layerInfoPath`).
```json
call_tool { "toolset_name": "AIWorldBuilderToolsets.LandscapeInspectTools", "tool_name": "GetLandscapeInfo", "arguments": { "landscapeName": "Landscape" } }
```

### `SampleHeight(xM, yM, landscapeName = "auto")`
Ground height (`heightM`) at a world point.
```json
call_tool { "toolset_name": "AIWorldBuilderToolsets.LandscapeInspectTools", "tool_name": "SampleHeight", "arguments": { "xM": 0, "yM": 0 } }
```

### `SampleHeightGrid(centerXM, centerYM, sizeM, gridCount = 16, landscapeName = "auto")`
Square grid, `GridCount` 2–64 per side. `heights[row * gridCount + col]` is at
(`originXM + col*spacingM`, `originYM + row*spacingM`); no-data points hold `missingValue` (-1000000).
```json
call_tool { "toolset_name": "AIWorldBuilderToolsets.LandscapeInspectTools", "tool_name": "SampleHeightGrid", "arguments": { "centerXM": 0, "centerYM": 0, "sizeM": 500, "gridCount": 8 } }
```

### `GetHeightStats(centerXM = 0, centerYM = 0, sizeXM = 0, sizeYM = 0, landscapeName = "auto")`
Region stats (sizes 0 = whole landscape; region is clipped to the landscape; at most 256×256 points):
`minHeightM`, `maxHeightM`, `averageHeightM`, `lowestPointM`, `highestPointM`, `averageSlopeDeg`,
`maxSlopeDeg`, `flatFraction` (<5°), `steepFraction` (>35°), `sampleCount`, `sampleSpacingM`, `missingCount`.
Slope is measured at full landscape resolution at each point.
```json
call_tool { "toolset_name": "AIWorldBuilderToolsets.LandscapeInspectTools", "tool_name": "GetHeightStats", "arguments": {} }
```

> Argument keys are camelCase (`xM`, `centerXM`, `sizeM`, `gridCount`, `landscapeName`): the first letter of the C++ parameter name is lowercased. `landscapeName` defaults to `"auto"` everywhere and can be omitted.
