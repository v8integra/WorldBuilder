#include "AGBCameraTour.h"

#include "AGBWorldEnvironment.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"

AAGBCameraTour::AAGBCameraTour()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(RootComponent);
	Camera->bConstrainAspectRatio = false;
}

void AAGBCameraTour::Play(APlayerController* Player)
{
	if (!Player || Points.Num() < 2)
	{
		return;
	}
	Viewer = Player;
	Elapsed = 0.f;
	Camera->SetFieldOfView(FieldOfView);
	Camera->SetWorldTransform(Points[0]);
	if (Hour >= 0.f && HasAuthority())
	{
		if (AAGBWorldEnvironment* Environment = AAGBWorldEnvironment::Get(GetWorld()))
		{
			Environment->SetHour(Hour);
		}
	}
	if (Player->GetHUD())
	{
		Player->GetHUD()->bShowHUD = false;
	}
	Player->SetViewTargetWithBlend(this, 0.f);
	SetActorTickEnabled(true);
}

void AAGBCameraTour::Stop()
{
	if (APlayerController* Player = Viewer.Get())
	{
		if (Player->GetHUD())
		{
			Player->GetHUD()->bShowHUD = true;
		}
		Player->SetViewTargetWithBlend(Player->GetPawn() ? static_cast<AActor*>(Player->GetPawn()) : Player, 0.f);
	}
	Viewer.Reset();
	SetActorTickEnabled(false);
}

void AAGBCameraTour::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Viewer.IsValid() || Points.Num() < 2)
	{
		Stop();
		return;
	}
	Elapsed += FApp::GetDeltaTime(); // Real time: unaffected by game speed.
	const float Position = Elapsed / FMath::Max(1.f, SecondsPerShot);
	const int32 Segment = FMath::FloorToInt(Position);
	if (Segment >= Points.Num() - 1)
	{
		Stop();
		return;
	}
	// Catmull-Rom through the points for smooth motion; eased rotation between neighbours.
	const float Alpha = Position - Segment;
	auto At = [this](int32 Index) { return Points[FMath::Clamp(Index, 0, Points.Num() - 1)].GetLocation(); };
	const FVector P0 = At(Segment - 1), P1 = At(Segment), P2 = At(Segment + 1), P3 = At(Segment + 2);
	const float T2 = Alpha * Alpha, T3 = T2 * Alpha;
	const FVector Location = 0.5f * ((2.f * P1) + (-P0 + P2) * Alpha + (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * T2 + (-P0 + 3.f * P1 - 3.f * P2 + P3) * T3);
	const FQuat Rotation = FQuat::Slerp(Points[Segment].GetRotation(), Points[Segment + 1].GetRotation(), FMath::SmoothStep(0.f, 1.f, Alpha));
	Camera->SetWorldLocationAndRotation(Location, Rotation);
}
