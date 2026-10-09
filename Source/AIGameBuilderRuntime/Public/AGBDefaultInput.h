#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

#include "AGBDefaultInput.generated.h"

class UInputAction;
class UInputMappingContext;

/** The Enhanced Input assets the player character uses. */
USTRUCT(BlueprintType)
struct AIGAMEBUILDERRUNTIME_API FAGBInputSet
{
	GENERATED_BODY()

	/** Key bindings for all actions below. If empty, the character builds the default bindings at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> MappingContext;

	/** Axis2D: X = right, Y = forward. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Move;

	/** Axis2D: X = yaw, Y = pitch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Look;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Jump;

	/** Hold to sprint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Sprint;

	/** Press to toggle crouching. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Crouch;

	/** Use what the player is looking at. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Interact;

	/** Switch between third and first person. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ToggleCamera;
};

namespace AGBInput
{
	/** Creates an object of a class under an asset name: transient at runtime, a saved asset in the editor. */
	using FObjectFactory = TFunctionRef<UObject*(UClass* Class, const FString& AssetName)>;

	/**
	 * Builds the default survival-style bindings (keyboard + mouse and gamepad):
	 * WASD / left stick move, mouse / right stick look, Space / A jump, Shift / L3 sprint, C / B crouch,
	 * E / X interact, V / View camera.
	 */
	AIGAMEBUILDERRUNTIME_API FAGBInputSet CreateDefaultInput(FObjectFactory Factory);
}
