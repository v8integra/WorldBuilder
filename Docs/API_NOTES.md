# API Notes — UE 5.8.3 (CL 58210709)

Verified against the engine source at `C:\Program Files\Epic Games\UE_5.8`.
Test project: `C:\Users\Raven\Documents\Unreal Projects\TestProjectBlueprint` (Blueprint-only).

Paths below are relative to `Engine/` unless noted.

---

## 1. Toolset Registry / Unreal MCP

| Item | Value |
|---|---|
| MCP plugin | `ModelContextProtocol` (`Plugins/Experimental/ModelContextProtocol`). Modules: `ModelContextProtocol`, `ModelContextProtocolEngine`, `ModelContextProtocolEditor` |
| Registry plugin | `ToolsetRegistry` (`Plugins/Experimental/ToolsetRegistry`). Module: `ToolsetRegistry` |
| Stock toolsets | `Plugins/Experimental/Toolsets/*`, enabled together by `AllToolsets` |
| Console | `ModelContextProtocol.RefreshTools`, `.StartServer`, `.StopServer`, `.GenerateClientConfig` |
| Server status | The log shows `LogModelContextProtocol: Tool search enabled: registered 3 meta-tools (52 toolsets discoverable via list_toolsets)` in the test project |

### C++ toolset pattern (from `GASToolsets/.../AttributeSetToolset.h/.cpp`)
```cpp
#include "ToolsetRegistry/ToolsetDefinition.h"

/// Class doc comment = toolset description.
UCLASS(BlueprintType, Hidden)
class UMyToolset : public UToolsetDefinition
{
    GENERATED_BODY()
public:
    /**
     * Tool description.
     * @param Foo  Param description (becomes the schema description).
     * @return     Return description.
     */
    UFUNCTION(meta = (AICallable), Category = "X")
    static FMyResult DoThing(const FString& Foo);
};
```
- The base class is `UCLASS(BlueprintType, Abstract, MinimalAPI)`. There's a virtual `GetToolsetVersion()` on the CDO (defaults to `"1.0"`).
- `meta = (AIIgnore)` hides a UFUNCTION. Any other UFUNCTION on the class must be a valid AICallable, or registration errors.
- Errors: `UKismetSystemLibrary::RaiseScriptError(FString)` surfaces an error to the agent. The Kismet stack frame is set up by the registry.
- Return types in use: USTRUCTs, `TArray<>`, `TMap<>`, `TOptional<>` params, `UObject*`/`AActor*` (serialized as references), `FString`.
- **Registration is explicit, not automatic.** In the module's `StartupModule()`:
  `UToolsetRegistry::RegisterToolsetClass(UMyToolset::StaticClass());`
  and in `ShutdownModule()` (guarded by `UObjectInitialized()`) call `UnregisterToolsetClass`.
  Header: `ToolsetRegistry/UToolsetRegistry.h`.
- Module setup: `"Type": "Editor"`, `"LoadingPhase": "PostEngineInit"`, `TargetAllowList: ["Editor"]`. Build.cs private deps: `Core, CoreUObject, Engine, ToolsetRegistry, UnrealEd` (+ domain modules).
- Test harness: `UE::ToolsetRegistry::FToolCallExceptionHandler::CaptureErrorsIn(...)` lets C++ tests call tools directly and capture `RaiseScriptError`. `UToolsetRegistry::ExecuteTool(Toolset, Tool, JsonInput)` runs a tool as if an agent had called it.

### Python toolset pattern
**Plan correction:** the plan's `toolset_registry/toolsets/core/actor.py` doesn't exist. The real examples are:
- `Plugins/Experimental/Toolsets/EditorToolset/Content/Python/editor_toolset/toolsets/actor.py`
- `Plugins/Experimental/ToolsetRegistry/Content/Python/toolset_registry/tests/demo_toolset.py`

```python
import unreal, toolset_registry

@unreal.ustruct()
class MyResult(unreal.StructBase):
    success = unreal.uproperty(bool)
    message = unreal.uproperty(str)

@unreal.uclass()
class MyTools(unreal.ToolsetDefinition):
    """Toolset description."""

    @toolset_registry.tool_call
    @staticmethod
    def ping(msg: str = "") -> str:
        """Description.

        Args:
            msg: ...

        Returns:
            ...
        """
```
- Supported types: primitives, `list[]`, `dict[]`, `set[]`, `X | None` optionals, `unreal.Object`/`Actor`/`Class`, `@unreal.ustruct` in/out, and `@unreal.uenum`. `uproperty(meta={'ToolTip':..., 'ClampMin':..., 'ClampMax':...})` flows into the JSON schema.
- Errors: raise a Python exception (e.g. `ValueError`). Helper: `toolset_registry.helpers.require_editable(obj)`.
- **Python toolsets aren't auto-discovered either.** The plugin's `Content/Python/init_unreal.py` must register them:
  ```python
  from toolset_registry.registration import Registration
  _registration = Registration([MyTools]); _registration.register()
  ```
- Agent skills: `toolset_registry.agent_skill` (Python) and `UAgentSkill` (C++, `ToolsetRegistry/AgentSkill.h`). Epic ships skills like `editor_toolset/skills/default_outdoor_lighting.py`. This is a candidate place for the Phase 8 workflow skill, served *through MCP* instead of as a CLAUDE.md snippet.

### Toolset naming (verified via `list_toolsets` in the Inspector)
- C++: `<ModuleName>.<ClassName without U>`, e.g. `GASToolsets.AttributeSetToolset`. Ours → `AIWorldBuilderToolsets.LandscapeSculptTools`, etc.
- Python: full module path + class, e.g. `editor_toolset.toolsets.actor.ActorTools`. Ours → `aiworldbuilder.toolsets.<file>.<Class>`.
- The first line of the class docstring/comment is the summary shown in `list_toolsets`, so put the "when to use" sentence first.

### JSON result shape (verified in Phase 1)
- Return values are wrapped: `{"returnValue": {...}}`.
- UPROPERTY names are camelCased by lowercasing the first letter only; the `b` bool prefix is kept: `bSuccess`, `message`, `landscapeCount`, `bWorldPartitionEnabled`.
- `call_tool` arguments: `toolset_name`, `tool_name`, `arguments` (object).
- Argument keys are camelCased the same way (`XM` → `xM`, `LandscapeName` → `landscapeName`).
- **Required vs optional — two separate checks:**
  1. The schema (`JsonUtilities/.../JsonSchemaGenerator.cpp:293`) marks a parameter optional if it has a `default` or is an `FOptionalProperty`.
  2. **The call** (`ToolsetRegistry/.../ObjectFunctionToolCall.cpp:222-240`) rejects any omitted argument whose schema has no `default`, with the error *"input param X needs a default value"*. Being `TOptional` doesn't help here.
  - Defaults come from UHT's `CPP_Default_<Param>` metadata, via `ToolsetJson.cpp:76` `CustomJsonSchema`. **An empty default string counts as no default.**
  - **Rule:** to make an argument omittable, give it a **non-empty C++ default**. `= TEXT("")` and `TOptional<>` without a default both fail when the argument is omitted. Our optional landscape names use `= TEXT("auto")` (`AIWorldBuilder::LandscapeUtils::AutoLandscapeName`).

### Transactions / undo
- **Toolset calls are not wrapped in a transaction automatically.** `FScopedTransaction` only appears in `ModelContextProtocolToolLibrary.cpp` (the separate "tool library" path). The `NonTransactableToolCall` meta used by PCGToolset isn't read by any C++ in 5.8.3. **Our tools must open their own `FScopedTransaction`,** as the plan says.

### Images in tool results (Phase 4)
- `FToolsetImage` (`ToolsetRegistry/ToolsetImage.h`): `{ MimeType, Data(base64) }`, with `SetFromBitmap(TArray<FColor>, FIntPoint, ERGBFormat)` and `SetFromFile(Path)`.
- Async variant: `UToolCallAsyncResultImage` (`ToolCallAsyncResultImage.h`). Return it from a tool and call `SetValue()` when the render finishes.
- **We can return images directly.** A file path is only needed as a fallback or for saving.

### Already provided by Epic — reuse, don't rebuild
`EditorToolset/Source/EditorToolset/Private/EditorAppToolset.h`:
- `CaptureViewport(TOptional<FTransform> CaptureTransform, TOptional<FViewportAnnotationConfig>, bool bShowUI)` → `FViewportCapture { FToolsetImage Image; CameraLocation; ... }`
- `CaptureEditorImage()`, `CaptureAssetImage(path)`
- `GetCameraTransform` / `SetCameraTransform` / `FocusOnActors` / `ScreenCoordsToWorld`
- `StartPIE(FPIESessionOptions)` / `StopPIE()` / `IsPIERunning()` (async)

→ **Phase 4 impact:** `CaptureView` can wrap `CaptureViewport` with a meters-based camera. Our new work is `CaptureOrbit`, `CaptureTopDown` (ortho) and `ExportHeightPreview`.
→ **Phase 8 impact:** `RunPlayInEditor`/`StopPlayInEditor` already exist, so we only add `TraceGround`.

---

## 2. Landscape (module `Landscape`, editor helpers in `LandscapeEditor`)

Headers: `Source/Runtime/Landscape/Classes/{Landscape.h, LandscapeProxy.h, LandscapeInfo.h, LandscapeEditLayer.h, LandscapeLayerInfoObject.h}`, `Source/Runtime/Landscape/Public/{LandscapeEdit.h, LandscapeDataAccess.h, LandscapeEditTypes.h}`.

### Height encoding — confirmed (`LandscapeDataAccess.h`)
```cpp
#define LANDSCAPE_ZSCALE (1.0f/128.0f)
namespace LandscapeDataAccess {
  constexpr int32 MaxValue = 65535;  constexpr float MidValue = 32768.f;
  float  GetLocalHeight(uint16 H) { return (H - MidValue) * LANDSCAPE_ZSCALE; }
  uint16 GetTexHeight(float LocalH);  // clamps to [0, 65535]
}
```
World Z (cm) = `GetLocalHeight(H) * ActorScale.Z + ActorLocation.Z`. This matches the plan: `(H − 32768) × ScaleZ / 128`. With the default ScaleZ = 100, the range is about ±256 m. **A 400 m volcano needs ScaleZ ≥ ~160.** `GetHeightStats` and `ApplyShape` must detect this and report clipping.

### Reading heights
- Fast path: `ALandscapeProxy::GetHeightAtLocation(FVector, EHeightfieldSource = Complex) -> TOptional<float>` (uses collision).
- Region read: `FLandscapeEditDataInterface(ULandscapeInfo*)` → `GetHeightData(X1,Y1,X2,Y2, uint16*, Stride)` / `GetHeightDataFast(...)`. Coordinates are in landscape quad/sample space (inclusive).
- Extent: `ULandscapeInfo::GetLandscapeExtent(MinX,MinY,MaxX,MaxY)`, `ComponentSizeQuads`, `DrawScale`, `GetLandscapeProxy()`, `ForEachLandscapeProxy(fn)`, `GetSortedStreamingProxies()`.

### Reading heights — chosen path (Phase 2)
- **Read path chosen: collision heightfield.** `ALandscapeProxy::GetHeightAtLocation(Location, EHeightfieldSource::Editor)`, then falling back to `Complex` (`Editor` geometry only exists when the collision mip level is > 0; `GetHeight` does not fall back by itself). The lookup is constant-time (`Info->XYtoCollisionComponentMap`), read-only, and matches the surface players walk on.
- **Avoid `FLandscapeEditDataInterface` for reads.** `FLandscapeTextureDataInfo`'s constructor calls `Texture->Modify(bShouldDirtyPackage)` and `Source.LockMip()` (read-write). On destruction it clears `RF_Transactional` and updates the texture hash. If we ever need texture-accurate reads, wrap them in `FLandscapeDoNotDirtyScope`.
- `ULandscapeComponent::GetHeightmap(FGuid())` with an invalid GUID returns the final merged heightmap. `FLandscapeEditDataInterface(Info, FGuid(), false)` therefore reads final heights (and has the same dirtying caveat).
- Bounds: `ULandscapeInfo::GetCompleteBounds()` and `GetCompleteLandscapeExtent()` cover all World Partition proxies, loaded or not. `GetLoadedBounds()` and `GetLandscapeExtent()` (which iterates `XYtoComponentMap`) cover loaded data only.

### Writing heights (Phase 3)
Recommended: **`FHeightmapAccessor<false>`** (`LandscapeEdit.h:361`), which is the same path the editor sculpt brushes use:
```cpp
FHeightmapAccessor<false> Acc(LandscapeInfo);
Acc.SetEditLayer(LayerGuid);          // optional, else current editing layer
Acc.GetDataFast(X1,Y1,X2,Y2, Buf);    // read
Acc.SetData(X1,Y1,X2,Y2, Buf);        // write: calls RequestHeightmapUpdate on components + foliage snapping
// destructor flushes
```
Lower level: `FLandscapeEditDataInterface::SetHeightData(X1,Y1,X2,Y2, const uint16*, Stride, bCalcNormals, ..., bUpdateBounds, bUpdateCollision, bGenerateMips)`.

- **5.7+ removed non-edit-layer landscapes** (`RecalculateNormals` is deprecated: "removal of non-edit layer landscapes"). So every landscape has edit layers, and we always write into a specific edit layer.
- Edit layers (`ALandscape`, `Landscape.h`): `GetEditLayers()/GetEditLayersConst()` → `ULandscapeEditLayerBase*` (`GetName()`, `GetGuid()`, `IsVisible()`, `IsLocked()`), `GetEditLayer(FName|FGuid|int32)`, `GetLayerIndex(...)`, `CreateLayer(FName, TSubclassOf<ULandscapeEditLayerBase>, bIgnoreLimit) -> int32`, `SetEditingLayer(FGuid)` / `GetEditingLayer()`.
- Scoped: **`FScopedSetLandscapeEditingLayer(ALandscape*, const FGuid&, TFunction<void()> OnComplete)`** (`Landscape.h:793`).
- Refresh after writing: `ALandscape::RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode::Update_All)` or `ForceUpdateLayersContent()`.
- **Design decision:** by default the AI writes into a dedicated edit layer named **"AI Sculpt"** (created on demand), so its work stays separate from hand sculpting and can be hidden or deleted as a unit. If a layer is locked, the tool returns an error.
- Alternatives found: `LandscapeEditorUtils::SetHeightmapData(ALandscapeProxy*, const TArray<uint16>&)` (whole proxy), `ALandscapeProxy::LandscapeImportHeightmapFromRenderTarget(RT, bRG, EditLayerIndex)`, `LandscapeExportHeightmapToRenderTarget`, `ALandscapeProxy::Import(...)` (creation, Phase 5).

### Weights / paint layers (Phase 6)
- `TAlphamapAccessor<bUseInterp>(ULandscapeInfo*, ULandscapeLayerInfoObject*)` → `GetData/GetDataFast/SetData(X1,Y1,X2,Y2, uint8*, ELandscapeLayerPaintingRestriction)`. **`FAlphamapAccessor` is deprecated in 5.7.**
- `FLandscapeEditDataInterface::SetAlphaData(LayerInfo, ..., PaintingRestriction)`. **`bWeightAdjust`/`bTotalWeightAdjust` were removed in 5.7**, so weight normalization is now handled by edit-layer merging and target-layer settings. Verify the blending behavior in Phase 6.
- Layer lookup: `ULandscapeInfo::GetLayerInfoByName(FName)`, `CreateTargetLayerSettingsFor(LayerInfo)`, `ALandscapeProxy::GetTargetLayers() -> TMap<FName, FLandscapeTargetLayerSettings>`. `EditorLayerSettings` is deprecated.
- Material: `ALandscapeProxy::LandscapeMaterial`, `EditorSetLandscapeMaterial()`.

### Proxy layout fields (`LandscapeProxy.h`)
`ComponentSizeQuads`, `SubsectionSizeQuads`, `NumSubsections`, `GetLandscapeActor()`, `GetLandscapeInfo()`, `GetLandscapeGuid()`.

---

## 3. PCG (plugin `PCG`, module `PCG`)
- `UPCGComponent` (`Plugins/PCG/Source/PCG/Public/PCGComponent.h`): `SetGraph(UPCGGraphInterface*)`, `Generate()` / `Generate(bool bForce)`, `Cleanup()` / `Cleanup(bool bRemoveComponents)`, plus `GenerateLocal`/`CleanupLocal` variants. Class `APCGVolume : AVolume`.
- **Epic's `PCGToolset` already covers most of Phase 7.** It includes `CreateGraph`, `AddNode`/`ConnectNodePins`, `SpawnGraphInstance(Graph, Name, Transform, JsonParams)`, `ExecuteGraphInstance(APCGVolume*)`, `Get/Set/ResetGraphInstanceParams`, `ListGraphInstances`, `RunPCGInstantGraph`, and a `PCGGraphGenerationSkill`.
  → **Phase 7 impact:** don't duplicate these. Our value-add is (a) a ready-made forest scatter graph asset with slope, height and layer rules, (b) a meters-based `SpawnPCGVolume(region)` convenience that wraps theirs, (c) the `ScatterFoliage` fallback, and (d) `CleanupPCG`, which theirs lacks.

---

## 4. Other plugins found
- `PythonScriptPlugin` → `Plugins/Experimental/PythonScriptPlugin`
- `Water`, `Landmass` → `Plugins/Experimental/` (for later ideas)
- **No stock landscape toolset exists.** The only mention is in `PCGToolsetLibraryCore.h`. This confirms the gap the plugin fills.

## 4b. Claude Code plugin
Epic's **"Unreal Engine Skills"** plugin (publisher: epicgames, repo `github.com/epicgames/unreal-engine-skills-for-claude-code-plugin`, v3.0.4) is in the Anthropic plugin directory. It provides the skills `create-toolset`, `unreal-mcp` and `unreal-skill`, plus a SessionStart hook. It's **not installed yet**. If installed, use `create-toolset` for Phase 1 scaffolding.

## 5. Toolchain
- Visual Studio Build Tools 18 (`C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`)
- `Engine/Build/BatchFiles/Build.bat` is present
- Node v24.18.1 (for `npx @modelcontextprotocol/inspector`)

## 6. Plan deviations summary
1. Python reference file path is wrong (see §1).
2. Neither C++ nor Python toolsets are auto-discovered. Both need explicit registration.
3. Toolset calls get no automatic undo transaction. We add our own (the plan already requires this).
4. Images can be returned inline via `FToolsetImage`.
5. Epic already provides viewport capture, PIE control, and PCG graph/volume tools. Phases 4, 7 and 8 shrink accordingly.
6. Non-edit-layer landscapes no longer exist (5.7+), and `FAlphamapAccessor` plus weight-adjust flags are deprecated.
7. Default Z scale (100) limits heights to about ±256 m. Tools must warn and suggest a larger ScaleZ.
8. The test project is Blueprint-only. To compile our C++ modules, the project needs a C++ target (Phase 1 adds a stub game module), or the plugin must be built separately.
