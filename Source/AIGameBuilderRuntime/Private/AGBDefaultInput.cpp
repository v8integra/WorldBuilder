#include "AGBDefaultInput.h"

#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace AGBInput
{
	FAGBInputSet CreateDefaultInput(FObjectFactory Factory)
	{
		FAGBInputSet Set;

		auto MakeAction = [&Factory](const TCHAR* Name, EInputActionValueType Type)
		{
			UInputAction* Action = CastChecked<UInputAction>(Factory(UInputAction::StaticClass(), Name));
			Action->ValueType = Type;
			return Action;
		};
		Set.Move = MakeAction(TEXT("IA_AGB_Move"), EInputActionValueType::Axis2D);
		Set.Look = MakeAction(TEXT("IA_AGB_Look"), EInputActionValueType::Axis2D);
		Set.Jump = MakeAction(TEXT("IA_AGB_Jump"), EInputActionValueType::Boolean);
		Set.Sprint = MakeAction(TEXT("IA_AGB_Sprint"), EInputActionValueType::Boolean);
		Set.Crouch = MakeAction(TEXT("IA_AGB_Crouch"), EInputActionValueType::Boolean);
		Set.Interact = MakeAction(TEXT("IA_AGB_Interact"), EInputActionValueType::Boolean);
		Set.ToggleCamera = MakeAction(TEXT("IA_AGB_ToggleCamera"), EInputActionValueType::Boolean);

		UInputMappingContext* Context = CastChecked<UInputMappingContext>(Factory(UInputMappingContext::StaticClass(), TEXT("IMC_AGB_Default")));
		Context->UnmapAll();
		Set.MappingContext = Context;

		// Modifiers are owned by the mapping context so they are saved with it.
		auto Negate = [Context](bool bX, bool bY)
		{
			UInputModifierNegate* Modifier = NewObject<UInputModifierNegate>(Context);
			Modifier->bX = bX;
			Modifier->bY = bY;
			Modifier->bZ = false;
			return Modifier;
		};
		auto SwapXY = [Context]() { return NewObject<UInputModifierSwizzleAxis>(Context); }; // Default order is YXZ.
		auto DeadZone = [Context]() { return NewObject<UInputModifierDeadZone>(Context); };

		// Move: Y = forward, X = right.
		Context->MapKey(Set.Move, EKeys::W).Modifiers.Add(SwapXY());
		{
			FEnhancedActionKeyMapping& Back = Context->MapKey(Set.Move, EKeys::S);
			Back.Modifiers.Add(SwapXY());
			Back.Modifiers.Add(Negate(true, true));
		}
		Context->MapKey(Set.Move, EKeys::A).Modifiers.Add(Negate(true, false));
		Context->MapKey(Set.Move, EKeys::D);
		Context->MapKey(Set.Move, EKeys::Gamepad_Left2D).Modifiers.Add(DeadZone());

		// Look: mouse deltas as-is (Y inverted to pitch up), gamepad scaled to degrees per second.
		Context->MapKey(Set.Look, EKeys::Mouse2D).Modifiers.Add(Negate(false, true));
		{
			FEnhancedActionKeyMapping& Stick = Context->MapKey(Set.Look, EKeys::Gamepad_Right2D);
			Stick.Modifiers.Add(DeadZone());
			Stick.Modifiers.Add(Negate(false, true));
			Stick.Modifiers.Add(NewObject<UInputModifierScaleByDeltaTime>(Context));
			UInputModifierScalar* Speed = NewObject<UInputModifierScalar>(Context);
			Speed->Scalar = FVector(60.0, 45.0, 1.0);
			Stick.Modifiers.Add(Speed);
		}

		Context->MapKey(Set.Jump, EKeys::SpaceBar);
		Context->MapKey(Set.Jump, EKeys::Gamepad_FaceButton_Bottom);
		Context->MapKey(Set.Sprint, EKeys::LeftShift);
		Context->MapKey(Set.Sprint, EKeys::Gamepad_LeftThumbstick);
		Context->MapKey(Set.Crouch, EKeys::C);
		Context->MapKey(Set.Crouch, EKeys::Gamepad_FaceButton_Right);
		Context->MapKey(Set.Interact, EKeys::E);
		Context->MapKey(Set.Interact, EKeys::Gamepad_FaceButton_Left);
		Context->MapKey(Set.ToggleCamera, EKeys::V);
		Context->MapKey(Set.ToggleCamera, EKeys::Gamepad_Special_Left);
		return Set;
	}
}
