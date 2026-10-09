#include "AGBInteractableLight.h"

#include "AGBInteractableComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "AIGameBuilder"

AAGBInteractableLight::AAGBInteractableLight()
{
	bReplicates = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	// Basic shapes are 100 cm and centred on their pivot. The root sits at the base of the 2.2 m post.
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	Post = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Post"));
	Post->SetupAttachment(GetRootComponent());
	Post->SetRelativeLocation(FVector(0.0, 0.0, 110.0));
	Post->SetRelativeScale3D(FVector(0.12, 0.12, 2.2));
	if (Cylinder.Succeeded())
	{
		Post->SetStaticMesh(Cylinder.Object);
	}

	Lamp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lamp"));
	Lamp->SetupAttachment(GetRootComponent());
	Lamp->SetRelativeLocation(FVector(0.0, 0.0, 235.0));
	Lamp->SetRelativeScale3D(FVector(0.35));
	if (Sphere.Succeeded())
	{
		Lamp->SetStaticMesh(Sphere.Object);
	}

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(GetRootComponent());
	Light->SetRelativeLocation(FVector(0.0, 0.0, 235.0));
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(40.f);
	Light->SetAttenuationRadius(1500.f);
	Light->SetLightColor(FLinearColor(1.f, 0.78f, 0.5f));

	Interactable = CreateDefaultSubobject<UAGBInteractableComponent>(TEXT("Interactable"));
}

void AAGBInteractableLight::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyState();
}

void AAGBInteractableLight::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Interactable->OnInteracted.AddDynamic(this, &AAGBInteractableLight::HandleInteracted);
	}
	ApplyState();
}

void AAGBInteractableLight::SetOn(bool bNewOn)
{
	bIsOn = bNewOn;
	ApplyState(); // OnRep doesn't run on the server.
}

void AAGBInteractableLight::HandleInteracted(APawn* Interactor)
{
	SetOn(!bIsOn);
}

void AAGBInteractableLight::OnRep_IsOn()
{
	ApplyState();
}

void AAGBInteractableLight::ApplyState()
{
	Light->SetVisibility(bIsOn);
	Interactable->PromptText = bIsOn ? LOCTEXT("TurnOff", "Turn off light") : LOCTEXT("TurnOn", "Turn on light");
}

void AAGBInteractableLight::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAGBInteractableLight, bIsOn);
}

#undef LOCTEXT_NAMESPACE
