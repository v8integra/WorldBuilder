#include "AGBGameFramework.h"

#include "AGBCharacter.h"
#include "AGBCraftingComponent.h"
#include "AGBCraftingStation.h"
#include "AGBCraftingTypes.h"
#include "AGBInteractionComponent.h"
#include "AGBInventoryComponent.h"
#include "AGBSurvivalConfig.h"
#include "AGBVitalsComponent.h"
#include "TimerManager.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "InputMappingContext.h"

namespace
{
	// Hit box names: "AGB_<inventory>_<slot>", inventory 0 = backpack, 1 = hotbar, 2 = equipment.
	constexpr int32 BackpackIndex = 0;
	constexpr int32 HotbarIndex = 1;
	constexpr int32 EquipmentIndex = 2;
	constexpr int32 InventoryColumns = 6;

	const FLinearColor SlotColor(0.f, 0.f, 0.f, 0.55f);
	const FLinearColor SelectedColor(1.f, 0.8f, 0.2f, 1.f);
	const FLinearColor HeldColor(0.3f, 0.8f, 1.f, 1.f);
	const FLinearColor LabelColor(1.f, 1.f, 1.f, 0.45f);
	const FLinearColor GoodColor(0.55f, 0.9f, 0.45f);
	const FLinearColor BadColor(1.f, 0.45f, 0.35f);
	constexpr float CraftingPanelWidth = 440.f;
	constexpr TCHAR DegreeSign = 0x00B0;

	UAGBInventoryComponent* InventoryByIndex(const AAGBCharacter* Character, int32 Index)
	{
		switch (Index)
		{
		case BackpackIndex: return Character->Inventory;
		case HotbarIndex: return Character->Hotbar;
		case EquipmentIndex: return Character->Equipment;
		default: return nullptr;
		}
	}

	FString ShortName(const UAGBItemDefinition* Item)
	{
		const FString Name = Item->GetDisplayNameOrId().ToString();
		return Name.Len() > 9 ? Name.Left(8) + TEXT(".") : Name;
	}

	FString EquipSlotName(EAGBEquipSlot Slot)
	{
		return StaticEnum<EAGBEquipSlot>()->GetDisplayNameTextByValue(static_cast<int64>(Slot)).ToString();
	}
}

AAGBGameMode::AAGBGameMode()
{
	DefaultPawnClass = AAGBCharacter::StaticClass();
	PlayerControllerClass = AAGBPlayerController::StaticClass();
	HUDClass = AAGBHUD::StaticClass();
}

void AAGBGameMode::ScheduleRespawn(AController* Controller, float DelaySeconds)
{
	if (!Controller)
	{
		return;
	}
	TWeakObjectPtr<AController> WeakController = Controller;
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, WeakController]()
	{
		AController* Player = WeakController.Get();
		if (Player && !Player->GetPawn())
		{
			RestartPlayer(Player);
		}
	}), FMath::Max(0.1f, DelaySeconds), false);
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

// ---------------------------------------------------------------- inventory screen state

void AAGBHUD::ToggleInventory()
{
	APlayerController* PlayerController = GetOwningPlayerController();
	if (!PlayerController)
	{
		return;
	}
	bInventoryOpen = !bInventoryOpen;
	CurrentStation.Reset();
	Held = FSlotRef();
	Hovered = FSlotRef();

	PlayerController->SetShowMouseCursor(bInventoryOpen);
	PlayerController->bEnableClickEvents = bInventoryOpen;
	PlayerController->bEnableMouseOverEvents = bInventoryOpen;
	if (bInventoryOpen)
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PlayerController->SetInputMode(Mode);
		PlayerController->SetIgnoreLookInput(true);
		if (Canvas)
		{
			PlayerController->SetMouseLocation(FMath::RoundToInt(Canvas->ClipX * 0.5f), FMath::RoundToInt(Canvas->ClipY * 0.5f));
		}
	}
	else
	{
		PlayerController->SetInputMode(FInputModeGameOnly());
		PlayerController->ResetIgnoreLookInput();
	}
}

AAGBHUD::FSlotRef AAGBHUD::ParseHitBox(FName BoxName) const
{
	FSlotRef Ref;
	const AAGBCharacter* Character = Cast<AAGBCharacter>(GetOwningPawn());
	TArray<FString> Parts;
	if (!Character || BoxName.ToString().ParseIntoArray(Parts, TEXT("_")) != 3 || Parts[0] != TEXT("AGB"))
	{
		return Ref;
	}
	Ref.Inventory = InventoryByIndex(Character, FCString::Atoi(*Parts[1]));
	Ref.Slot = FCString::Atoi(*Parts[2]);
	return Ref;
}

void AAGBHUD::OpenStation(AAGBCraftingStation* Station)
{
	if (!bInventoryOpen)
	{
		ToggleInventory();
	}
	CurrentStation = Station;
}

bool AAGBHUD::HandleCraftingClick(const FString& BoxName)
{
	AAGBCharacter* Character = Cast<AAGBCharacter>(GetOwningPawn());
	if (!Character)
	{
		return false;
	}
	FString Index;
	if (BoxName.Split(TEXT("AGBR_"), nullptr, &Index))
	{
		const int32 RecipeIndex = FCString::Atoi(*Index);
		const APlayerController* PlayerController = GetOwningPlayerController();
		const bool bFive = PlayerController && (PlayerController->IsInputKeyDown(EKeys::LeftShift) || PlayerController->IsInputKeyDown(EKeys::RightShift));
		if (DrawnRecipes.IsValidIndex(RecipeIndex) && DrawnRecipes[RecipeIndex].IsValid())
		{
			Character->Crafting->RequestCraft(DrawnRecipes[RecipeIndex].Get(), bFive ? 5 : 1);
		}
		return true;
	}
	if (BoxName.Split(TEXT("AGBQ_"), nullptr, &Index))
	{
		Character->Crafting->RequestCancel(FCString::Atoi(*Index));
		return true;
	}
	if (BoxName == TEXT("AGBF"))
	{
		if (AAGBCraftingStation* Station = CurrentStation.Get())
		{
			Character->Crafting->RequestAddFuel(Station);
		}
		return true;
	}
	return false;
}

void AAGBHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	if (HandleCraftingClick(BoxName.ToString()))
	{
		return;
	}
	AAGBCharacter* Character = Cast<AAGBCharacter>(GetOwningPawn());
	const FSlotRef Clicked = ParseHitBox(BoxName);
	if (!Character || !Clicked.IsValid())
	{
		return;
	}

	if (!Held.IsValid())
	{
		const FAGBItemStack Stack = Clicked.Inventory->GetSlot(Clicked.Slot);
		if (Stack.IsEmpty())
		{
			return;
		}
		const APlayerController* PlayerController = GetOwningPlayerController();
		const bool bHalf = PlayerController && (PlayerController->IsInputKeyDown(EKeys::LeftShift) || PlayerController->IsInputKeyDown(EKeys::RightShift));
		Held = Clicked;
		HeldCount = (bHalf && Stack.Count > 1) ? Stack.Count / 2 : 0;
		return;
	}

	if (!(Held == Clicked))
	{
		Character->Inventory->RequestMoveItem(Held.Inventory.Get(), Held.Slot, Clicked.Inventory.Get(), Clicked.Slot, HeldCount);
	}
	Held = FSlotRef();
}

void AAGBHUD::NotifyHitBoxBeginCursorOver(FName BoxName)
{
	Super::NotifyHitBoxBeginCursorOver(BoxName);
	Hovered = ParseHitBox(BoxName);
}

void AAGBHUD::NotifyHitBoxEndCursorOver(FName BoxName)
{
	Super::NotifyHitBoxEndCursorOver(BoxName);
	if (ParseHitBox(BoxName) == Hovered)
	{
		Hovered = FSlotRef();
	}
}

void AAGBHUD::UseHoveredSlot()
{
	AAGBCharacter* Character = Cast<AAGBCharacter>(GetOwningPawn());
	if (Character && Hovered.IsValid())
	{
		Character->RequestUseItem(Hovered.Inventory.Get(), Hovered.Slot);
	}
}

void AAGBHUD::DrawVitals(AAGBCharacter* Character, float Scale)
{
	const UAGBVitalsComponent* Vitals = Character->Vitals;
	if (!Vitals)
	{
		return;
	}
	UFont* Font = GEngine->GetSmallFont();
	const float Width = 220.f * Scale;
	const float Height = 14.f * Scale;
	const float Gap = 6.f * Scale;
	const float X = 24.f * Scale;
	float Y = Canvas->ClipY - 24.f * Scale;

	// Temperature line at the bottom, bars stacked above it.
	const float Air = Vitals->GetAirTemperature();
	const FVector2D Comfort = Vitals->GetComfortRange();
	const TCHAR* Feeling = Air < Comfort.X ? TEXT("  Cold!") : (Air > Comfort.Y ? TEXT("  Hot!") : TEXT(""));
	const FLinearColor TemperatureColor = Air < Comfort.X ? FLinearColor(0.5f, 0.8f, 1.f) : (Air > Comfort.Y ? FLinearColor(1.f, 0.5f, 0.3f) : TextColor);
	float TextW = 0.f, TextH = 0.f;
	const FString Temperature = FString::Printf(TEXT("%.0f %sC%s"), Air, *FString(1, &DegreeSign), Feeling);
	GetTextSize(Temperature, TextW, TextH, Font, Scale);
	Y -= TextH;
	DrawText(Temperature, TemperatureColor, X, Y, Font, Scale);
	Y -= Gap;

	const TArray<FAGBStatConfig>& Stats = Vitals->GetConfig()->Stats;
	for (int32 Index = Stats.Num() - 1; Index >= 0; --Index)
	{
		const FAGBStatConfig& Stat = Stats[Index];
		if (!Stat.bShowOnHUD)
		{
			continue;
		}
		Y -= Height;
		const float Fraction = FMath::Clamp(Vitals->GetFraction(Stat.Id), 0.f, 1.f);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), X, Y, Width, Height);
		DrawRect(Stat.Color, X + 2.f, Y + 2.f, (Width - 4.f) * Fraction, Height - 4.f);
		const FString Label = FString::Printf(TEXT("%s  %.0f"), *Stat.DisplayName.ToString(), Vitals->GetValue(Stat.Id));
		GetTextSize(Label, TextW, TextH, Font, Scale * 0.9f);
		DrawText(Label, TextColor, X + 6.f, Y + (Height - TextH) * 0.5f, Font, Scale * 0.9f);
		Y -= Gap;
	}
}

void AAGBHUD::AddNotification(const FText& Message, bool bWarning)
{
	const FString Text = Message.ToString();
	const double Now = GetWorld()->GetTimeSeconds();
	// Same message again: refresh it instead of stacking duplicates.
	Notifications.RemoveAll([&Text](const FNotification& Existing) { return Existing.Text == Text; });
	Notifications.Add({ Text, bWarning, Now });
	if (Notifications.Num() > 6)
	{
		Notifications.RemoveAt(0);
	}
}

void AAGBHUD::DrawNotifications(float Scale)
{
	constexpr double Lifetime = 3.0;
	constexpr double FadeTime = 0.5;
	const double Now = GetWorld()->GetTimeSeconds();
	Notifications.RemoveAll([Now](const FNotification& Entry) { return Now - Entry.Time > Lifetime; });

	UFont* Font = GEngine->GetMediumFont();
	float Y = Canvas->ClipY - 140.f * Scale;
	for (int32 Index = Notifications.Num() - 1; Index >= 0; --Index)
	{
		const FNotification& Entry = Notifications[Index];
		const float Alpha = static_cast<float>(FMath::Clamp((Lifetime - (Now - Entry.Time)) / FadeTime, 0.0, 1.0));
		FLinearColor Color = Entry.bWarning ? FLinearColor(1.f, 0.65f, 0.25f) : TextColor;
		Color.A = Alpha;
		float W = 0.f, H = 0.f;
		GetTextSize(Entry.Text, W, H, Font, Scale);
		const float X = Canvas->ClipX - W - 30.f * Scale;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f * Alpha), X - 8.f, Y - 3.f, W + 16.f, H + 6.f);
		DrawText(Entry.Text, Color, X, Y, Font, Scale);
		Y -= H + 10.f * Scale;
	}
}

void AAGBHUD::DrawDeathScreen(float Scale)
{
	const APlayerController* PlayerController = GetOwningPlayerController();
	const AAGBCharacter* Body = PlayerController ? Cast<AAGBCharacter>(PlayerController->GetViewTarget()) : nullptr;
	if (!Body || !Body->IsDead())
	{
		return;
	}
	DrawRect(FLinearColor(0.15f, 0.f, 0.f, 0.35f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	UFont* Font = GEngine->GetLargeFont();
	const FString Title = TEXT("You died");
	float W = 0.f, H = 0.f;
	GetTextSize(Title, W, H, Font, Scale * 1.5f);
	const float Y = Canvas->ClipY * 0.4f;
	DrawText(Title, FLinearColor(1.f, 0.85f, 0.85f), (Canvas->ClipX - W) * 0.5f, Y, Font, Scale * 1.5f);

	const FString Detail = FString::Printf(TEXT("%s. Respawning..."), *Body->Vitals->GetDeathCause());
	float DW = 0.f, DH = 0.f;
	GetTextSize(Detail, DW, DH, GEngine->GetMediumFont(), Scale);
	DrawText(Detail, TextColor, (Canvas->ClipX - DW) * 0.5f, Y + H * 1.5f + 10.f * Scale, GEngine->GetMediumFont(), Scale);
}

void AAGBHUD::DropHoveredSlot()
{
	AAGBCharacter* Character = Cast<AAGBCharacter>(GetOwningPawn());
	if (Character && Hovered.IsValid())
	{
		Character->Inventory->RequestDropItem(Hovered.Inventory.Get(), Hovered.Slot, 0);
		if (Held == Hovered)
		{
			Held = FSlotRef();
		}
	}
}

// ---------------------------------------------------------------- drawing

void AAGBHUD::DrawFrame(float X, float Y, float W, float H, float Thickness, const FLinearColor& Color)
{
	DrawRect(Color, X, Y, W, Thickness);
	DrawRect(Color, X, Y + H - Thickness, W, Thickness);
	DrawRect(Color, X, Y, Thickness, H);
	DrawRect(Color, X + W - Thickness, Y, Thickness, H);
}

void AAGBHUD::DrawSlot(UAGBInventoryComponent* Inventory, int32 InventoryIndex, int32 SlotIndex, float X, float Y, float Size, bool bSelected, const FString& EmptyLabel)
{
	const FAGBItemStack Stack = Inventory->GetSlot(SlotIndex);
	const FSlotRef This{ Inventory, SlotIndex };
	UFont* SmallFont = GEngine->GetSmallFont();
	UFont* TinyFont = GEngine->GetTinyFont();
	const float TextScale = FMath::Max(1.f, Size / 60.f);

	DrawRect(SlotColor, X, Y, Size, Size);
	if (bInventoryOpen && Hovered == This)
	{
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.15f), X, Y, Size, Size);
	}

	if (!Stack.IsEmpty())
	{
		UTexture2D* Icon = Stack.Item->Icon.LoadSynchronous();
		if (Icon)
		{
			DrawTexture(Icon, X + 4.f, Y + 4.f, Size - 8.f, Size - 8.f, 0.f, 0.f, 1.f, 1.f);
		}
		else
		{
			const FString Name = ShortName(Stack.Item);
			float W = 0.f, H = 0.f;
			GetTextSize(Name, W, H, SmallFont, TextScale);
			DrawText(Name, TextColor, X + (Size - W) * 0.5f, Y + (Size - H) * 0.5f, SmallFont, TextScale);
		}
		if (Stack.Count > 1)
		{
			const FString Count = FString::FromInt(Stack.Count);
			float W = 0.f, H = 0.f;
			GetTextSize(Count, W, H, SmallFont, TextScale);
			DrawText(Count, TextColor, X + Size - W - 4.f, Y + Size - H - 2.f, SmallFont, TextScale);
		}
	}
	if (!EmptyLabel.IsEmpty() && (Stack.IsEmpty() || InventoryIndex == HotbarIndex))
	{
		DrawText(EmptyLabel, LabelColor, X + 4.f, Y + 2.f, TinyFont, TextScale);
	}

	if (Held == This)
	{
		DrawFrame(X, Y, Size, Size, 3.f, HeldColor);
	}
	else if (bSelected)
	{
		DrawFrame(X, Y, Size, Size, 3.f, SelectedColor);
	}

	if (bInventoryOpen)
	{
		AddHitBox(FVector2D(X, Y), FVector2D(Size, Size), *FString::Printf(TEXT("AGB_%d_%d"), InventoryIndex, SlotIndex), true);
	}
}

void AAGBHUD::DrawHotbar(AAGBCharacter* Character, float Scale)
{
	UAGBInventoryComponent* Hotbar = Character->Hotbar;
	const int32 NumSlots = Hotbar->GetNumSlots();
	if (NumSlots == 0)
	{
		return;
	}
	const float Size = 60.f * Scale;
	const float Gap = 6.f * Scale;
	const float Width = NumSlots * Size + (NumSlots - 1) * Gap;
	const float X0 = (Canvas->ClipX - Width) * 0.5f;
	const float Y = Canvas->ClipY - Size - 24.f * Scale;
	for (int32 Index = 0; Index < NumSlots; ++Index)
	{
		const FString Number = FString::FromInt((Index + 1) % 10);
		DrawSlot(Hotbar, HotbarIndex, Index, X0 + Index * (Size + Gap), Y, Size, Index == Character->GetSelectedHotbarSlot(), Number);
	}

	// Name of the selected item above the bar.
	const FAGBItemStack Selected = Character->GetSelectedItem();
	if (!Selected.IsEmpty() && !bInventoryOpen)
	{
		const FString Name = Selected.Item->GetDisplayNameOrId().ToString();
		float W = 0.f, H = 0.f;
		GetTextSize(Name, W, H, GEngine->GetSmallFont(), Scale);
		DrawText(Name, TextColor, (Canvas->ClipX - W) * 0.5f, Y - H - 6.f * Scale, GEngine->GetSmallFont(), Scale);
	}
}

void AAGBHUD::DrawInventoryScreen(AAGBCharacter* Character, float Scale)
{
	UAGBInventoryComponent* Backpack = Character->Inventory;
	UAGBInventoryComponent* Equipment = Character->Equipment;
	UFont* Font = GEngine->GetMediumFont();
	UFont* SmallFont = GEngine->GetSmallFont();

	const float Size = 60.f * Scale;
	const float Gap = 6.f * Scale;
	const float Step = Size + Gap;
	const float Padding = 20.f * Scale;
	const float TitleHeight = 40.f * Scale;
	const int32 Rows = FMath::DivideAndRoundUp(Backpack->GetNumSlots(), InventoryColumns);
	const int32 EquipmentRows = Equipment->GetNumSlots();

	const float GridWidth = InventoryColumns * Step - Gap;
	const float EquipmentWidth = Size + 40.f * Scale;
	const float PanelWidth = Padding * 2.f + EquipmentWidth + GridWidth;
	const float PanelHeight = Padding * 2.f + TitleHeight + FMath::Max(Rows, EquipmentRows) * Step - Gap + 40.f * Scale;
	const float CraftingWidth = CraftingPanelWidth * Scale;
	const float PanelX = (Canvas->ClipX - PanelWidth - CraftingWidth - 12.f * Scale) * 0.5f;
	const float PanelY = FMath::Max(10.f, (Canvas->ClipY - 120.f * Scale - PanelHeight) * 0.5f);
	CraftingPanelX = PanelX + PanelWidth + 12.f * Scale;
	CraftingPanelY = PanelY;
	CraftingPanelHeight = PanelHeight;

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.35f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	DrawRect(FLinearColor(0.05f, 0.05f, 0.06f, 0.85f), PanelX, PanelY, PanelWidth, PanelHeight);

	// Title with weight.
	const float Weight = Backpack->GetTotalWeight() + Character->Hotbar->GetTotalWeight() + Equipment->GetTotalWeight();
	const FString Title = FString::Printf(TEXT("%s    %.1f kg"), *Backpack->DisplayName.ToString(), Weight);
	DrawText(Title, TextColor, PanelX + Padding, PanelY + Padding, Font, Scale);

	const float ContentY = PanelY + Padding + TitleHeight;
	for (int32 Index = 0; Index < EquipmentRows; ++Index)
	{
		const EAGBEquipSlot Type = Equipment->SlotTypes.IsValidIndex(Index) ? Equipment->SlotTypes[Index] : EAGBEquipSlot::None;
		DrawSlot(Equipment, EquipmentIndex, Index, PanelX + Padding, ContentY + Index * Step, Size, false, EquipSlotName(Type));
	}
	const float GridX = PanelX + Padding + EquipmentWidth;
	for (int32 Index = 0; Index < Backpack->GetNumSlots(); ++Index)
	{
		DrawSlot(Backpack, BackpackIndex, Index, GridX + (Index % InventoryColumns) * Step, ContentY + (Index / InventoryColumns) * Step, Size, false, FString());
	}

	// Footer: what is held or hovered, then help.
	FString Info;
	if (Held.IsValid())
	{
		const FAGBItemStack Stack = Held.Inventory->GetSlot(Held.Slot);
		if (!Stack.IsEmpty())
		{
			Info = FString::Printf(TEXT("Moving %s x%d: click a slot to place it."), *Stack.Item->GetDisplayNameOrId().ToString(), HeldCount > 0 ? HeldCount : Stack.Count);
		}
	}
	else if (Hovered.IsValid())
	{
		const FAGBItemStack Stack = Hovered.Inventory->GetSlot(Hovered.Slot);
		if (!Stack.IsEmpty())
		{
			const FString Category = StaticEnum<EAGBItemCategory>()->GetDisplayNameTextByValue(static_cast<int64>(Stack.Item->Category)).ToString();
			Info = FString::Printf(TEXT("%s (%s, %.1f kg each)  %s"), *Stack.Item->GetDisplayNameOrId().ToString(), *Category, Stack.Item->Weight, *Stack.Item->Description.ToString());
		}
	}
	const float FooterY = PanelY + PanelHeight - Padding - 30.f * Scale;
	DrawText(Info, TextColor, PanelX + Padding, FooterY, SmallFont, Scale);
	DrawText(TEXT("Click: pick up / place    Shift+click: half    E: eat / use    Q: drop    Tab: close"), LabelColor, PanelX + Padding, FooterY + 16.f * Scale, SmallFont, Scale);
}

void AAGBHUD::DrawCraftingPanel(AAGBCharacter* Character, float Scale)
{
	UAGBCraftingComponent* Crafting = Character->Crafting;
	if (!Crafting)
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	UFont* SmallFont = GEngine->GetSmallFont();
	const float Padding = 16.f * Scale;
	const float Width = CraftingPanelWidth * Scale;
	const float X = CraftingPanelX;
	const float Bottom = CraftingPanelY + CraftingPanelHeight;
	float Y = CraftingPanelY;
	DrawRect(FLinearColor(0.05f, 0.05f, 0.06f, 0.85f), X, Y, Width, CraftingPanelHeight);
	Y += Padding;

	// Station header (fuel, out of reach).
	AAGBCraftingStation* Station = CurrentStation.Get();
	if (Station && !Station->IsInRange(Character))
	{
		CurrentStation.Reset();
		Station = nullptr;
	}
	FString Title = TEXT("Crafting");
	if (Station && Station->Definition)
	{
		Title = Station->Definition->GetDisplayNameOrId().ToString();
		if (Station->Definition->bNeedsFuel)
		{
			const int32 Seconds = FMath::CeilToInt(Station->GetFuelSeconds());
			Title += Seconds > 0 ? FString::Printf(TEXT("  (burning %d:%02d)"), Seconds / 60, Seconds % 60) : FString(TEXT("  (out of fuel)"));
		}
	}
	DrawText(Title, TextColor, X + Padding, Y, Font, Scale);
	if (Station && Station->Definition && Station->Definition->bNeedsFuel)
	{
		const FString Button = TEXT("Add fuel");
		float W = 0.f, H = 0.f;
		GetTextSize(Button, W, H, SmallFont, Scale);
		const float BX = X + Width - Padding - W - 16.f * Scale;
		DrawRect(FLinearColor(0.35f, 0.2f, 0.05f, 0.9f), BX, Y, W + 16.f * Scale, H + 8.f * Scale);
		DrawText(Button, TextColor, BX + 8.f * Scale, Y + 4.f * Scale, SmallFont, Scale);
		AddHitBox(FVector2D(BX, Y), FVector2D(W + 16.f * Scale, H + 8.f * Scale), TEXT("AGBF"), true);
	}
	Y += 34.f * Scale;

	// Queue (click to cancel).
	const TArray<FAGBCraftJob>& Queue = Crafting->GetQueue();
	for (int32 Index = 0; Index < Queue.Num() && Index < 4; ++Index)
	{
		const FAGBCraftJob& Job = Queue[Index];
		if (!Job.Recipe)
		{
			continue;
		}
		const float RowH = 22.f * Scale;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), X + Padding, Y, Width - Padding * 2.f, RowH);
		DrawRect(FLinearColor(0.3f, 0.6f, 0.9f, 0.6f), X + Padding, Y, (Width - Padding * 2.f) * Crafting->GetJobProgress(Index), RowH);
		FString Line = FString::Printf(TEXT("%s x%d"), *Job.Recipe->GetDisplayNameOrId().ToString(), Job.Remaining);
		if (Job.bPaused && Job.Recipe->Station)
		{
			Line += FString::Printf(TEXT("  (waiting for %s)"), *Job.Recipe->Station->GetDisplayNameOrId().ToString());
		}
		DrawText(Line, TextColor, X + Padding + 6.f * Scale, Y + 3.f * Scale, SmallFont, Scale);
		AddHitBox(FVector2D(X + Padding, Y), FVector2D(Width - Padding * 2.f, RowH), *FString::Printf(TEXT("AGBQ_%d"), Index), true);
		Y += RowH + 4.f * Scale;
	}
	if (Queue.Num() > 0)
	{
		Y += 8.f * Scale;
	}

	// Recipes: ones for the open station first, then the rest.
	TArray<UAGBRecipeDefinition*> Recipes = Crafting->GetKnownRecipes();
	if (Station)
	{
		Recipes.StableSort([Station](const UAGBRecipeDefinition& A, const UAGBRecipeDefinition& B)
		{
			return (A.Station == Station->Definition) > (B.Station == Station->Definition);
		});
	}
	DrawnRecipes.Reset();
	const float RowHeight = 42.f * Scale;
	const float ListBottom = Bottom - Padding - 18.f * Scale;
	int32 Hidden = 0;
	for (UAGBRecipeDefinition* Recipe : Recipes)
	{
		if (Y + RowHeight > ListBottom)
		{
			++Hidden;
			continue;
		}
		const int32 Index = DrawnRecipes.Add(Recipe);
		const int32 Craftable = Crafting->GetMaxCraftable(Recipe);
		const EAGBStationStatus StationStatus = Crafting->GetStationStatus(Recipe);
		const bool bReady = Craftable > 0 && StationStatus == EAGBStationStatus::Ready;

		DrawRect(FLinearColor(1.f, 1.f, 1.f, bReady ? 0.08f : 0.03f), X + Padding, Y, Width - Padding * 2.f, RowHeight - 4.f * Scale);
		const int32 OutCount = Recipe->Outputs.Num() > 0 ? Recipe->Outputs[0].Count : 1;
		FString Name = Recipe->GetDisplayNameOrId().ToString() + (OutCount > 1 ? FString::Printf(TEXT(" x%d"), OutCount) : FString());
		DrawText(Name, bReady ? TextColor : LabelColor, X + Padding + 6.f * Scale, Y + 2.f * Scale, SmallFont, Scale);

		FString Right = FString::Printf(TEXT("%.0fs"), Recipe->CraftSeconds);
		if (Recipe->Station && StationStatus != EAGBStationStatus::Ready)
		{
			Right = (StationStatus == EAGBStationStatus::NeedsFuel ? TEXT("light ") : TEXT("needs ")) + Recipe->Station->GetDisplayNameOrId().ToString();
		}
		float RW = 0.f, RH = 0.f;
		GetTextSize(Right, RW, RH, SmallFont, Scale);
		DrawText(Right, StationStatus == EAGBStationStatus::Ready ? LabelColor : BadColor, X + Width - Padding - RW - 6.f * Scale, Y + 2.f * Scale, SmallFont, Scale);

		// Ingredients: have/need, green when enough.
		float IX = X + Padding + 6.f * Scale;
		for (const FAGBItemAmount& Ingredient : Recipe->Ingredients)
		{
			if (!Ingredient.Item)
			{
				continue;
			}
			const int32 Have = Character->CountItem(Ingredient.Item);
			const FString Part = FString::Printf(TEXT("%s %d/%d   "), *Ingredient.Item->GetDisplayNameOrId().ToString(), Have, Ingredient.Count);
			float PW = 0.f, PH = 0.f;
			GetTextSize(Part, PW, PH, SmallFont, Scale * 0.9f);
			DrawText(Part, Have >= Ingredient.Count ? GoodColor : BadColor, IX, Y + 19.f * Scale, SmallFont, Scale * 0.9f);
			IX += PW;
		}
		AddHitBox(FVector2D(X + Padding, Y), FVector2D(Width - Padding * 2.f, RowHeight - 4.f * Scale), *FString::Printf(TEXT("AGBR_%d"), Index), true);
		Y += RowHeight;
	}
	FString Footer = Recipes.Num() == 0 ? FString(TEXT("No recipes known yet.")) : FString(TEXT("Click: craft 1    Shift+click: craft 5    Click queue: cancel"));
	if (Hidden > 0)
	{
		Footer = FString::Printf(TEXT("+%d more recipes.  "), Hidden) + Footer;
	}
	DrawText(Footer, LabelColor, X + Padding, Bottom - Padding - 12.f * Scale, SmallFont, Scale * 0.9f);
}

void AAGBHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}

	const float Scale = FMath::Max(0.6f, Canvas->ClipY / 1080.f);
	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	AAGBCharacter* Character = Cast<AAGBCharacter>(GetOwningPawn());
	if (!Character)
	{
		if (bInventoryOpen)
		{
			ToggleInventory(); // Died or lost the pawn with the screen open.
		}
		DrawDeathScreen(Scale);
		return;
	}

	if (bInventoryOpen)
	{
		DrawInventoryScreen(Character, Scale);
		DrawCraftingPanel(Character, Scale);
	}
	DrawHotbar(Character, Scale);
	DrawVitals(Character, Scale);
	DrawNotifications(Scale);
	if (bInventoryOpen)
	{
		return;
	}

	if (bShowCrosshair)
	{
		const float Size = 4.f;
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.8f), CenterX - Size * 0.5f, CenterY - Size * 0.5f, Size, Size);
	}

	const APawn* Pawn = GetOwningPawn();
	const UAGBInteractionComponent* Interaction = Pawn ? Pawn->FindComponentByClass<UAGBInteractionComponent>() : nullptr;
	if (Interaction && Interaction->HasFocus())
	{
		const FString Prompt = FString::Printf(TEXT("[%s] %s"), *GetInteractKeyName(), *Interaction->GetFocusedPrompt().ToString());
		UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
		float Width = 0.f, Height = 0.f;
		GetTextSize(Prompt, Width, Height, Font, Scale);
		const float X = CenterX - Width * 0.5f;
		const float Y = CenterY + 40.f * Scale;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), X - 8.f, Y - 4.f, Width + 16.f, Height + 8.f);
		DrawText(Prompt, TextColor, X, Y, Font, Scale);
	}
	else if (Interaction && !Interaction->GetFocusedResourceText().IsEmpty())
	{
		// Harvestable in reach: its name, or what tool it needs.
		const FString Text = Interaction->GetFocusedResourceText().ToString();
		UFont* Font = GEngine->GetSmallFont();
		float Width = 0.f, Height = 0.f;
		GetTextSize(Text, Width, Height, Font, Scale);
		const FLinearColor Color = Interaction->CanHarvestFocusedResource() ? TextColor : FLinearColor(1.f, 0.65f, 0.25f);
		DrawText(Text, Color, CenterX - Width * 0.5f, CenterY + 24.f * Scale, Font, Scale);
	}
}
