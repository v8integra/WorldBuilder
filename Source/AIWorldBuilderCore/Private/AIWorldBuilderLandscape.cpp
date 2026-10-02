#include "AIWorldBuilderLandscape.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "LandscapeInfo.h"

namespace AIWorldBuilder
{
	namespace LandscapeMath
	{
		double HeightValueToLocalZ(uint16 Value)
		{
			return LandscapeDataAccess::GetLocalHeight(Value);
		}

		uint16 LocalZToHeightValue(double LocalZ)
		{
			return LandscapeDataAccess::GetTexHeight(static_cast<float>(LocalZ));
		}

		double HeightValueToWorldZCm(uint16 Value, double ScaleZ, double ActorZCm)
		{
			return HeightValueToLocalZ(Value) * ScaleZ + ActorZCm;
		}

		uint16 WorldZCmToHeightValue(double WorldZCm, double ScaleZ, double ActorZCm)
		{
			if (FMath::IsNearlyZero(ScaleZ))
			{
				return static_cast<uint16>(LandscapeDataAccess::MidValue);
			}
			return LocalZToHeightValue((WorldZCm - ActorZCm) / ScaleZ);
		}

		void GetHeightRangeCm(double ScaleZ, double ActorZCm, double& OutMinCm, double& OutMaxCm)
		{
			const double A = HeightValueToWorldZCm(0, ScaleZ, ActorZCm);
			const double B = HeightValueToWorldZCm(LandscapeDataAccess::MaxValue, ScaleZ, ActorZCm);
			OutMinCm = FMath::Min(A, B);
			OutMaxCm = FMath::Max(A, B);
		}

		double SlopeDegrees(double DzDxCm, double DzDyCm)
		{
			return FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(DzDxCm * DzDxCm + DzDyCm * DzDyCm)));
		}
	}

	namespace LandscapeUtils
	{
		TArray<ALandscape*> GetAllLandscapes(UWorld* World)
		{
			TArray<ALandscape*> Result;
			if (World)
			{
				for (TActorIterator<ALandscape> It(World); It; ++It)
				{
					Result.Add(*It);
				}
			}
			return Result;
		}

		FString GetDisplayName(const ALandscape* Landscape)
		{
			return Landscape ? Landscape->GetActorLabel() : FString();
		}

		FBox GetCompleteBounds(const ALandscape* Landscape)
		{
			if (!Landscape)
			{
				return FBox(ForceInit);
			}
			if (const ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
			{
				// In World Partition, GetCompleteBounds only sees *saved* proxies (actor descriptors), so a landscape
				// created or extended since the last save would be missing. Union with what is loaded.
				FBox Bounds = Info->GetCompleteBounds();
				Bounds += Info->GetLoadedBounds();
				return Bounds;
			}
			return Landscape->GetCompleteBounds();
		}

		FIntRect GetCompleteExtent(const ALandscape* Landscape)
		{
			FIntRect Extent(MAX_int32, MAX_int32, MIN_int32, MIN_int32);
			if (const ULandscapeInfo* Info = Landscape ? Landscape->GetLandscapeInfo() : nullptr)
			{
				// Same World Partition caveat as GetCompleteBounds: include loaded (possibly unsaved) components.
				Extent = Info->GetCompleteLandscapeExtent();
				FIntRect Loaded;
				if (Info->GetLandscapeExtent(Loaded))
				{
					Extent.Union(Loaded);
				}
			}
			return Extent;
		}

		double GetSampleSpacingCm(const ALandscape* Landscape)
		{
			return Landscape ? Landscape->GetActorScale3D().X : 100.0;
		}

		ALandscape* Resolve(UWorld* World, const FString& Name, const FVector2D* PointCm, FString& OutError)
		{
			const TArray<ALandscape*> All = GetAllLandscapes(World);
			if (All.IsEmpty())
			{
				OutError = TEXT("The open level has no landscape. Create one first (Landscape mode, or a new Open World level).");
				return nullptr;
			}

			auto ListNames = [&All]()
			{
				TArray<FString> Names;
				for (const ALandscape* L : All)
				{
					Names.Add(FString::Printf(TEXT("'%s'"), *GetDisplayName(L)));
				}
				return FString::Join(Names, TEXT(", "));
			};

			const bool bAutoSelect = Name.IsEmpty() || Name.Equals(AutoLandscapeName, ESearchCase::IgnoreCase);
			if (!bAutoSelect)
			{
				for (ALandscape* L : All)
				{
					if (GetDisplayName(L).Equals(Name, ESearchCase::IgnoreCase) || L->GetName().Equals(Name, ESearchCase::IgnoreCase))
					{
						return L;
					}
				}
				OutError = FString::Printf(TEXT("No landscape named '%s'. Available: %s."), *Name, *ListNames());
				return nullptr;
			}

			if (PointCm)
			{
				for (ALandscape* L : All)
				{
					const FBox Bounds = GetCompleteBounds(L);
					if (Bounds.IsValid
						&& PointCm->X >= Bounds.Min.X && PointCm->X <= Bounds.Max.X
						&& PointCm->Y >= Bounds.Min.Y && PointCm->Y <= Bounds.Max.Y)
					{
						return L;
					}
				}
				OutError = FString::Printf(TEXT("Point (%.1f m, %.1f m) is outside every landscape. Use ListLandscapes to see their bounds."),
					CmToMeters(PointCm->X), CmToMeters(PointCm->Y));
				return nullptr;
			}

			if (All.Num() == 1)
			{
				return All[0];
			}
			OutError = FString::Printf(TEXT("The level has %d landscapes; pass a landscape name. Available: %s."), All.Num(), *ListNames());
			return nullptr;
		}

		TOptional<double> SampleHeightCm(const ALandscape* Landscape, const FVector2D& PointCm)
		{
			if (!Landscape)
			{
				return {};
			}
			auto SampleAt = [Landscape](const FVector2D& P) -> TOptional<float>
			{
				const FVector Location(P.X, P.Y, 0.0);
				TOptional<float> Height = Landscape->GetHeightAtLocation(Location, EHeightfieldSource::Editor);
				if (!Height.IsSet())
				{
					Height = Landscape->GetHeightAtLocation(Location, EHeightfieldSource::Complex);
				}
				return Height;
			};

			TOptional<float> Height = SampleAt(PointCm);
			if (!Height.IsSet())
			{
				// GetHeightAtLocation picks the component by flooring, so points exactly on the landscape's
				// far (+X/+Y) edge land on a component that does not exist. Retry just inside that edge.
				constexpr double EdgeNudgeCm = 0.5;
				for (const FVector2D& Nudge : { FVector2D(EdgeNudgeCm, 0.0), FVector2D(0.0, EdgeNudgeCm), FVector2D(EdgeNudgeCm, EdgeNudgeCm) })
				{
					Height = SampleAt(PointCm - Nudge);
					if (Height.IsSet())
					{
						break;
					}
				}
			}
			return Height.IsSet() ? TOptional<double>(Height.GetValue()) : TOptional<double>();
		}
	}
}
