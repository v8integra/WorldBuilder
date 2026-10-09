#include "AGBClimate.h"

#include "AGBSurvivalConfig.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

float UAGBHeatSourceComponent::GetWarmthAt(const FVector& Location) const
{
	if (!bActive)
	{
		return 0.f;
	}
	const double Distance = FVector::Dist(GetComponentLocation(), Location);
	return Distance >= Radius ? 0.f : WarmthC * static_cast<float>(1.0 - Distance / Radius);
}

void UAGBHeatSourceComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UAGBClimateSubsystem* Climate = GetWorld() ? GetWorld()->GetSubsystem<UAGBClimateSubsystem>() : nullptr)
	{
		Climate->RegisterHeatSource(this);
	}
}

void UAGBHeatSourceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UAGBClimateSubsystem* Climate = GetWorld() ? GetWorld()->GetSubsystem<UAGBClimateSubsystem>() : nullptr)
	{
		Climate->UnregisterHeatSource(this);
	}
	Super::EndPlay(EndPlayReason);
}

float UAGBClimateSubsystem::GetTemperatureAt(FVector Location, const UAGBSurvivalConfig* Config) const
{
	const UAGBSurvivalConfig* Rules = Config ? Config : GetDefault<UAGBSurvivalConfig>();
	const double AltitudeM = Location.Z / 100.0 - Rules->SeaLevelM;
	float Temperature = Rules->BaseTemperatureC - static_cast<float>(FMath::Max(0.0, AltitudeM) / 100.0) * Rules->CoolingPer100m + WorldOffsetC;

	float Warmth = 0.f;
	for (const TWeakObjectPtr<UAGBHeatSourceComponent>& Source : HeatSources)
	{
		if (const UAGBHeatSourceComponent* Heat = Source.Get())
		{
			Warmth += Heat->GetWarmthAt(Location);
		}
	}
	return Temperature + Warmth;
}

AAGBHeatSource::AAGBHeatSource()
{
	bReplicates = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(GetRootComponent());
	Mesh->SetRelativeLocation(FVector(0.0, 0.0, 25.0));
	Mesh->SetRelativeScale3D(FVector(0.6, 0.6, 0.5));
	if (Cone.Succeeded())
	{
		Mesh->SetStaticMesh(Cone.Object);
	}

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(GetRootComponent());
	Light->SetRelativeLocation(FVector(0.0, 0.0, 80.0));
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(60.f);
	Light->SetAttenuationRadius(1200.f);
	Light->SetLightColor(FLinearColor(1.f, 0.55f, 0.2f));

	Heat = CreateDefaultSubobject<UAGBHeatSourceComponent>(TEXT("Heat"));
	Heat->SetupAttachment(GetRootComponent());
}
