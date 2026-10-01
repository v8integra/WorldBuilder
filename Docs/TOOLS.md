# AI World Builder — Tool Reference

All tools are reached through Unreal MCP's tool-search meta-tools (`list_toolsets` → `describe_toolset` → `call_tool`).
Units: all distances are **meters** in world space unless stated otherwise.

`call_tool` takes three fields: `toolset_name` (string), `tool_name` (string), `arguments` (object).
In the MCP Inspector form, type names **without quotes**; only the `arguments` box takes JSON (`{}` when there are none).

Every result has `bSuccess` and `message`; on failure `message` says why and what to do.
Optional `landscapeName` arguments take the Outliner label; omit it (default `"auto"`) when the level has one landscape
(point-based tools then pick the landscape containing the point).

_Last updated: Phase 4._

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

---

## `AIWorldBuilderToolsets.LandscapeSculptTools` (C++, writes terrain)
Real landscape heights, never meshes. Every call:
- writes into the **`AI Sculpt`** edit layer (created on first use; hide/lock/delete it in Landscape mode to manage all AI changes),
- is one **undo** step (`Ctrl+Z`), named e.g. "AI: Apply Volcano",
- forces the landscape to merge immediately and re-reads collision at the most-changed point (`bCollisionVerified`),
- refuses (changes nothing) if the result exceeds the landscape's height range, unless `bAllowClipping: true`;
  the message says the Scale Z needed,
- refuses areas larger than 4097 × 4097 samples or touching unloaded World Partition regions.

Shared result (`FWorldBuilderSculptResult`): `bSuccess`, `message`, `landscapeName`, `editLayerName`, `bCreatedEditLayer`,
`regionMinM`/`regionMaxM`, `sampleCount`, `changedSampleCount`, `minChangeM`/`maxChangeM`, `newMinHeightM`/`newMaxHeightM`,
`clippedSampleCount`, `bCollisionVerified`, `verifyPointM`, `collisionHeightM`.

**Enums** (pass as strings): `shapeType` Mountain | Volcano | Crater | Hill | Plateau | Mesa;
line `shapeType` Valley | Ridge; `blendMode` Add | Max | Min | Replace | Blend; `falloff` Linear | Smooth | Sphere | Tip.

**Blend modes** (Ref = ground at the shape centre; line shapes interpolate ground between the ends):
| Mode | Result |
|---|---|
| Add | terrain + alpha × shape (keeps underlying bumps) |
| Max | raise toward Ref + shape, never lower |
| Min | lower toward Ref + shape, never raise |
| Replace | Ref + shape, blended at the footprint edge |
| Blend | lerp(terrain, Ref + shape, alpha) |

### `ApplyShape(shapeType, centerXM, centerYM, radiusM, heightM, blendMode = Add, blendAlpha = 1, noiseAmount = 0.3, seed = 1, craterRadiusM = 0, craterDepthM = 0, rimSharpness = 0.5, lavaChannels = 0, topFraction = 0.6, bAllowClipping = false, landscapeName = "auto")`
Crater: `radiusM` = rim radius, `heightM` = depth. Volcano: crater defaults 12 % of radius, 20 % of height deep.
```json
call_tool { "toolset_name": "AIWorldBuilderToolsets.LandscapeSculptTools", "tool_name": "ApplyShape", "arguments": { "shapeType": "Volcano", "centerXM": 0, "centerYM": 0, "radiusM": 600, "heightM": 120, "lavaChannels": 3 } }
```

### `ApplyLineShape(shapeType, startXM, startYM, endXM, endYM, widthM, heightM, blendMode = Add, blendAlpha = 1, noiseAmount = 0.3, seed = 1, bAllowClipping = false, landscapeName = "auto")`
```json
call_tool { "toolset_name": "AIWorldBuilderToolsets.LandscapeSculptTools", "tool_name": "ApplyLineShape", "arguments": { "shapeType": "Valley", "startXM": -800, "startYM": 500, "endXM": 800, "endYM": 300, "widthM": 120, "heightM": 20 } }
```

### `RaiseLower(centerXM, centerYM, radiusM, deltaM, falloff = Smooth, bAllowClipping = false, landscapeName = "auto")`
### `Flatten(centerXM, centerYM, radiusM, targetHeightM, strength = 1, edgeFraction = 0.3, falloff = Smooth, bAllowClipping = false, landscapeName = "auto")`
```json
call_tool { "toolset_name": "AIWorldBuilderToolsets.LandscapeSculptTools", "tool_name": "Flatten", "arguments": { "centerXM": 400, "centerYM": 0, "radiusM": 60, "targetHeightM": 50 } }
```
### `Smooth(centerXM, centerYM, radiusM, strength = 0.5, kernelRadiusM = 0, iterations = 3, falloff = Smooth, landscapeName = "auto")`
`kernelRadiusM` 0 = 5 % of the radius.
### `AddNoise(centerXM, centerYM, sizeXM, sizeYM, amplitudeM, wavelengthM = 200, octaves = 4, seed = 1, bRidged = false, bAllowClipping = false, landscapeName = "auto")`
### `CarvePath(pointsM, widthM, depthM, bankWidthM = 0, falloff = Smooth, bLowerOnly = true, bAllowClipping = false, landscapeName = "auto")`
`pointsM` is `[{"x":..,"y":..}, ...]` in meters. Bed = ground at each point − depth, interpolated; order river points downhill.
`bLowerOnly: false` cuts and fills (roads).
```json
call_tool { "toolset_name": "AIWorldBuilderToolsets.LandscapeSculptTools", "tool_name": "CarvePath", "arguments": { "pointsM": [{"x":-900,"y":300},{"x":-200,"y":100},{"x":600,"y":-400}], "widthM": 20, "depthM": 4 } }
```

---

## `AIWorldBuilderToolsets.WorldCaptureTools` (C++, eyes)
Every image is a **PNG saved to `<Project>/Saved/AIWorldBuilder/Captures/`**; results return absolute `filePath`s —
**open the file to look at it**. (Unreal MCP returns toolset results as text, so inline images would arrive as base64 text.)

Camera captures render through the **editor level viewport** (same lighting/Lumen as the editor; 4 warm-up frames),
then restore the user's camera. The viewport must be Perspective; PIE must be stopped.
Top-down images: **image top = +X, image right = +Y**.

Result (`FWorldBuilderCaptureResult`): `bSuccess`, `message`, `images[]` (`filePath`, `widthPx`, `heightPx`,
`cameraLocationM`, `pitchDeg`, `yawDeg`, `fovDeg`).

### `CaptureView(cameraXM, cameraYM, cameraZM, pitchDeg, yawDeg, fovDeg = 60, widthPx = 1280, heightPx = 720)`
Pitch: 0 horizontal, negative looks down. Yaw: 0 = +X, 90 = +Y.
### `CaptureLookAt(cameraXM, cameraYM, cameraZM, targetXM, targetYM, targetZM, fovDeg = 60, widthPx = 1280, heightPx = 720)`
```json
call_tool { "toolset_name": "AIWorldBuilderToolsets.WorldCaptureTools", "tool_name": "CaptureLookAt", "arguments": { "cameraXM": -1200, "cameraYM": -1200, "cameraZM": 500, "targetXM": 0, "targetYM": 0, "targetZM": 100 } }
```
### `CaptureOrbit(targetXM, targetYM, distanceM, count = 4, elevationDeg = 30, targetHeightAboveGroundM = 0, startYawDeg = 45, fovDeg = 60, widthPx = 1280, heightPx = 720, landscapeName = "auto")`
Cameras stay ≥ 5 m above terrain.
```json
call_tool { "toolset_name": "AIWorldBuilderToolsets.WorldCaptureTools", "tool_name": "CaptureOrbit", "arguments": { "targetXM": 0, "targetYM": 0, "distanceM": 1500, "count": 3, "targetHeightAboveGroundM": 60 } }
```
### `CaptureTopDown(centerXM, centerYM, sizeM, sizePx = 1024, fovDeg = 30, landscapeName = "auto")`
Perspective camera straight down, high enough to frame the square at its highest point.
### `ExportHeightPreview(centerXM = 0, centerYM = 0, sizeM = 0, widthPx = 1024, landscapeName = "auto")`
Drawn from terrain data (exact, lighting-independent): `heightMapPath` (grey: black = `minHeightM`, white = `maxHeightM`)
and `slopeMapPath` (green 0° → yellow-green 10° → yellow 20° → orange 30° → red 40° → purple 55°+, magenta = no data).
`sizeM = 0` maps the whole landscape. Also returns `metersPerPixel`, `regionMinM`/`regionMaxM`, `slopeLegend`.
```json
call_tool { "toolset_name": "AIWorldBuilderToolsets.WorldCaptureTools", "tool_name": "ExportHeightPreview", "arguments": {} }
```
