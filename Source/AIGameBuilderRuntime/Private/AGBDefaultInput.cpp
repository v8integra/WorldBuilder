#include "AGBDefaultInput.h"

#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace AGBInput
{
	namespace
	{
		using FMapFunction = void (*)(UInputMappingContext*, UInputAction*);

		struct FActionSpec
		{
			TObjectPtr<UInputAction> FAGBInputSet::* Member;
			const TCHAR* AssetName;
			EInputActionValueType ValueType;
			FMapFunction Map;
		};

		// Modifiers are owned by the mapping context so they are saved with it.
		UInputModifierNegate* Negate(UInputMappingContext* Context, bool bX, bool bY)
		{
			UInputModifierNegate* Modifier = NewObject<UInputModifierNegate>(Context);
			Modifier->bX = bX;
			Modifier->bY = bY;
			Modifier->bZ = false;
			return Modifier;
		}

		UInputModifierScalar* Scale(UInputMappingContext* Context, double X, double Y = 1.0)
		{
			UInputModifierScalar* Modifier = NewObject<UInputModifierScalar>(Context);
			Modifier->Scalar = FVector(X, Y, 1.0);
			return Modifier;
		}

		UInputModifier* SwapXY(UInputMappingContext* Context) { return NewObject<UInputModifierSwizzleAxis>(Context); } // Default order is YXZ.
		UInputModifier* DeadZone(UInputMappingContext* Context) { return NewObject<UInputModifierDeadZone>(Context); }

		void MapMove(UInputMappingContext* C, UInputAction* A)
		{
			// Y = forward, X = right.
			C->MapKey(A, EKeys::W).Modifiers.Add(SwapXY(C));
			FEnhancedActionKeyMapping& Back = C->MapKey(A, EKeys::S);
			Back.Modifiers.Add(SwapXY(C));
			Back.Modifiers.Add(Negate(C, true, true));
			C->MapKey(A, EKeys::A).Modifiers.Add(Negate(C, true, false));
			C->MapKey(A, EKeys::D);
			C->MapKey(A, EKeys::Gamepad_Left2D).Modifiers.Add(DeadZone(C));
		}

		void MapLook(UInputMappingContext* C, UInputAction* A)
		{
			// Mouse deltas as-is (Y inverted to pitch up); gamepad scaled to degrees per second.
			C->MapKey(A, EKeys::Mouse2D).Modifiers.Add(Negate(C, false, true));
			FEnhancedActionKeyMapping& Stick = C->MapKey(A, EKeys::Gamepad_Right2D);
			Stick.Modifiers.Add(DeadZone(C));
			Stick.Modifiers.Add(Negate(C, false, true));
			Stick.Modifiers.Add(NewObject<UInputModifierScaleByDeltaTime>(C));
			Stick.Modifiers.Add(Scale(C, 60.0, 45.0));
		}

		void MapJump(UInputMappingContext* C, UInputAction* A) { C->MapKey(A, EKeys::SpaceBar); C->MapKey(A, EKeys::Gamepad_FaceButton_Bottom); }
		void MapSprint(UInputMappingContext* C, UInputAction* A) { C->MapKey(A, EKeys::LeftShift); C->MapKey(A, EKeys::Gamepad_LeftThumbstick); }
		void MapCrouch(UInputMappingContext* C, UInputAction* A) { C->MapKey(A, EKeys::C); C->MapKey(A, EKeys::Gamepad_FaceButton_Right); }
		void MapInteract(UInputMappingContext* C, UInputAction* A) { C->MapKey(A, EKeys::E); C->MapKey(A, EKeys::Gamepad_FaceButton_Left); }
		void MapToggleCamera(UInputMappingContext* C, UInputAction* A) { C->MapKey(A, EKeys::V); C->MapKey(A, EKeys::Gamepad_Special_Left); }
		void MapInventory(UInputMappingContext* C, UInputAction* A) { C->MapKey(A, EKeys::Tab); C->MapKey(A, EKeys::Gamepad_FaceButton_Top); }
		void MapDrop(UInputMappingContext* C, UInputAction* A) { C->MapKey(A, EKeys::Q); C->MapKey(A, EKeys::Gamepad_DPad_Down); }
		void MapUseItem(UInputMappingContext* C, UInputAction* A) { C->MapKey(A, EKeys::LeftMouseButton); C->MapKey(A, EKeys::Gamepad_RightTrigger); }

		void MapHotbarSelect(UInputMappingContext* C, UInputAction* A)
		{
			const FKey Keys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
				EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero };
			for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Keys)); ++Index)
			{
				C->MapKey(A, Keys[Index]).Modifiers.Add(Scale(C, Index + 1.0));
			}
		}

		void MapHotbarCycle(UInputMappingContext* C, UInputAction* A)
		{
			C->MapKey(A, EKeys::MouseWheelAxis).Modifiers.Add(Negate(C, true, false)); // Wheel down = next slot.
			C->MapKey(A, EKeys::Gamepad_DPad_Right);
			C->MapKey(A, EKeys::Gamepad_DPad_Left).Modifiers.Add(Negate(C, true, false));
		}

		const FActionSpec Specs[] = {
			{ &FAGBInputSet::Move, TEXT("IA_AGB_Move"), EInputActionValueType::Axis2D, &MapMove },
			{ &FAGBInputSet::Look, TEXT("IA_AGB_Look"), EInputActionValueType::Axis2D, &MapLook },
			{ &FAGBInputSet::Jump, TEXT("IA_AGB_Jump"), EInputActionValueType::Boolean, &MapJump },
			{ &FAGBInputSet::Sprint, TEXT("IA_AGB_Sprint"), EInputActionValueType::Boolean, &MapSprint },
			{ &FAGBInputSet::Crouch, TEXT("IA_AGB_Crouch"), EInputActionValueType::Boolean, &MapCrouch },
			{ &FAGBInputSet::Interact, TEXT("IA_AGB_Interact"), EInputActionValueType::Boolean, &MapInteract },
			{ &FAGBInputSet::ToggleCamera, TEXT("IA_AGB_ToggleCamera"), EInputActionValueType::Boolean, &MapToggleCamera },
			{ &FAGBInputSet::Inventory, TEXT("IA_AGB_Inventory"), EInputActionValueType::Boolean, &MapInventory },
			{ &FAGBInputSet::Drop, TEXT("IA_AGB_Drop"), EInputActionValueType::Boolean, &MapDrop },
			{ &FAGBInputSet::HotbarSelect, TEXT("IA_AGB_HotbarSelect"), EInputActionValueType::Axis1D, &MapHotbarSelect },
			{ &FAGBInputSet::HotbarCycle, TEXT("IA_AGB_HotbarCycle"), EInputActionValueType::Axis1D, &MapHotbarCycle },
			{ &FAGBInputSet::UseItem, TEXT("IA_AGB_UseItem"), EInputActionValueType::Boolean, &MapUseItem },
		};
	}

	void ForEachAction(FAGBInputSet& Set, TFunctionRef<void(TObjectPtr<UInputAction>& Action, const TCHAR* AssetName)> Visit)
	{
		for (const FActionSpec& Spec : Specs)
		{
			Visit(Set.*Spec.Member, Spec.AssetName);
		}
	}

	FAGBInputSet CreateDefaultInput(FObjectFactory Factory, const FAGBInputSet* Existing)
	{
		FAGBInputSet Set = Existing ? *Existing : FAGBInputSet();
		UInputMappingContext* Context = Set.MappingContext;
		if (!Context)
		{
			Context = CastChecked<UInputMappingContext>(Factory(UInputMappingContext::StaticClass(), TEXT("IMC_AGB_Default")));
			Context->UnmapAll();
			Set.MappingContext = Context;
		}

		for (const FActionSpec& Spec : Specs)
		{
			TObjectPtr<UInputAction>& Action = Set.*Spec.Member;
			if (Action)
			{
				continue;
			}
			Action = CastChecked<UInputAction>(Factory(UInputAction::StaticClass(), Spec.AssetName));
			Action->ValueType = Spec.ValueType;
			Context->UnmapAllKeysFromAction(Action);
			Spec.Map(Context, Action);
		}
		return Set;
	}

	bool IsComplete(const FAGBInputSet& Set)
	{
		if (!Set.MappingContext)
		{
			return false;
		}
		for (const FActionSpec& Spec : Specs)
		{
			if (!(Set.*Spec.Member))
			{
				return false;
			}
		}
		return true;
	}
}
