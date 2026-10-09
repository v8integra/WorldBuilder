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

### ⚠️ Images do NOT reach the agent as images (verified in Phase 4)
`ModelContextProtocolToolsetRegistryAdapter.cpp:76/267` wraps **every** toolset result in `MakeTextResult(JsonString)`. A returned `FToolsetImage` (or a struct that contains one, like Epic's `FViewportCapture`) is serialized as `{mimeType, data: <base64>}` **inside a text block**. That's megabytes of text, not an MCP image content block. Only non-toolset MCP tools (`ModelContextProtocolToolUtils.cpp:101`, `MakeImageResult`) produce real image content.
→ **Our capture tools save PNGs to `Saved/AIWorldBuilder/Captures/` and return file paths.** Claude Code opens the PNG to look at it.

### Viewport capture technique (Phase 4)
Same approach as `UEditorAppToolset::CaptureViewport` (`EditorAppToolset.cpp:1076`):
- Take `GCurrentLevelEditingViewportClient` (`Editor.h`), or fall back to the first perspective client in `GEditor->GetLevelViewportClients()`.
- Save the camera location, rotation, `ViewFOV` and show flags (ModeWidgets, SelectionOutline, Selection).
- `SetViewLocation` / `SetViewRotation`, then `Invalidate` + `Viewport->Draw()` + `FlushRenderingCommands()`. We do this 4 times so temporal AA and exposure settle.
- `GetViewportScreenShot(Viewport, Bitmap, CropRect)` (`UnrealClient.h:922`). Force alpha to 255, then restore everything with `ON_SCOPE_EXIT`.
- Aspect: crop the viewport to the requested aspect and widen `ViewFOV` so the crop spans the requested horizontal FOV: `vpFov = 2·atan(tan(fov/2)·vpW/cropW)`. Resize with `FImageUtils::ImageResize`, save with `FImageUtils::SaveImageByExtension(Path, FImageView(FColor*, W, H))`.
- Top-down: pitch −89.9 with yaw 0 gives **image top = +X, right = +Y**. `ExportHeightPreview` uses the same orientation.

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
  - ⚠️ **Bug found in Phase 5:** in World Partition, the "complete" functions enumerate **actor descriptors**, and those only exist for **saved** actors (`LandscapeEdit.cpp:4717`, `Landscape.cpp:7199`). A landscape created or re-gridded since the last save returns **empty bounds and extent** (0 × 0 m, 0 components). **Always use `LandscapeUtils::GetCompleteBounds` / `GetCompleteExtent`**, which union the complete result with the loaded result.

### Writing heights — implemented path (Phase 3)
Verified in `LandscapeEditLayers.cpp`, `LandscapeEditLayer.cpp`, `LandscapeEdModeTools.h`, `LandscapeEditLayersHeightmaps.usf`:
- **Standard edit layers are additive.** `ULandscapeEditLayerBase::GetBlendMode()` returns `LSBM_AdditiveBlend`, and the shader does `Final += LayerAlpha * (LayerValue − 32768)`. Each layer stores a delta around 32768. New layers start empty, via `ALandscapeProxy::AddLayer` → `InitializeLayerWithEmptyContent`.
- `ALandscape::CreateLayer(FName, Class = ULandscapeEditLayer, bIgnoreLimit)` calls `Modify()` (transactional), appends to the top of the stack and returns the index, or `INDEX_NONE` at the max layer count. It then requests layer re-initialization.
- **Merged heights for any subset of layers:** `ALandscape::SelectiveRenderEditLayersHeightmaps(FLandscapeEditLayerRenderHeightParams{Bounds (half-open, landscape coords), ActiveEditLayers bit array, CpuResult})`. This is what the editor's `FLandscapeEditLayerStackDataCache` uses for "bottom layers". It's read-only and dirties nothing.
  - ⚠️ It does `check(bLandscapeLayersAreInitialized)` **before** its `CanUpdateLayersContent()` early-out. Always call `ForceUpdateLayersContent()` first, since `UpdateLayersContent` runs `InitializeLayers()`. Also guard with the public pieces of `CanUpdateLayersContent`: `FApp::CanEverRender()`, `Info->AreAllComponentsRegistered()` and `Info->SupportsLandscapeEditing()`.
- **Our algorithm:** render F (all visible layers) and B (all visible layers except "AI Sculpt"), compute the new heights N from F, then write `L' = (N − B) / alpha` into "AI Sculpt" via `FHeightmapAccessor<false>` + `SetEditLayer(guid)` + `SetData`. This matches the editor's own combined-layer write (`LandscapeEdModeTools.h:889`). The merged result is exactly N, and the layer holds only the AI's contribution.
- **Undo:** `FScopedTransaction` around everything. `FLandscapeTextureDataInfo` sets `RF_Transactional` and calls `Modify()` on each touched layer heightmap texture, so the texture state is recorded. `Transaction.Cancel()` on any refusal.
- **After writing:** `ALandscape::ForceUpdateLayersContent()`, which is `UpdateLayersContent(bWaitForStreaming=true, bSkipMonitor=true, bFlushRender=true)`. This merges synchronously and updates collision, so the next tool call and `SampleHeight` see the result.
- `ForceUpdateLayersContent(bool)` is deprecated in 5.7. Use the no-argument version.
- **Name clash:** a global `::EBlendMode` (material blend mode) exists, so don't name our own enum `EBlendMode` in any namespace that might be `using`-ed.

### Creating landscapes (Phase 5)
Source: `LandscapeEditorDetailCustomization_NewLandscape.cpp:1145` `OnCreateButtonClicked`.
- **Non-region path (what we use):** `World->SpawnActor<ALandscape>(Location, Rotation)`, set `LandscapeMaterial`, `SetActorRelativeScale3D`, `StaticLightingLOD = DivideAndRoundUp(CeilLogTwo(SizeX*SizeY/(2048*2048)+1), 2)`, then
  `Import(FGuid::NewGuid(), 0, 0, SizeX-1, SizeY-1, SectionsPerComponent, QuadsPerSection, {FGuid() → heights}, HeightmapFileName, {FGuid() → layers}, ELandscapeImportAlphamapType, TArrayView<const FLandscapeLayer>())`, then
  `Info->UpdateLayerInfoMap(Landscape)` and `ULandscapeSubsystem::ChangeGridSize(Info, GridSizeInComponents)` (it splits into streaming proxies when `IsGridBased()`). The editor wraps all of this in one `FScopedTransaction`.
- **Region path** (World Partition, landscape larger than `WorldPartitionRegionSize`, default 16 components): it needs a saved map, creates `ALocationVolume` regions (`LandscapeRegionUtils`, which is **private**), adds components region by region, and saves and unloads proxies. It's **not undoable**. This is needed for truly huge maps such as 50 km. Not implemented yet.
- UI limits (`LandscapeEditorObject.h:840`): at most 256 components and ≤ 8191 quads per side. Defaults: `WorldPartitionGridSize = 2`, `WorldPartitionRegionSize = 16`.
- Valid `QuadsPerSection`: 7, 15, 31, 63, 127, 255. Sections per component: 1 or 2. Size = components × quadsPerComponent + 1.
- File import: `FLandscapeImportHelper` (`LandscapeEditor/Public/LandscapeImportHelper.h`), via `GetHeightmapImportDescriptor(Path, bSingleFile, bFlipY, Desc, Msg)` and `GetHeightmapImportData(Desc, 0, Data, Msg)`. Use `bSingleFile=false` with `ExtractCoordinates()` for `_x0_y0` tile sets. Results are `ELandscapeImportResult` {Success, Warning, Error}.
- 16-bit PNG write: `FImageUtils::SaveImageByExtension(Path, FImageView(uint16*, W, H, ERawImageFormat::G16))`.
- Our refactor: `LandscapeEditPipeline.h/.cpp` (RunSculpt, SplitIntoTiles, AccumulateTileResult, RenderMergedHeights, CanEditLandscape) is shared by the sculpt, generate and import tools.

### Painting weights (Phase 6)
- **Layer Info creation** (same steps as the editor's "+" button, `LandscapeEditorDetailCustomization_TargetLayers.cpp:2348`):
  1. `UE::Landscape::GetLayerInfoObjectPackageName(Name, Folder, OutObjectName)` gives a unique `<Name>_LayerInfo[_N]`.
  2. `UE::Landscape::CreateTargetLayerInfo(Name, Folder, ObjectName)` (`LandscapeUtils.h`) duplicates the project's default Layer Info template if one is set, marks the package dirty and notifies the asset registry. **It doesn't save.**
  3. `Landscape->AddTargetLayer` / `UpdateTargetLayer(Name, FLandscapeTargetLayerSettings(Info))`, then `Info->CreateTargetLayerSettingsFor(Info)` and `UpdateLayerInfoMap`.
- **Material layer names:** `ALandscapeProxy::RetrieveTargetLayerNamesFromMaterials(bIncludeVisibilityLayer)`. **`GetLayersFromMaterial` is deprecated in 5.8.** Target layers now live on the proxy (`GetTargetLayers()` → `TMap<FName, FLandscapeTargetLayerSettings>`), and `EditorLayerSettings` is deprecated.
- **Weight blending** (5.7+): `ULandscapeLayerInfoObject::BlendMethod` is one of `ELandscapeTargetLayerBlendMethod` {None (**the default**), FinalWeightBlending (legacy), PremultipliedAlphaBlending ("Advanced", works per edit layer, with a `BlendGroup`)}. It replaces `bNoWeightBlend`. Set it with `SetBlendMethod(Method, bModify)`.
- **Weights in edit layers** (`LandscapeEditLayersWeightmaps.usf`): per edit layer and target layer, blending is additive by default. With premultiplied blending, `Final = Prev·(1 − groupSum) + alpha·Current/max(groupSum,1)`, so a layer whose weights sum to 1 fully overrides the layers below.
- **Writing:** `TAlphamapAccessor<false>(Info, LayerInfo)`, then `SetEditLayer(guid)`, then `GetDataFast` / `SetData(X1,Y1,X2,Y2, uint8*, ELandscapeLayerPaintingRestriction::None)`. This handles weightmap allocation (`SetAlphaData`) and calls `RequestWeightmapUpdate`. **`FAlphamapAccessor` is deprecated in 5.7.**
- Exclude the visibility (holes) layer with `UE::Landscape::IsVisibilityLayer(Info)`.

### Foliage and PCG (Phase 7)
- **Foliage:** `AInstancedFoliageActor::AddInstances(WorldContext, FoliageType, Transforms)` is a UFUNCTION but **not exported** (no `FOLIAGE_API`), so it can't be linked from C++. Its logic is reproduced from exported functions: `AInstancedFoliageActor::Get(World, bCreateIfNone, PersistentLevel, Location)` (the World Partition cell foliage actor), then `IFA->AddFoliageType(Type, &Info)` and `FFoliageInfo::AddInstances(Type, TArray<const FFoliageInstance*>)`. Removal: `ForEachFoliageInfo`, `FFoliageInfo::GetInstancesOverlappingBox`, `RemoveInstances(indices, bRebuildTree)`.
- Foliage Type asset: `NewObject<UFoliageType_InstancedStaticMesh>(Package, Name, RF_Public|RF_Standalone|RF_Transactional)`, then `SetStaticMesh` and `FAssetRegistryModule::AssetCreated`.
- Paint weight at a point: `ULandscapeComponent::GetLayerWeightAtLocation(WorldPos, LayerInfo)`. The component is found via `Info->XYtoComponentMap` and the floor of local XY / ComponentSizeQuads.
- **PCG:** Epic's `UPCGToolset` has **no export macro**, so it can't be called from our C++. Use the native API: `UPCGGraph::AddNodeOfType` / `AddEdge`, and `UPCGComponent::SetGraph`, `Seed`, `Generate(bForce)` (asynchronous), `Cleanup(bRemoveComponents)`, `bGenerated`.
- Spawning `APCGVolume` with real bounds (copied from `PCGToolset.cpp:336`): `UCubeBuilder` with X/Y/Z in cm, then `UActorFactory::CreateBrushForVolumeActor(Volume, Builder)`, then on the brush component `ReregisterComponent` and `SetCollisionEnabled(NoCollision)`.
- Range filters (`UPCGAttributeFilteringRangeSettings`) need `FPCGMetadataTypesConstantStruct` thresholds. Hand-building graphs in C++ is fiddly, so the forest graph is authored with Epic's PCGToolset instead.

### Skeletal → static conversion and GPU safety (Phase 7b)
- **The 2026-10-02 crashes** were GPU hangs (`DXGI_ERROR_DEVICE_HUNG`, with breadcrumbs in `RenderVirtualShadowMaps(Nanite)`) on an RTX 3070, after a dense PCG forest was generated. PCG volumes default to **`GenerateOnLoad`**, so the level re-generated the forest and hung **every time it was opened**, which looked like level corruption. Fix: `SpawnPCGVolume` sets `UPCGComponent::GenerationTrigger = GenerateOnDemand`, and `ScatterFoliage` has a `maxInstances` cap.
- **Megaplants** (Fab/Quixel, made with Epic's **Procedural Vegetation Editor**): each tree is a `USkeletalMesh` built from instanced branch skeletal meshes, with `PVE_*` graph assets. Their master materials live in `/ProceduralVegetationEditor/...`, so **that plugin must be enabled** or the materials fail to load.
- **"Make Static Mesh"** = `IMeshUtilities::ConvertMeshesToStaticMesh(TArray<UMeshComponent*>, RootTransform, PackageName)` (module `MeshUtilities`). It needs `SkinnedComponent->MeshObject` (a registered render object) and `IsVisible()`. We register a transient `USkeletalMeshComponent` in a private `FPreviewScene`, then `FlushRenderingCommands()` before converting. Then `GetNaniteSettings`/`SetNaniteSettings` (direct `NaniteSettings` access is deprecated in 5.7) and `UEditorLoadingAndSavingUtils::SavePackages`. About 574 MB estimated build memory per Hornbeam tree.
- An alternative for later: `NaniteAssemblyEditorUtils` (`UNaniteAssemblyStaticMeshBuilder::BeginNewStaticMeshAssemblyBuild` / `AddAssemblyParts` / `FinishAssemblyBuild`) can build static **Nanite assemblies** (instanced parts) instead of one merged mesh, which is lighter. For wind animation, use PCG's `PCGSkinnedMeshSpawner` (instanced skinned meshes).

### Water (Phase 9)
- **Crash (2026-10-08, twice):** Claude in UE spawned `WaterBodyRiver` through Epic's `SceneTools.add_to_scene_from_class`. `FWaterEditorModule::OnLevelActorAddedToWorld` (`WaterEditorModule.cpp:130`, bound to `GEngine->OnLevelActorAdded`) saw `AffectsLandscape()` was true, spawned an `AWaterLandscapeBrush`, and `SetTargetLandscape` called `GetOrCreateEditLayer("Water")` + `AddBrushToLayer`. During the next `ALandscape::UpdateLayersContent`, the brush's `PushDeferredLayersContentUpdate` (with user-triggered requests) led to `check(Component->GetLayerUpdateFlagPerMode() == 0)` at `LandscapeEditLayers.cpp:7292`. This is an engine/Water-plugin interaction; our tools don't set user-triggered flags (`RequestHeightmapUpdate`'s `bInUserTriggered` defaults to false).
- **Avoidance:** `UWaterBodyComponent::bAffectsLandscape = false` before `BroadcastLevelActorAdded`. That broadcast happens **inside** `UWorld::SpawnActor` (`LevelActor.cpp:787`), **even for deferred spawns**, so setting the flag after spawning is too late. Use `World->AddOnActorPreSpawnInitialization(...)`, which is broadcast before `PostSpawnInitialize` (`LevelActor.cpp:753`) when components already exist.
- Spawn via `GEditor->FindActorFactoryForActorClass(Class)->CreateActor(Class, Level, Transform, Params)` to get the factory defaults (`WaterBodyActorFactory.cpp`): materials, spline defaults, ocean waves, `FillWaterZoneWithOcean`, and the custom water mesh. The editor module auto-creates an `AWaterZone`, sized from `Landscape->GetCompleteBounds()` (the unsaved-WP bug applies), so `CreateOcean` re-sizes the zone.
- Shapes: `UWaterSplineComponent::ResetSpline(localPoints)`. River metadata (`UWaterSplineMetadata::Depth / RiverWidth / WaterVelocityScalar`, `FInterpCurveFloat` keyed by point index) is followed by `K2_SynchronizeAndBroadcastDataChange()` and `UWaterBodyComponent::UpdateAll(FOnWaterBodyChangedParams{bShapeOrPositionChanged})`.
- Types: Ocean, Lake, River, Custom (`AWaterBodyCustom`, which uses `WaterMeshOverride` and `CanEverAffectWaterMesh() == false`, so it works without a water zone, underground included), Island (only meaningful with landscape deformation, so unused). Exclusion volumes are `AWaterBodyExclusionVolume`.

### Writing heights — original plan notes
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

## 7. Game framework (Phase 11, runtime module `AIGameBuilderRuntime`)

- Plugin is no longer `EditorOnly`; the runtime module is `Type: Runtime`, the world-building modules stay `Editor`.
- **Enhanced Input in C++:** `UInputMappingContext::MapKey(Action, Key)` returns `FEnhancedActionKeyMapping&`; add
  modifiers to `.Modifiers` (outer = the mapping context so they save with it). `UInputModifierSwizzleAxis` defaults to
  YXZ; `UInputModifierNegate` has public `bX/bY/bZ`. Keys: `EKeys::Mouse2D`, `Gamepad_Left2D`, `Gamepad_Right2D`.
  Add the context in `APawn::NotifyControllerChanged` via `ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()`.
  Mouse look: Negate Y, `AddControllerPitchInput(Y)` (legacy input scales still apply: `bEnableLegacyInputScales`).
- **Sprint prediction:** `FSavedMove_Character` (custom flag `FLAG_Custom_0`, override `Clear`, `GetCompressedFlags`,
  `CanCombineWith`, `SetMoveFor`, `PrepMoveFor`), `FNetworkPredictionData_Client_Character::AllocateNewMove`,
  `UCharacterMovementComponent::UpdateFromCompressedFlags` + `GetMaxSpeed`; allocate `ClientPredictionData` in
  `GetPredictionData_Client() const` with a const_cast (engine pattern). Don't copy saved moves (copy ctor not exported).
- **Camera mode is replicated:** first person uses controller yaw, third person orients to movement; the server must
  rotate the same way or other players see the wrong facing.
- **Project settings:** `UGameMapsSettings::GameDefaultMap` and `GlobalDefaultGameMode` are private: use the static
  setters, then `GetMutableDefault<UGameMapsSettings>()->TryUpdateDefaultConfigFile()`. `EditorStartupMap` is public.
  Input classes: `UInputSettings::SetDefaultInputComponentClass` / `SetDefaultPlayerInputClass`.
- **Blueprints from C++:** `FKismetEditorUtilities::CreateBlueprint(Parent, CreatePackage(path), Name, BPTYPE_Normal,
  UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass())`, `CompileBlueprint`, then set values on
  `GeneratedClass->GetDefaultObject()` (`Modify()` first) and save. `UBlueprint::GeneratedClass` is
  `TSubclassOf<UObject>`: use `.Get()` when assigning to `TSubclassOf<APawn>` etc.
- Appearance is data (`BodyMesh`, `BodyAnimClass`) applied in `OnConstruction`, so tools never edit inherited component
  templates. `UAnimBlueprint::TargetSkeleton` matches the mesh skeleton (mannequin: ABP_Unarmed ↔ SKM_Manny_Simple).
- **Inventory (Phase 12):** slots are a replicated `TArray<FAGBItemStack>` (item asset pointers replicate by path).
  Clients change contents only through Server RPCs on their **own** inventory component (the RPC needs a connection
  owner), passing the source/target components (replicated components are net-addressable).
- **Canvas HUD hit boxes** still work with Enhanced Input: `AddHitBox` each `DrawHUD`, `NotifyHitBoxClick` fires from
  `APlayerController::InputKey` when `bEnableClickEvents` is on; hover (`NotifyHitBoxBeginCursorOver`) is computed in
  `AHUD::PostRender` from the mouse position. Use `FInputModeGameAndUI` + `SetShowMouseCursor(true)` while open.
- **Input upgrades:** existing `IMC_AGB_Default` assets get only the missing actions mapped (`CreateDefaultInput(Factory,
  &Existing)`); at runtime an incomplete set gets a transient supplementary mapping context.
- `UBlueprint::GetBlueprintFromClass(Class)` instead of `ClassGeneratedBy`. Item ids are found through the asset
  registry tag of the `AssetRegistrySearchable` `ItemId` property.
- **Water for drinking (Phase 13):** Water plugin bodies use collision profile `WaterBodyCollision` (QueryOnly; Pawn,
  WorldDynamic, PhysicsBody overlap; **Visibility and Camera ignore**). So visibility traces pass through water: detect it
  with `LineTraceMultiByChannel(ECC_Pawn)` (overlaps before the blocking ground) and on the server with
  `OverlapMultiByChannel(ECC_Pawn)`, matching the profile name. No Water module dependency needed.
- **Death:** `DetachFromControllerPendingDestroy` → `APlayerController::OnUnPossess` switches the view target to the
  controller; call `SetViewTarget(body)` again to keep a death camera. `ACharacter::Landed` runs before the landing
  velocity is cleared (fall damage from `Velocity.Z`). Respawn with `AGameModeBase::RestartPlayer` on a timer.
- Non-ASCII characters in `TEXT()` literals: build them from code points (e.g. `TCHAR(0x00B0)`), not source text.

