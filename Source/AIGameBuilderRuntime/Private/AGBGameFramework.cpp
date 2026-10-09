#include "AGBGameFramework.h"

#include "AGBCharacter.h"
#include "AGBInteractionComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "InputMappingContext.h"

AAGBGameMode::AAGBGameMode()
{
	DefaultPawnClass = AAGBCharacter::StaticClass();
	PlayerControllerClass = AAGBPlayerController::StaticClass();
	HUDClass = AAGBHUD::StaticClass();
}

FString AAGBHUD::GetInteractKeyName() const
{
	const AAGBCharacter* Character = Cast<AAGBCharacter>(GetOwningPawn());
	if (Character && Character->Input.MappingContext && Character->Input.Interact)
	{
		for (const FEnhancedActionKeyMapping& Mapping : Character->Input.MappingContext->GetMappings())
		{
			if (Mapping.Action == Character->Input.Interact && !Mapping.Key.IsGamepadKey())
			{
				return Mapping.Key.GetDisplayName().ToString();
			}
		}
	}
	return TEXT("E");
}

void AAGBHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}

	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	if (bShowCrosshair)
	{
		const float Size = 4.f;
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.8f), CenterX - Size * 0.5f, CenterY - Size * 0.5f, Size, Size);
	}

	const APawn* Pawn = GetOwningPawn();
	const UAGBInteractionComponent* Interaction = Pawn ? Pawn->FindComponentByClass<UAGBInteractionComponent>() : nullptr;
	if (Interaction && Interaction->GetFocusedActor())
	{
		const FString Prompt = FString::Printf(TEXT("[%s] %s"), *GetInteractKeyName(), *Interaction->GetFocusedPrompt().ToString());
		UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
		const float Scale = FMath::Max(1.f, Canvas->ClipY / 1080.f);
		float Width = 0.f, Height = 0.f;
		GetTextSize(Prompt, Width, Height, Font, Scale);
		const float X = CenterX - Width * 0.5f;
		const float Y = CenterY + 40.f * Scale;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), X - 8.f, Y - 4.f, Width + 16.f, Height + 8.f);
		DrawText(Prompt, TextColor, X, Y, Font, Scale);
	}
}
