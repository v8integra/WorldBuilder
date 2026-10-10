#include "HarvestTools.h"

#include "GameToolUtils.h"

#include "AGBHarvestTypes.h"
#include "AGBItemTypes.h"
#include "AGBResourceNode.h"
#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "Algo/AnyOf.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/CollisionProfile.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "InstancedFoliageActor.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/BodySetup.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "AIGameBuilderHarvest"

// Exported by UnrealEd (GeomFitUtils.h is private): refreshes nav collision and the physics state of components using the mesh.
UNREALED_API void RefreshCollisionChange(UStaticMesh& StaticMesh);

using namespace AIWorldBuilder;

namespace
{
	FHarvestResult Fail(const FString& Message)
	{
		FHarvestResult R;
		R.Message = Message;
		return R;
	}

	bool MeshHasCollision(const UStaticMesh* Mesh)
	{
		const UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
		return Body && (Body->AggGeom.GetElementCount() > 0 || Body->GetCollisionTraceFlag() == CTF_UseComplexAsSimple);
	}

	FString DescribeYield(const FAGBResourceYield& Yield)
	{
		const FString Id = Yield.Item ? Yield.Item->ItemId.ToString() : FString(TEXT("?"));
		FString Text = Yield.MinCount == Yield.MaxCount ? FString::Printf(TEXT("%s %d"), *Id, Yield.MinCount) : FString::Printf(TEXT("%s %d-%d"), *Id, Yield.MinCount, Yield.MaxCount);
		if (Yield.Chance < 1.f)
		{
			Text += FString::Printf(TEXT(" (%.0f%%)"), Yield.Chance * 100.f);
		}
		return Text;
	}

	FGameResourceInfo Describe(const UAGBResourceDefinition* Resource)
	{
		FGameResourceInfo Info;
		Info.ResourceId = Resource->ResourceId.ToString();
		Info.DisplayName = Resource->DisplayName.ToString();
		Info.AssetPath = Resource->GetPathName();
		for (const TSoftObjectPtr<UStaticMesh>& Mesh : Resource->Meshes)
		{
			Info.Meshes.Add(Mesh.ToSoftObjectPath().ToString());
		}
		for (const FName& Tag : Resource->ToolTags)
		{
			Info.ToolTags.Add(Tag.ToString());
		}
		Info.bAllowHands = Resource->bAllowHands;
		Info.Health = Resource->Health;
		for (const FAGBResourceYield& Yield : Resource->YieldPerHit)
		{
			Info.YieldPerHit.Add(DescribeYield(Yield));
		}
		for (const FAGBResourceYield& Yield : Resource->YieldWhenDepleted)
		{
			Info.YieldWhenDepleted.Add(DescribeYield(Yield));
		}
		Info.RegrowMinutes = Resource->RegrowMinutes;
		Info.bFallWhenDepleted = Resource->bFallWhenDepleted;
		return Info;
	}

	TArray<UAGBResourceDefinition*> LoadAllResources()
	{
		TArray<UAGBResourceDefinition*> Result;
		for (const FAssetData& Asset : GameToolUtils::GetAssetsOfClass(UAGBResourceDefinition::StaticClass()))
		{
			if (UAGBResourceDefinition* Resource = Cast<UAGBResourceDefinition>(Asset.GetAsset()))
			{
				Result.Add(Resource);
			}
		}
		return Result;
	}

	/** Mesh path -> resource id, for all resources. */
	TMap<FString, FString> MeshOwners()
	{
		TMap<FString, FString> Owners;
		for (const UAGBResourceDefinition* Resource : LoadAllResources())
		{
			for (const TSoftObjectPtr<UStaticMesh>& Mesh : Resource->Meshes)
			{
				Owners.Add(Mesh.ToSoftObjectPath().ToString(), Resource->ResourceId.ToString());
			}
		}
		return Owners;
	}

	FString SourceOf(const UInstancedStaticMeshComponent* Component)
	{
		const AActor* Owner = Component->GetOwner();
		if (Owner && Owner->IsA<AInstancedFoliageActor>())
		{
			return TEXT("Foliage");
		}
		if ((Owner && Owner->GetClass()->GetName().Contains(TEXT("PCG"))) || Component->ComponentHasTag(TEXT("PCG Generated Component")))
		{
			return TEXT("PCG");
		}
		return TEXT("Instanced");
	}

	bool IsCollisionOn(const UPrimitiveComponent* Component)
	{
		return Component->IsQueryCollisionEnabled() && Component->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block;
	}

	bool IsHittable(const UPrimitiveComponent* Component, const UStaticMesh* Mesh)
	{
		return MeshHasCollision(Mesh) && IsCollisionOn(Component);
	}

	/** Collision settings for placed copies: Solid blocks everything; HarvestOnly is hit by traces but lets players through. */
	void ApplyCollisionMode(FBodyInstance& Body, bool bSolid)
	{
		if (bSolid)
		{
			Body.SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			return;
		}
		Body.SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
		Body.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Body.SetResponseToAllChannels(ECR_Ignore);
		Body.SetResponseToChannel(ECC_Visibility, ECR_Block);
	}

	/** Replaces a mesh's simple collision with one shape fitted to its bounds. Returns false for an unknown shape. */
	bool BuildSimpleCollision(UStaticMesh* Mesh, const FString& Shape)
	{
		if (!Mesh->GetBodySetup())
		{
			Mesh->CreateBodySetup();
		}
		UBodySetup* Body = Mesh->GetBodySetup();
		const FBox Bounds = Mesh->GetBoundingBox();
		const FVector Center = Bounds.GetCenter();
		const FVector Extent = Bounds.GetExtent();
		const double Height = Extent.Z * 2.0;

		FKAggregateGeom Geometry;
		if (Shape.Equals(TEXT("Box"), ESearchCase::IgnoreCase))
		{
			FKBoxElem Box(Extent.X * 2.0, Extent.Y * 2.0, Height);
			Box.Center = Center;
			Geometry.BoxElems.Add(Box);
		}
		else if (Shape.Equals(TEXT("Sphere"), ESearchCase::IgnoreCase))
		{
			FKSphereElem Sphere(Extent.GetMax());
			Sphere.Center = Center;
			Geometry.SphereElems.Add(Sphere);
		}
		else if (Shape.Equals(TEXT("Capsule"), ESearchCase::IgnoreCase) || Shape.Equals(TEXT("Trunk"), ESearchCase::IgnoreCase))
		{
			const bool bTrunk = Shape.Equals(TEXT("Trunk"), ESearchCase::IgnoreCase);
			// Trunk: a thin capsule at the pivot (trees are modelled with the trunk base at the origin).
			const double Radius = bTrunk ? FMath::Clamp(FMath::Max(Extent.X, Extent.Y) * 0.06, 8.0, 60.0) : FMath::Max(Extent.X, Extent.Y);
			FKSphylElem Capsule(static_cast<float>(Radius), static_cast<float>(FMath::Max(0.0, Height - Radius * 2.0)));
			Capsule.Center = bTrunk ? FVector(0.0, 0.0, Bounds.Min.Z + Extent.Z) : Center;
			Geometry.SphylElems.Add(Capsule);
		}
		else
		{
			return false;
		}

		Body->Modify();
		Body->RemoveSimpleCollision();
		Body->AddCollisionFrom(Geometry);
		Body->InvalidatePhysicsData();
		Body->CreatePhysicsMeshes();
		RefreshCollisionChange(*Mesh);
		Mesh->MarkPackageDirty();
		return true;
	}

	/** All meshes placed in the level, grouped by mesh and source. */
	TArray<FGameWorldMeshInfo> GatherWorldMeshes(UWorld* World)
	{
		const TMap<FString, FString> Owners = MeshOwners();
		TMap<FString, FGameWorldMeshInfo> ByKey;
		auto Add = [&](const UStaticMesh* Mesh, const FString& Source, int32 Count, const UPrimitiveComponent* Component)
		{
			const FString Path = Mesh->GetPathName();
			FGameWorldMeshInfo& Info = ByKey.FindOrAdd(Path + TEXT("|") + Source);
			Info.MeshPath = Path;
			Info.Source = Source;
			Info.Count += Count;
			Info.bHittable |= IsHittable(Component, Mesh);
			Info.bMeshHasCollision = MeshHasCollision(Mesh);
			Info.bCollisionEnabled |= IsCollisionOn(Component);
			if (const FString* Owner = Owners.Find(Path))
			{
				Info.ResourceId = *Owner;
			}
		};
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (const AAGBResourceNode* Node = Cast<AAGBResourceNode>(*It))
			{
				if (const UStaticMesh* Mesh = Node->Mesh->GetStaticMesh())
				{
					Add(Mesh, TEXT("ResourceNode"), 1, Node->Mesh);
				}
				continue;
			}
			TArray<UInstancedStaticMeshComponent*> Components;
			It->GetComponents<UInstancedStaticMeshComponent>(Components);
			for (const UInstancedStaticMeshComponent* Component : Components)
			{
				if (const UStaticMesh* Mesh = Component->GetStaticMesh(); Mesh && Component->GetInstanceCount() > 0)
				{
					Add(Mesh, SourceOf(Component), Component->GetInstanceCount(), Component);
				}
			}
		}
		TArray<FGameWorldMeshInfo> Result;
		ByKey.GenerateValueArray(Result);
		Result.Sort([](const FGameWorldMeshInfo& A, const FGameWorldMeshInfo& B) { return A.Count > B.Count; });
		return Result;
	}

	bool ConvertYields(const TArray<FGameResourceYieldArg>& Args, TArray<FAGBResourceYield>& Out, FString& OutError)
	{
		Out.Reset();
		for (const FGameResourceYieldArg& Arg : Args)
		{
			UAGBItemDefinition* Item = GameToolUtils::FindItem(Arg.ItemId);
			if (!Item)
			{
				OutError = FString::Printf(TEXT("No item with id '%s' (create it with ItemTools.CreateItem). Known items: %s."), *Arg.ItemId,
					*GameToolUtils::ListIds(UAGBItemDefinition::StaticClass(), GET_MEMBER_NAME_CHECKED(UAGBItemDefinition, ItemId)));
				return false;
			}
			FAGBResourceYield& Yield = Out.AddDefaulted_GetRef();
			Yield.Item = Item;
			Yield.MinCount = FMath::Max(0, Arg.MinCount);
			Yield.MaxCount = FMath::Max(Yield.MinCount, Arg.MaxCount);
			Yield.Chance = FMath::Clamp(Arg.Chance, 0.f, 1.f);
		}
		return true;
	}
}

FString UHarvestTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FHarvestResult UHarvestTools::ListWorldMeshes()
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Fail(TEXT("No level is open in the editor."));
	}
	FHarvestResult R;
	R.WorldMeshes = GatherWorldMeshes(World);
	int32 NotHittable = 0;
	for (const FGameWorldMeshInfo& Info : R.WorldMeshes)
	{
		NotHittable += Info.bHittable ? 0 : 1;
	}
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("%d placed mesh type(s) in the loaded part of the level.%s"), R.WorldMeshes.Num(),
		NotHittable > 0 ? *FString::Printf(TEXT(" %d cannot be hit (see bMeshHasCollision / bCollisionEnabled): fix with SetupHarvestCollision."), NotHittable) : TEXT(""));
	return R;
}

FHarvestResult UHarvestTools::SetupHarvestCollision(const TArray<FString>& MeshPaths, const FString& Shape, const FString& Mode)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Fail(TEXT("No level is open in the editor."));
	}
	const bool bSolid = Mode.Equals(TEXT("Solid"), ESearchCase::IgnoreCase);
	if (!bSolid && !Mode.Equals(TEXT("HarvestOnly"), ESearchCase::IgnoreCase))
	{
		return Fail(FString::Printf(TEXT("Unknown mode '%s': use Solid or HarvestOnly."), *Mode));
	}
	const bool bAuto = Shape.Equals(TEXT("auto"), ESearchCase::IgnoreCase);
	const bool bKeep = Shape.Equals(TEXT("Keep"), ESearchCase::IgnoreCase);
	static const TCHAR* Shapes[] = { TEXT("Trunk"), TEXT("Box"), TEXT("Capsule"), TEXT("Sphere") };
	if (!bAuto && !bKeep && !Algo::AnyOf(Shapes, [&Shape](const TCHAR* Known) { return Shape.Equals(Known, ESearchCase::IgnoreCase); }))
	{
		return Fail(FString::Printf(TEXT("Unknown shape '%s': use auto, Trunk, Box, Capsule, Sphere or Keep."), *Shape));
	}

	TArray<UStaticMesh*> Meshes;
	for (const FString& Path : MeshPaths)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
		if (!Mesh)
		{
			return Fail(FString::Printf(TEXT("Static mesh '%s' not found."), *Path));
		}
		Meshes.AddUnique(Mesh);
	}
	if (Meshes.Num() == 0)
	{
		return Fail(TEXT("meshPaths is empty."));
	}

	const FScopedTransaction Transaction(LOCTEXT("SetupHarvestCollision", "AI Game Builder: Setup Harvest Collision"));
	TArray<UPackage*> ToSave;
	TArray<FString> Lines;
	for (UStaticMesh* Mesh : Meshes)
	{
		// 1. The mesh's own collision shape.
		FString ShapeNote = TEXT("kept its collision");
		const FString UseShape = bAuto ? (MeshHasCollision(Mesh) ? FString() : FString(TEXT("Box"))) : (bKeep ? FString() : Shape);
		if (!UseShape.IsEmpty())
		{
			Mesh->Modify();
			BuildSimpleCollision(Mesh, UseShape);
			ToSave.AddUnique(Mesh->GetPackage());
			ShapeNote = FString::Printf(TEXT("new %s collision"), *UseShape);
		}
		else if (!MeshHasCollision(Mesh))
		{
			ShapeNote = TEXT("still NO collision (shape Keep)");
		}

		// 2. Collision on every placed copy: foliage types (and their components), other instanced components.
		int32 FoliageTypes = 0;
		int32 Components = 0;
		for (TActorIterator<AInstancedFoliageActor> It(World); It; ++It)
		{
			AInstancedFoliageActor* Foliage = *It;
			TArray<UFoliageType*> Changed;
			Foliage->ForEachFoliageInfo([Mesh, &Changed](UFoliageType* Type, FFoliageInfo& Info)
			{
				const UFoliageType_InstancedStaticMesh* MeshType = Cast<UFoliageType_InstancedStaticMesh>(Type);
				if (MeshType && MeshType->GetStaticMesh() == Mesh)
				{
					Changed.Add(Type);
				}
				return true;
			});
			for (UFoliageType* Type : Changed)
			{
				Type->Modify();
				ApplyCollisionMode(Type->BodyInstance, bSolid);
				if (Type->IsAsset())
				{
					ToSave.AddUnique(Type->GetPackage());
				}
				Foliage->Modify();
				Foliage->NotifyFoliageTypeChanged(Type, /*bSourceChanged=*/false);
				++FoliageTypes;
			}
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->IsA<AInstancedFoliageActor>())
			{
				continue;
			}
			TArray<UInstancedStaticMeshComponent*> Instanced;
			It->GetComponents<UInstancedStaticMeshComponent>(Instanced);
			for (UInstancedStaticMeshComponent* Component : Instanced)
			{
				if (Component->GetStaticMesh() == Mesh)
				{
					Component->Modify();
					ApplyCollisionMode(Component->BodyInstance, bSolid);
					Component->RecreatePhysicsState();
					++Components;
				}
			}
		}
		Lines.Add(FString::Printf(TEXT("%s: %s; %s on %d foliage type(s) and %d other instanced component(s)."),
			*Mesh->GetName(), *ShapeNote, bSolid ? TEXT("solid") : TEXT("harvest-only"), FoliageTypes, Components));
	}
	if (ToSave.Num() > 0)
	{
		UEditorLoadingAndSavingUtils::SavePackages(ToSave, /*bOnlyDirty=*/false);
	}

	FHarvestResult R;
	for (const FGameWorldMeshInfo& Info : GatherWorldMeshes(World))
	{
		if (Meshes.ContainsByPredicate([&Info](const UStaticMesh* Mesh) { return Mesh->GetPathName() == Info.MeshPath; }))
		{
			R.WorldMeshes.Add(Info);
		}
	}
	R.bSuccess = true;
	R.Message = FString::Join(Lines, TEXT(" ")) + TEXT(" Save the level to keep the foliage changes.");
	return R;
}

FHarvestResult UHarvestTools::CreateResource(const FString& ResourceId, const FString& DisplayName, const TArray<FString>& MeshPaths, const TArray<FString>& ToolTags,
	bool bAllowHands, double Health, const TArray<FGameResourceYieldArg>& YieldPerHit, const TArray<FGameResourceYieldArg>& YieldWhenDepleted,
	double RegrowMinutes, bool bFallWhenDepleted, const FString& ToolHint, const FString& Folder)
{
	// Validate everything before touching assets.
	const FString Id = ResourceId.TrimStartAndEnd().ToLower();
	if (Id.IsEmpty())
	{
		return Fail(TEXT("resourceId is empty."));
	}
	for (const TCHAR Char : Id)
	{
		if (!FChar::IsAlnum(Char) && Char != TEXT('_'))
		{
			return Fail(FString::Printf(TEXT("resourceId '%s' may only contain letters, digits and underscores."), *ResourceId));
		}
	}
	if (MeshPaths.Num() == 0)
	{
		return Fail(TEXT("meshPaths is empty: list the static meshes this resource covers (see ListWorldMeshes)."));
	}
	if (Health <= 0.0)
	{
		return Fail(TEXT("health must be above 0."));
	}

	const TMap<FString, FString> Owners = MeshOwners();
	TArray<UStaticMesh*> Meshes;
	TArray<FString> Warnings;
	for (const FString& Path : MeshPaths)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
		if (!Mesh)
		{
			return Fail(LoadObject<USkeletalMesh>(nullptr, *Path)
				? FString::Printf(TEXT("'%s' is a skeletal mesh: placed foliage uses the converted static mesh (MeshConversionTools)."), *Path)
				: FString::Printf(TEXT("Static mesh '%s' not found."), *Path));
		}
		const FString* Owner = Owners.Find(Mesh->GetPathName());
		if (Owner && *Owner != Id)
		{
			return Fail(FString::Printf(TEXT("%s already belongs to resource '%s'. A mesh can belong to one resource only."), *Mesh->GetName(), **Owner));
		}
		if (!MeshHasCollision(Mesh))
		{
			Warnings.Add(FString::Printf(TEXT("%s has no simple collision: players cannot hit it (run SetupHarvestCollision)."), *Mesh->GetName()));
		}
		Meshes.Add(Mesh);
	}
	TArray<FAGBResourceYield> PerHit;
	TArray<FAGBResourceYield> WhenDepleted;
	FString Error;
	if (!ConvertYields(YieldPerHit, PerHit, Error) || !ConvertYields(YieldWhenDepleted, WhenDepleted, Error))
	{
		return Fail(Error);
	}

	FString Root = Folder;
	Root.RemoveFromEnd(TEXT("/"));
	if (!Root.StartsWith(TEXT("/Game")))
	{
		return Fail(FString::Printf(TEXT("Folder must be under /Game (got '%s')."), *Folder));
	}

	UAGBResourceDefinition* Resource = GameToolUtils::FindResource(Id);
	const bool bCreated = Resource == nullptr;
	if (bCreated)
	{
		const FString AssetName = TEXT("DA_Resource_") + Id;
		const FString PackageName = Root / AssetName;
		if (FPackageName::DoesPackageExist(PackageName) || FindObject<UObject>(nullptr, *(PackageName + TEXT(".") + AssetName)))
		{
			return Fail(FString::Printf(TEXT("%s already exists but is not a resource with id '%s'."), *PackageName, *Id));
		}
		UPackage* Package = CreatePackage(*PackageName);
		Resource = NewObject<UAGBResourceDefinition>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Resource);
	}

	Resource->Modify();
	Resource->ResourceId = FName(*Id);
	Resource->DisplayName = FText::FromString(DisplayName.TrimStartAndEnd());
	Resource->Meshes.Reset();
	for (UStaticMesh* Mesh : Meshes)
	{
		Resource->Meshes.Add(Mesh);
	}
	Resource->ToolTags.Reset();
	for (const FString& Tag : ToolTags)
	{
		if (!Tag.TrimStartAndEnd().IsEmpty())
		{
			Resource->ToolTags.AddUnique(FName(*Tag.TrimStartAndEnd()));
		}
	}
	Resource->bAllowHands = bAllowHands;
	Resource->Health = static_cast<float>(Health);
	Resource->YieldPerHit = PerHit;
	Resource->YieldWhenDepleted = WhenDepleted;
	Resource->RegrowMinutes = static_cast<float>(FMath::Max(0.0, RegrowMinutes));
	Resource->bFallWhenDepleted = bFallWhenDepleted;
	Resource->ToolHint = ToolHint.Equals(TEXT("auto"), ESearchCase::IgnoreCase) ? FText::GetEmpty() : FText::FromString(ToolHint);
	Resource->MarkPackageDirty();
	UEditorLoadingAndSavingUtils::SavePackages({ Resource->GetPackage() }, /*bOnlyDirty=*/false);

	// How much of the level it covers.
	int32 Placed = 0;
	if (UWorld* World = GetEditorWorld())
	{
		for (const FGameWorldMeshInfo& Info : GatherWorldMeshes(World))
		{
			Placed += Info.ResourceId == Id ? Info.Count : 0;
		}
	}

	FHarvestResult R;
	R.bSuccess = true;
	R.Resources.Add(Describe(Resource));
	R.Message = FString::Printf(TEXT("%s resource '%s' (%s): %d placed instance(s) in the loaded level are now harvestable.%s%s"),
		bCreated ? TEXT("Created") : TEXT("Updated"), *Id, *Resource->GetPathName(), Placed,
		Warnings.Num() > 0 ? TEXT(" ") : TEXT(""), *FString::Join(Warnings, TEXT(" ")));
	return R;
}

FHarvestResult UHarvestTools::ListResources()
{
	FHarvestResult R;
	for (const UAGBResourceDefinition* Resource : LoadAllResources())
	{
		R.Resources.Add(Describe(Resource));
	}
	R.Resources.Sort([](const FGameResourceInfo& A, const FGameResourceInfo& B) { return A.ResourceId < B.ResourceId; });
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("%d resource(s)."), R.Resources.Num());
	return R;
}

FHarvestResult UHarvestTools::SpawnResourceNode(const FString& ResourceId, double XM, double YM, int32 MeshIndex, double YawDeg, double Scale)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Fail(TEXT("No level is open in the editor."));
	}
	UAGBResourceDefinition* Resource = GameToolUtils::FindResource(ResourceId);
	if (!Resource)
	{
		return Fail(FString::Printf(TEXT("No resource '%s'. Known: %s."), *ResourceId,
			*GameToolUtils::ListIds(UAGBResourceDefinition::StaticClass(), GET_MEMBER_NAME_CHECKED(UAGBResourceDefinition, ResourceId))));
	}
	if (!Resource->Meshes.IsValidIndex(MeshIndex))
	{
		return Fail(FString::Printf(TEXT("meshIndex %d is out of range: '%s' has %d mesh(es)."), MeshIndex, *ResourceId, Resource->Meshes.Num()));
	}
	FHitResult Hit;
	if (!GameToolUtils::TraceGround(World, XM, YM, Hit))
	{
		return Fail(FString::Printf(TEXT("No ground at (%.1f, %.1f) m."), XM, YM));
	}

	const FScopedTransaction Transaction(LOCTEXT("SpawnResourceNode", "AI Game Builder: Spawn Resource Node"));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	const FTransform Transform(FRotator(0.0, YawDeg, 0.0), Hit.ImpactPoint, FVector(FMath::Max(0.01, Scale)));
	AAGBResourceNode* Node = World->SpawnActor<AAGBResourceNode>(AAGBResourceNode::StaticClass(), Transform, Params);
	if (!Node)
	{
		return Fail(TEXT("Could not spawn the resource node."));
	}
	Node->Resource = Resource;
	Node->MeshIndex = MeshIndex;
	Node->FinishSpawning(Transform);
	Node->SetActorLabel(GameToolUtils::UniqueLabel(World, TEXT("auto"), TEXT("Resource_") + Resource->ResourceId.ToString()));

	FHarvestResult R;
	R.bSuccess = true;
	R.Resources.Add(Describe(Resource));
	R.Message = FString::Printf(TEXT("Placed '%s' at (%.1f, %.1f, %.1f) m."), *Node->GetActorLabel(), XM, YM, Hit.ImpactPoint.Z / CmPerMeter);
	return R;
}

#undef LOCTEXT_NAMESPACE
