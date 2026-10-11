#include "CreatureTools.h"

#include "GameToolUtils.h"

#include "AGBCreature.h"
#include "AGBCreatureSpawnZone.h"
#include "AGBCreatureTypes.h"
#include "AGBItemTypes.h"
#include "AIWorldBuilderCore.h"
#include "AIWorldBuilderLandscape.h"
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/OverlapResult.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Landscape.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "LandscapeInfo.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "AIGameBuilderCreatures"

using namespace AIWorldBuilder;

namespace
{
	const FName WaterCollisionProfile = TEXT("WaterBodyCollision");

	FCreatureResult Fail(const FString& Message)
	{
		FCreatureResult R;
		R.Message = Message;
		return R;
	}

	UAGBCreatureDefinition* FindCreature(const FString& CreatureId)
	{
		return Cast<UAGBCreatureDefinition>(GameToolUtils::FindAssetByTag(UAGBCreatureDefinition::StaticClass(),
			GET_MEMBER_NAME_CHECKED(UAGBCreatureDefinition, CreatureId), CreatureId));
	}

	FString KnownCreatures()
	{
		return GameToolUtils::ListIds(UAGBCreatureDefinition::StaticClass(), GET_MEMBER_NAME_CHECKED(UAGBCreatureDefinition, CreatureId));
	}

	FString DescribeCreature(const UAGBCreatureDefinition* Creature)
	{
		auto Anim = [](const TSoftObjectPtr<UAnimSequenceBase>& Ptr) { return Ptr.IsNull() ? FString(TEXT("-")) : Ptr.GetAssetName(); };
		return FString::Printf(TEXT("%s: %s (%s, health %.0f, damage %.0f, walk %.1f / run %.1f m/s, mesh %s, anims idle %s walk %s run %s attack %s death %s, loot %d kinds)"),
			*Creature->CreatureId.ToString(), *Creature->GetDisplayNameOrId().ToString(),
			*StaticEnum<EAGBCreatureBehaviour>()->GetNameStringByValue(static_cast<int64>(Creature->Behaviour)), Creature->MaxHealth, Creature->Damage,
			Creature->WalkSpeed / CmPerMeter, Creature->RunSpeed / CmPerMeter, Creature->Mesh.IsNull() ? TEXT("placeholder") : *Creature->Mesh.GetAssetName(),
			*Anim(Creature->IdleAnim), *Anim(Creature->WalkAnim), *Anim(Creature->RunAnim), *Anim(Creature->AttackAnim), *Anim(Creature->DeathAnim),
			Creature->Loot.Num());
	}

	void FillLists(FCreatureResult& R)
	{
		for (const FAssetData& Asset : GameToolUtils::GetAssetsOfClass(UAGBCreatureDefinition::StaticClass()))
		{
			if (const UAGBCreatureDefinition* Creature = Cast<UAGBCreatureDefinition>(Asset.GetAsset()))
			{
				R.Creatures.Add(DescribeCreature(Creature));
			}
		}
		R.Creatures.Sort();
		if (UWorld* World = GetEditorWorld())
		{
			for (TActorIterator<AAGBCreatureSpawnZone> It(World); It; ++It)
			{
				R.SpawnZones.Add(FString::Printf(TEXT("%s: %s, up to %d alive, %d spawn points, at (%.0f, %.0f) m, respawn %.1f min"), *It->GetActorLabel(),
					It->Creature ? *It->Creature->CreatureId.ToString() : TEXT("?"), It->MaxAlive, It->SpawnPoints.Num(),
					It->GetActorLocation().X / CmPerMeter, It->GetActorLocation().Y / CmPerMeter, It->RespawnSeconds / 60.f));
			}
		}
	}

	/** Best animation of a skeleton for a role, by name keywords (earlier keyword and shorter name win). */
	UAnimSequence* FindAnimation(const TArray<FAssetData>& Candidates, std::initializer_list<const TCHAR*> Keywords)
	{
		static const TCHAR* Penalised[] = { TEXT("left"), TEXT("right"), TEXT("turn"), TEXT("start"), TEXT("stop"), TEXT("end"), TEXT("additive"), TEXT("pose"), TEXT("_to_") };
		const FAssetData* Best = nullptr;
		int32 BestScore = MAX_int32;
		int32 KeywordIndex = 0;
		for (const TCHAR* Keyword : Keywords)
		{
			for (const FAssetData& Asset : Candidates)
			{
				const FString Name = Asset.AssetName.ToString().ToLower();
				if (!Name.Contains(Keyword))
				{
					continue;
				}
				int32 Score = KeywordIndex * 1000 + Name.Len();
				for (const TCHAR* Bad : Penalised)
				{
					Score += Name.Contains(Bad) ? 200 : 0;
				}
				if (Score < BestScore)
				{
					BestScore = Score;
					Best = &Asset;
				}
			}
			++KeywordIndex;
		}
		return Best ? Cast<UAnimSequence>(Best->GetAsset()) : nullptr;
	}
}

FString UCreatureTools::GetToolsetVersion() const
{
	return GetPluginVersion();
}

FCreatureResult UCreatureTools::CreateCreature(const FString& CreatureId, const FString& DisplayName, const FString& Behaviour, const FString& MeshPath,
	const FString& AnimationFolder, double MaxHealth, double Damage, double WalkSpeedMs, double RunSpeedMs, double RadiusCm, double HalfHeightCm,
	double AggroRangeM, double FleeRangeM, double WanderRadiusM, const TArray<FGameResourceYieldArg>& Loot, const TArray<FString>& ButcherToolTags,
	double MeshYawDeg, double MeshScale, const FString& Folder)
{
	const FString Id = CreatureId.TrimStartAndEnd().ToLower();
	if (Id.IsEmpty())
	{
		return Fail(TEXT("creatureId is empty."));
	}
	EAGBCreatureBehaviour BehaviourValue;
	if (Behaviour.Equals(TEXT("Passive"), ESearchCase::IgnoreCase)) { BehaviourValue = EAGBCreatureBehaviour::Passive; }
	else if (Behaviour.Equals(TEXT("Skittish"), ESearchCase::IgnoreCase)) { BehaviourValue = EAGBCreatureBehaviour::Skittish; }
	else if (Behaviour.Equals(TEXT("Aggressive"), ESearchCase::IgnoreCase)) { BehaviourValue = EAGBCreatureBehaviour::Aggressive; }
	else
	{
		return Fail(FString::Printf(TEXT("Unknown behaviour '%s': use Passive, Skittish or Aggressive."), *Behaviour));
	}
	USkeletalMesh* Mesh = nullptr;
	const bool bSetMesh = !MeshPath.Equals(TEXT("auto"), ESearchCase::IgnoreCase) && !MeshPath.IsEmpty();
	if (bSetMesh && !MeshPath.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
		if (!Mesh)
		{
			return Fail(FString::Printf(TEXT("Skeletal mesh '%s' not found."), *MeshPath));
		}
	}
	TArray<FAGBResourceYield> LootList;
	for (const FGameResourceYieldArg& Arg : Loot)
	{
		UAGBItemDefinition* Item = GameToolUtils::FindItem(Arg.ItemId);
		if (!Item)
		{
			return Fail(FString::Printf(TEXT("No loot item '%s' (create it with ItemTools.CreateItem)."), *Arg.ItemId));
		}
		FAGBResourceYield& Yield = LootList.AddDefaulted_GetRef();
		Yield.Item = Item;
		Yield.MinCount = FMath::Max(0, Arg.MinCount);
		Yield.MaxCount = FMath::Max(Yield.MinCount, Arg.MaxCount);
		Yield.Chance = FMath::Clamp(Arg.Chance, 0.f, 1.f);
	}

	UAGBCreatureDefinition* Creature = FindCreature(Id);
	const bool bCreated = Creature == nullptr;
	if (bCreated)
	{
		FString Root = Folder;
		Root.RemoveFromEnd(TEXT("/"));
		const FString AssetName = TEXT("DA_Creature_") + Id;
		if (!Root.StartsWith(TEXT("/Game")) || FPackageName::DoesPackageExist(Root / AssetName))
		{
			return Fail(FString::Printf(TEXT("Cannot create %s (folder must be under /Game and the name unused)."), *(Root / AssetName)));
		}
		UPackage* Package = CreatePackage(*(Root / AssetName));
		Creature = NewObject<UAGBCreatureDefinition>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Creature);
	}
	Creature->Modify();
	Creature->CreatureId = FName(*Id);
	Creature->DisplayName = FText::FromString(DisplayName.TrimStartAndEnd());
	Creature->Behaviour = BehaviourValue;
	if (bSetMesh)
	{
		Creature->Mesh = Mesh;
	}
	Creature->MaxHealth = static_cast<float>(FMath::Max(1.0, MaxHealth));
	Creature->Damage = static_cast<float>(FMath::Max(0.0, Damage));
	Creature->WalkSpeed = static_cast<float>(FMath::Max(0.1, WalkSpeedMs) * CmPerMeter);
	Creature->RunSpeed = static_cast<float>(FMath::Max(WalkSpeedMs, RunSpeedMs) * CmPerMeter);
	Creature->CapsuleRadius = static_cast<float>(FMath::Max(10.0, RadiusCm));
	Creature->CapsuleHalfHeight = static_cast<float>(FMath::Max(RadiusCm, HalfHeightCm));
	Creature->AggroRange = static_cast<float>(FMath::Max(0.0, AggroRangeM) * CmPerMeter);
	Creature->FleeRange = static_cast<float>(FMath::Max(0.0, FleeRangeM) * CmPerMeter);
	Creature->WanderRadius = static_cast<float>(FMath::Max(1.0, WanderRadiusM) * CmPerMeter);
	Creature->MeshYawDeg = static_cast<float>(MeshYawDeg);
	Creature->MeshScale = static_cast<float>(FMath::Max(0.01, MeshScale));
	Creature->Loot = LootList;
	Creature->ButcherToolTags.Reset();
	for (const FString& Tag : ButcherToolTags)
	{
		if (!Tag.TrimStartAndEnd().IsEmpty())
		{
			Creature->ButcherToolTags.Add(FName(*Tag.TrimStartAndEnd()));
		}
	}

	// Animations of the same skeleton, found by name.
	FString AnimationNote;
	USkeletalMesh* Body = Creature->Mesh.LoadSynchronous();
	if (Body && Body->GetSkeleton() && !AnimationFolder.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		FARFilter Filter;
		Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
		Filter.bRecursivePaths = true;
		Filter.PackagePaths.Add(AnimationFolder.Equals(TEXT("auto"), ESearchCase::IgnoreCase) ? FName(TEXT("/Game")) : FName(*AnimationFolder));
		TArray<FAssetData> All;
		Registry.GetAssets(Filter, All);
		const FString SkeletonPath = Body->GetSkeleton()->GetPathName();
		TArray<FAssetData> Matching = All.FilterByPredicate([&SkeletonPath](const FAssetData& Asset)
		{
			FString Skeleton;
			return Asset.GetTagValue(TEXT("Skeleton"), Skeleton) && Skeleton.Contains(SkeletonPath);
		});
		Creature->IdleAnim = FindAnimation(Matching, { TEXT("idle"), TEXT("stand"), TEXT("breath") });
		Creature->WalkAnim = FindAnimation(Matching, { TEXT("walk") });
		Creature->RunAnim = FindAnimation(Matching, { TEXT("run"), TEXT("gallop"), TEXT("trot"), TEXT("sprint"), TEXT("jog") });
		Creature->AttackAnim = FindAnimation(Matching, { TEXT("attack"), TEXT("bite"), TEXT("claw"), TEXT("swipe") });
		Creature->DeathAnim = FindAnimation(Matching, { TEXT("death"), TEXT("die"), TEXT("dead") });
		AnimationNote = FString::Printf(TEXT(" %d animation(s) share its skeleton."), Matching.Num());
	}
	Creature->MarkPackageDirty();
	UEditorLoadingAndSavingUtils::SavePackages({ Creature->GetPackage() }, /*bOnlyDirty=*/false);

	FCreatureResult R;
	FillLists(R);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("%s creature: %s.%s"), bCreated ? TEXT("Created") : TEXT("Updated"), *DescribeCreature(Creature), *AnimationNote);
	return R;
}

FCreatureResult UCreatureTools::ListCreatures()
{
	FCreatureResult R;
	FillLists(R);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("%d creature kind(s), %d spawn zone(s) in the level."), R.Creatures.Num(), R.SpawnZones.Num());
	return R;
}

FCreatureResult UCreatureTools::CreateSpawnZone(const FString& CreatureId, double CenterXM, double CenterYM, double RadiusM, int32 MaxAlive,
	double MinHeightM, double MaxHeightM, double MaxSlopeDeg, const FString& PaintLayer, double MinLayerWeight, double RespawnMinutes, const FString& Label)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Fail(TEXT("No level is open in the editor."));
	}
	UAGBCreatureDefinition* Creature = FindCreature(CreatureId);
	if (!Creature)
	{
		return Fail(FString::Printf(TEXT("No creature '%s'. Known: %s."), *CreatureId, *KnownCreatures()));
	}
	const FVector2D CenterCm = FVector2D(CenterXM, CenterYM) * CmPerMeter;
	FString Error;
	ALandscape* Landscape = LandscapeUtils::Resolve(World, TEXT("auto"), &CenterCm, Error);
	ULandscapeInfo* Info = Landscape ? Landscape->GetLandscapeInfo() : nullptr;
	ULandscapeLayerInfoObject* Layer = nullptr;
	if (!PaintLayer.Equals(TEXT("none"), ESearchCase::IgnoreCase) && !PaintLayer.IsEmpty())
	{
		Layer = Landscape ? GameToolUtils::FindLayerInfo(Landscape, PaintLayer) : nullptr;
		if (!Layer || !Info)
		{
			return Fail(FString::Printf(TEXT("No paint layer '%s' on the landscape here (LandscapePaintTools.ListPaintLayers)."), *PaintLayer));
		}
	}

	// Sample a grid over the circle; keep ground that fits the biome rules.
	const double RadiusCm = FMath::Max(5.0, RadiusM) * CmPerMeter;
	const double Step = FMath::Max(300.0, RadiusCm / 8.0);
	int32 Tested = 0, OffLandscape = 0, WrongHeight = 0, TooSteep = 0, WrongLayer = 0, InWater = 0;
	TArray<FVector> Points;
	for (double DX = -RadiusCm; DX <= RadiusCm; DX += Step)
	{
		for (double DY = -RadiusCm; DY <= RadiusCm; DY += Step)
		{
			if (FMath::Square(DX) + FMath::Square(DY) > FMath::Square(RadiusCm))
			{
				continue;
			}
			++Tested;
			FHitResult Hit;
			const FVector2D Point = CenterCm + FVector2D(DX, DY);
			if (!GameToolUtils::TraceGround(World, Point.X / CmPerMeter, Point.Y / CmPerMeter, Hit) || !Cast<ULandscapeHeightfieldCollisionComponent>(Hit.GetComponent()))
			{
				++OffLandscape; // No ground, or under a tree/rock/building.
				continue;
			}
			const double HeightM = Hit.ImpactPoint.Z / CmPerMeter;
			if (HeightM < MinHeightM || HeightM > MaxHeightM)
			{
				++WrongHeight;
				continue;
			}
			if (FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Hit.ImpactNormal.Z, -1.0, 1.0))) > MaxSlopeDeg)
			{
				++TooSteep;
				continue;
			}
			if (Layer && GameToolUtils::LayerWeightAt(Landscape, Info, Layer, Hit.ImpactPoint) < MinLayerWeight)
			{
				++WrongLayer;
				continue;
			}
			TArray<FOverlapResult> Overlaps;
			World->OverlapMultiByChannel(Overlaps, Hit.ImpactPoint + FVector(0, 0, 80), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(60.f));
			if (Overlaps.ContainsByPredicate([](const FOverlapResult& Overlap) { return Overlap.GetComponent() && Overlap.GetComponent()->GetCollisionProfileName() == WaterCollisionProfile; }))
			{
				++InWater;
				continue;
			}
			Points.Add(Hit.ImpactPoint);
		}
	}
	if (Points.Num() < 3)
	{
		return Fail(FString::Printf(TEXT("Only %d of %d sampled spots fit (%d off the landscape or under objects, %d wrong height, %d too steep, %d not on '%s', %d in water). Widen the rules or move the zone."),
			Points.Num(), Tested, OffLandscape, WrongHeight, TooSteep, WrongLayer, *PaintLayer, InWater));
	}
	// Keep a random spread of at most 64.
	for (int32 Index = Points.Num() - 1; Index > 0; --Index)
	{
		Points.Swap(Index, FMath::RandRange(0, Index));
	}
	Points.SetNum(FMath::Min(Points.Num(), 64));

	const FScopedTransaction Transaction(LOCTEXT("CreateSpawnZone", "AI Game Builder: Create Spawn Zone"));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAGBCreatureSpawnZone* Zone = World->SpawnActor<AAGBCreatureSpawnZone>(AAGBCreatureSpawnZone::StaticClass(),
		FTransform(FVector(CenterCm.X, CenterCm.Y, Points[0].Z)), Params);
	if (!Zone)
	{
		return Fail(TEXT("Could not spawn the zone."));
	}
	Zone->Creature = Creature;
	Zone->MaxAlive = FMath::Max(0, MaxAlive);
	Zone->SpawnPoints = Points;
	Zone->RespawnSeconds = static_cast<float>(FMath::Max(0.0, RespawnMinutes) * 60.0);
	Zone->ActivationRadius = static_cast<float>(FMath::Max(25000.0, RadiusCm + 15000.0));
	Zone->SetActorLabel(GameToolUtils::UniqueLabel(World, Label, TEXT("SpawnZone_") + Creature->CreatureId.ToString()));

	FCreatureResult R;
	FillLists(R);
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("Placed '%s': up to %d %s, %d spawn points (of %d sampled; %d off landscape/under objects, %d wrong height, %d too steep, %d wrong layer, %d in water). Save the level."),
		*Zone->GetActorLabel(), Zone->MaxAlive, *Creature->GetDisplayNameOrId().ToString(), Points.Num(), Tested, OffLandscape, WrongHeight, TooSteep, WrongLayer, InWater);
	return R;
}

FCreatureResult UCreatureTools::SpawnCreature(const FString& CreatureId, double XM, double YM)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Fail(TEXT("No level is open in the editor."));
	}
	UAGBCreatureDefinition* Creature = FindCreature(CreatureId);
	if (!Creature)
	{
		return Fail(FString::Printf(TEXT("No creature '%s'. Known: %s."), *CreatureId, *KnownCreatures()));
	}
	FHitResult Hit;
	if (!GameToolUtils::TraceGround(World, XM, YM, Hit))
	{
		return Fail(FString::Printf(TEXT("No ground at (%.1f, %.1f) m."), XM, YM));
	}
	const FScopedTransaction Transaction(LOCTEXT("SpawnCreature", "AI Game Builder: Spawn Creature"));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Params.bDeferConstruction = true;
	const FTransform Transform(FRotator::ZeroRotator, Hit.ImpactPoint + FVector(0, 0, FMath::Max(Creature->CapsuleHalfHeight, Creature->CapsuleRadius) + 10.0));
	AAGBCreature* Spawned = World->SpawnActor<AAGBCreature>(AAGBCreature::StaticClass(), Transform, Params);
	if (!Spawned)
	{
		return Fail(TEXT("Could not spawn the creature."));
	}
	Spawned->Definition = Creature;
	Spawned->FinishSpawning(Transform);
	Spawned->SetActorLabel(GameToolUtils::UniqueLabel(World, TEXT("auto"), TEXT("Creature_") + Creature->CreatureId.ToString()));

	FCreatureResult R;
	R.bSuccess = true;
	R.Message = FString::Printf(TEXT("Placed '%s' at (%.1f, %.1f) m. It comes alive in Play."), *Spawned->GetActorLabel(), XM, YM);
	return R;
}

#undef LOCTEXT_NAMESPACE
