#include "AGBMenus.h"

#include "AGBAudioFX.h"
#include "AGBCharacter.h"
#include "AGBDefaultInput.h"
#include "AGBGameFramework.h"
#include "AGBMultiplayer.h"
#include "AGBPlayerSettings.h"
#include "AGBUITheme.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Camera/CameraActor.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/InputKeySelector.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/Font.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/FileManager.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "AIGameBuilder"

namespace
{
	FSlateBrush MakeBrush(UTexture2D* Texture, const FLinearColor& Color, float Radius)
	{
		if (Texture)
		{
			FSlateBrush Brush;
			Brush.SetResourceObject(Texture);
			Brush.ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
			Brush.DrawAs = ESlateBrushDrawType::Box;
			Brush.Margin = FMargin(0.25f);
			Brush.TintColor = Color;
			return Brush;
		}
		return FSlateRoundedBoxBrush(Color, Radius);
	}

	FSlateFontInfo MakeFont(const UAGBUITheme* Theme, int32 Size, bool bTitleFont)
	{
		UFont* Font = bTitleFont ? Theme->TitleFont.LoadSynchronous() : nullptr;
		if (!Font)
		{
			Font = Theme->Font.LoadSynchronous();
		}
		if (Font)
		{
			return FSlateFontInfo(Font, Size);
		}
		return FCoreStyle::GetDefaultFontStyle(bTitleFont ? "Bold" : "Regular", Size);
	}

	FString ResolutionLabel(const FIntPoint& Resolution)
	{
		return FString::Printf(TEXT("%d x %d"), Resolution.X, Resolution.Y);
	}

	FText Percent(float Value)
	{
		return FText::AsPercent(Value);
	}

	APlayerController* FirstLocalPlayer(const UObject* WorldContext)
	{
		return UGameplayStatics::GetPlayerController(WorldContext, 0);
	}
}

void UAGBUIProxy::HandleClick()
{
	if (UAGBAudioFXSubsystem* Audio = UAGBAudioFXSubsystem::Get(GetOuter()))
	{
		Audio->PlayCue2D(TEXT("UI.Click"));
	}
	if (Click)
	{
		Click();
	}
}

void UAGBUIProxy::HandleHover()
{
	if (UAGBAudioFXSubsystem* Audio = UAGBAudioFXSubsystem::Get(GetOuter()))
	{
		Audio->PlayCue2D(TEXT("UI.Hover"));
	}
}

// ---------------------------------------------------------------- base widget

TSharedRef<SWidget> UAGBMenuWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Theme = UAGBUISettings::GetTheme();
		Proxies.Reset();
		SetIsFocusable(true);

		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Root;

		// Background: menu image, or a dim layer over the game.
		UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
		UTexture2D* Background = UsesMenuBackground() ? Theme->MenuBackground.LoadSynchronous() : nullptr;
		if (Background)
		{
			FSlateBrush Brush;
			Brush.SetResourceObject(Background);
			Brush.DrawAs = ESlateBrushDrawType::Image;
			Backdrop->SetBrush(Brush);
		}
		else
		{
			Backdrop->SetBrush(FSlateRoundedBoxBrush(Theme->BackdropColor, 0.f));
		}
		UOverlaySlot* BackdropSlot = Root->AddChildToOverlay(Backdrop);
		BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
		BackdropSlot->SetVerticalAlignment(VAlign_Fill);

		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Size->SetWidthOverride(GetPanelWidth());
		Size->SetMaxDesiredHeight(980.f);
		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
		Panel->SetBrush(MakeBrush(Theme->PanelTexture.LoadSynchronous(), Theme->PanelColor, 10.f));
		Panel->SetPadding(FMargin(36.f, 30.f));
		UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Content"));
		Panel->SetContent(Content);
		Size->SetContent(Panel);
		UOverlaySlot* PanelSlot = Root->AddChildToOverlay(Size);
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
		PanelSlot->SetVerticalAlignment(VAlign_Center);

		BuildContent(Content);
	}
	return Super::RebuildWidget();
}

void UAGBMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	FocusFirst();
}

void UAGBMenuWidget::FocusFirst()
{
	if (FirstFocus)
	{
		FirstFocus->SetKeyboardFocus();
	}
	else
	{
		SetKeyboardFocus();
	}
}

FReply UAGBMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right)
	{
		GoBack();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UAGBMenuWidget::GoBack()
{
	if (UAGBMenuSubsystem* Menus = UAGBMenuSubsystem::Get(GetOwningPlayer()))
	{
		Menus->CloseTop();
	}
}

UAGBUIProxy* UAGBMenuWidget::NewProxy()
{
	return Proxies.Add_GetRef(NewObject<UAGBUIProxy>(this));
}

UTextBlock* UAGBMenuWidget::MakeText(const FText& Text, int32 Size, const FLinearColor& Color, bool bTitleFont)
{
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Block->SetText(Text);
	Block->SetFont(MakeFont(Theme, Size, bTitleFont));
	Block->SetColorAndOpacity(FSlateColor(Color));
	Block->SetAutoWrapText(true);
	return Block;
}

UButton* UAGBMenuWidget::MakeButton(const FText& Label, TFunction<void()> OnClick, bool bAccent)
{
	UTexture2D* Texture = Theme->ButtonTexture.LoadSynchronous();
	const FLinearColor Normal = bAccent ? Theme->ButtonHoverColor : Theme->ButtonColor;
	FLinearColor Hover = Theme->ButtonHoverColor;
	if (bAccent)
	{
		Hover = Theme->AccentColor;
	}
	FLinearColor Pressed = Theme->AccentColor * 0.8f;
	Pressed.A = 1.f;
	FLinearColor Disabled = Theme->ButtonColor;
	Disabled.A *= 0.4f;

	FButtonStyle Style;
	Style.SetNormal(MakeBrush(Texture, Normal, 6.f));
	Style.SetHovered(MakeBrush(Texture, Hover, 6.f));
	Style.SetPressed(MakeBrush(Texture, Pressed, 6.f));
	Style.SetDisabled(MakeBrush(Texture, Disabled, 6.f));
	Style.SetNormalPadding(FMargin(18.f, 10.f));
	Style.SetPressedPadding(FMargin(18.f, 11.f, 18.f, 9.f));

	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	Button->SetStyle(Style);
	UTextBlock* Text = MakeText(Label, FMath::RoundToInt(Theme->BodySize * 1.1f), Theme->TextColor, true);
	Text->SetAutoWrapText(false);
	if (UButtonSlot* TextSlot = Cast<UButtonSlot>(Button->AddChild(Text)))
	{
		TextSlot->SetHorizontalAlignment(HAlign_Center);
	}
	UAGBUIProxy* Proxy = NewProxy();
	Proxy->Click = MoveTemp(OnClick);
	Button->OnClicked.AddDynamic(Proxy, &UAGBUIProxy::HandleClick);
	Button->OnHovered.AddDynamic(Proxy, &UAGBUIProxy::HandleHover);
	if (!FirstFocus)
	{
		FirstFocus = Button;
	}
	return Button;
}

UWidget* UAGBMenuWidget::MakeLabeledRow(const FText& Label, UWidget* Control)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(MakeText(Label, Theme->BodySize, Theme->TextColor));
	LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	LabelSlot->SetVerticalAlignment(VAlign_Center);
	LabelSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
	UHorizontalBoxSlot* ControlSlot = Row->AddChildToHorizontalBox(Control);
	ControlSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	ControlSlot->SetVerticalAlignment(VAlign_Center);
	return Row;
}

UWidget* UAGBMenuWidget::MakeSliderRow(const FText& Label, float Value, float Min, float Max, float Step, TFunction<FText(float)> Format, TFunction<void(float)> OnChanged)
{
	UHorizontalBox* Control = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	USlider* Slider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
	Slider->SetMinValue(Min);
	Slider->SetMaxValue(Max);
	Slider->SetStepSize(Step);
	Slider->SetValue(Value);
	Slider->SetSliderBarColor(Theme->DimTextColor);
	Slider->SetSliderHandleColor(Theme->AccentColor);
	UHorizontalBoxSlot* SliderSlot = Control->AddChildToHorizontalBox(Slider);
	SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SliderSlot->SetVerticalAlignment(VAlign_Center);

	USizeBox* ValueBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	ValueBox->SetWidthOverride(70.f);
	UTextBlock* ValueText = MakeText(Format(Value), Theme->BodySize, Theme->DimTextColor);
	ValueText->SetJustification(ETextJustify::Right);
	ValueBox->SetContent(ValueText);
	Control->AddChildToHorizontalBox(ValueBox)->SetVerticalAlignment(VAlign_Center);

	UAGBUIProxy* Proxy = NewProxy();
	Proxy->Float = [ValueText, Format, OnChanged](float NewValue)
	{
		ValueText->SetText(Format(NewValue));
		OnChanged(NewValue);
	};
	Slider->OnValueChanged.AddDynamic(Proxy, &UAGBUIProxy::HandleFloat);
	return MakeLabeledRow(Label, Control);
}

UWidget* UAGBMenuWidget::MakeCheckRow(const FText& Label, bool bValue, TFunction<void(bool)> OnChanged)
{
	UCheckBox* Check = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
	Check->SetIsChecked(bValue);
	UAGBUIProxy* Proxy = NewProxy();
	Proxy->Bool = MoveTemp(OnChanged);
	Check->OnCheckStateChanged.AddDynamic(Proxy, &UAGBUIProxy::HandleBool);
	UWidget* Row = MakeLabeledRow(Label, Check);
	return Row;
}

UWidget* UAGBMenuWidget::MakeComboRow(const FText& Label, const TArray<FString>& Options, const FString& Selected, TFunction<void(const FString&)> OnChanged, UComboBoxString** OutCombo)
{
	UComboBoxString* Combo = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass());
	for (const FString& Option : Options)
	{
		Combo->AddOption(Option);
	}
	Combo->SetSelectedOption(Selected);
	UAGBUIProxy* Proxy = NewProxy();
	Proxy->String = MoveTemp(OnChanged);
	Combo->OnSelectionChanged.AddDynamic(Proxy, &UAGBUIProxy::HandleString);
	if (OutCombo)
	{
		*OutCombo = Combo;
	}
	return MakeLabeledRow(Label, Combo);
}

UEditableTextBox* UAGBMenuWidget::MakeTextField(const FText& Hint, const FString& Initial, bool bPassword)
{
	UEditableTextBox* Field = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
	Field->SetHintText(Hint);
	Field->SetText(FText::FromString(Initial));
	Field->SetIsPassword(bPassword);
	return Field;
}

void UAGBMenuWidget::AddSpacer(UPanelWidget* Panel, float Height)
{
	USpacer* Spacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
	Spacer->SetSize(FVector2D(1.f, Height));
	Panel->AddChild(Spacer);
}

// ---------------------------------------------------------------- main menu

void UAGBMainMenuWidget::BuildContent(UVerticalBox* Panel)
{
	if (UTexture2D* Logo = Theme->Logo.LoadSynchronous())
	{
		UImage* Image = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		Image->SetBrushFromTexture(Logo, true);
		Panel->AddChildToVerticalBox(Image)->SetHorizontalAlignment(HAlign_Center);
	}
	else
	{
		UTextBlock* Title = MakeText(Theme->GameTitle, Theme->TitleSize, Theme->AccentColor, true);
		Title->SetJustification(ETextJustify::Center);
		Panel->AddChildToVerticalBox(Title);
	}
	if (!Theme->Subtitle.IsEmpty())
	{
		UTextBlock* Subtitle = MakeText(Theme->Subtitle, Theme->BodySize, Theme->DimTextColor);
		Subtitle->SetJustification(ETextJustify::Center);
		Panel->AddChildToVerticalBox(Subtitle);
	}
	if (UAGBNetworkSubsystem* Network = UAGBNetworkSubsystem::Get(this))
	{
		const FText Error = Network->TakeLastError();
		if (!Error.IsEmpty())
		{
			AddSpacer(Panel, 10.f);
			UTextBlock* ErrorText = MakeText(Error, Theme->BodySize, Theme->WarningColor);
			ErrorText->SetJustification(ETextJustify::Center);
			Panel->AddChildToVerticalBox(ErrorText);
		}
	}
	AddSpacer(Panel, 28.f);

	auto AddButton = [this, Panel](UButton* Button)
	{
		Panel->AddChildToVerticalBox(Button)->SetPadding(FMargin(0.f, 5.f));
	};
	const bool bHasGame = UAGBMenuLibrary::HasGameMap();
	const bool bHasSave = bHasGame && UAGBMenuLibrary::HasSavedGame(this);
	if (bHasSave)
	{
		AddButton(MakeButton(LOCTEXT("Continue", "Continue"), [this]() { UAGBMenuLibrary::ContinueGame(this); }, true));
	}
	if (bHasGame)
	{
		AddButton(MakeButton(LOCTEXT("NewGame", "New Game"), [this, bHasSave]()
		{
			if (!bHasSave)
			{
				UAGBMenuLibrary::NewGame(this);
				return;
			}
			if (UAGBMenuSubsystem* Menus = UAGBMenuSubsystem::Get(GetOwningPlayer()))
			{
				Menus->Confirm(LOCTEXT("NewGameConfirm", "Start a new world? Your saved world will be deleted."), LOCTEXT("NewGameYes", "New Game"),
					[this]() { UAGBMenuLibrary::NewGame(this); });
			}
		}, !bHasSave));
	}
	else
	{
		UTextBlock* Missing = MakeText(LOCTEXT("NoGameMap", "No game level set yet (UITools.SetupUI)."), Theme->BodySize, Theme->WarningColor);
		Missing->SetJustification(ETextJustify::Center);
		Panel->AddChildToVerticalBox(Missing);
	}
	if (bHasGame)
	{
		AddButton(MakeButton(LOCTEXT("HostGame", "Host Game"), [this]()
		{
			if (UAGBMenuSubsystem* Menus = UAGBMenuSubsystem::Get(GetOwningPlayer()))
			{
				Menus->OpenMenu(UAGBHostMenuWidget::StaticClass());
			}
		}));
	}
	AddButton(MakeButton(LOCTEXT("JoinGame", "Join Game"), [this]()
	{
		if (UAGBMenuSubsystem* Menus = UAGBMenuSubsystem::Get(GetOwningPlayer()))
		{
			Menus->OpenMenu(UAGBJoinMenuWidget::StaticClass());
		}
	}));
	AddButton(MakeButton(LOCTEXT("Settings", "Settings"), [this]() { UAGBMenuLibrary::OpenSettings(this); }));
	AddButton(MakeButton(LOCTEXT("Quit", "Quit"), [this]() { UAGBMenuLibrary::QuitGame(this); }));
}

// ---------------------------------------------------------------- pause menu

void UAGBPauseMenuWidget::BuildContent(UVerticalBox* Panel)
{
	const APlayerController* Player = GetOwningPlayer();
	const bool bMultiplayer = Player && Player->GetNetMode() != NM_Standalone;
	UTextBlock* Title = MakeText(bMultiplayer ? LOCTEXT("MenuTitle", "Menu") : LOCTEXT("Paused", "Paused"), Theme->HeadingSize + 8, Theme->AccentColor, true);
	Title->SetJustification(ETextJustify::Center);
	Panel->AddChildToVerticalBox(Title);
	if (bMultiplayer)
	{
		UTextBlock* Note = MakeText(LOCTEXT("NoPause", "The game keeps running in multiplayer."), Theme->BodySize - 2, Theme->DimTextColor);
		Note->SetJustification(ETextJustify::Center);
		Panel->AddChildToVerticalBox(Note);
	}
	AddSpacer(Panel, 20.f);

	auto AddButton = [Panel](UButton* Button)
	{
		Panel->AddChildToVerticalBox(Button)->SetPadding(FMargin(0.f, 5.f));
	};
	AddButton(MakeButton(LOCTEXT("Resume", "Resume"), [this]() { GoBack(); }, true));
	AddButton(MakeButton(LOCTEXT("Settings", "Settings"), [this]() { UAGBMenuLibrary::OpenSettings(this); }));
	if (Player && Player->HasAuthority())
	{
		AddButton(MakeButton(LOCTEXT("SaveGame", "Save Game"), [this]() { UAGBMenuLibrary::SaveGame(this); GoBack(); }));
	}
	if (UAGBMenuLibrary::HasMainMenuMap())
	{
		AddButton(MakeButton(LOCTEXT("MainMenu", "Main Menu"), [this]() { UAGBMenuLibrary::QuitToMainMenu(this); }));
	}
	AddButton(MakeButton(LOCTEXT("QuitGame", "Quit Game"), [this]()
	{
		if (UAGBMenuSubsystem* Menus = UAGBMenuSubsystem::Get(GetOwningPlayer()))
		{
			Menus->Confirm(LOCTEXT("QuitConfirm", "Quit the game? The world is saved first."), LOCTEXT("QuitYes", "Quit"),
				[this]() { UAGBMenuLibrary::QuitGame(this); });
		}
	}));

	// Players in the game; the host can remove them and sees the address friends join with.
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (bMultiplayer && GameState)
	{
		AddSpacer(Panel, 18.f);
		Panel->AddChildToVerticalBox(MakeText(FText::Format(LOCTEXT("PlayersTitle", "Players ({0})"), GameState->PlayerArray.Num()), Theme->BodySize, Theme->AccentColor));
		AAGBGameMode* GameMode = GetWorld()->GetAuthGameMode<AAGBGameMode>();
		for (APlayerState* Other : GameState->PlayerArray)
		{
			if (!Other)
			{
				continue;
			}
			const AAGBPlayerState* AGBState = Cast<AAGBPlayerState>(Other);
			const FString Label = Other->GetPlayerName() + (AGBState && AGBState->bIsHost ? TEXT("  (host)") : TEXT(""));
			APlayerController* OtherController = Cast<APlayerController>(Other->GetOwner());
			if (GameMode && OtherController && !OtherController->IsLocalController())
			{
				TWeakObjectPtr<APlayerController> Weak = OtherController;
				UWidget* Kick = MakeButton(LOCTEXT("Kick", "Kick"), [GameMode, Weak]()
				{
					if (Weak.IsValid())
					{
						GameMode->KickPlayer(Weak.Get());
					}
				});
				Panel->AddChildToVerticalBox(MakeLabeledRow(FText::FromString(Label), Kick))->SetPadding(FMargin(0.f, 2.f));
			}
			else
			{
				Panel->AddChildToVerticalBox(MakeText(FText::FromString(Label), Theme->BodySize, Theme->TextColor));
			}
		}
		if (Player && Player->GetNetMode() == NM_ListenServer)
		{
			Panel->AddChildToVerticalBox(MakeText(FText::Format(LOCTEXT("JoinAddress", "Friends on your network join with: {0}"),
				FText::FromString(UAGBNetworkSubsystem::GetLocalAddress())), Theme->BodySize - 3, Theme->DimTextColor));
		}
	}
}

FReply UAGBPauseMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// The pause key closes the menu too.
	const AAGBCharacter* Character = Cast<AAGBCharacter>(GetOwningPlayerPawn());
	if (Character && Character->Input.Pause && InKeyEvent.GetKey() == Character->GetActionKey(Character->Input.Pause))
	{
		GoBack();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// ---------------------------------------------------------------- settings

void UAGBSettingsMenuWidget::BuildContent(UVerticalBox* Panel)
{
	UTextBlock* Title = MakeText(LOCTEXT("SettingsTitle", "Settings"), Theme->HeadingSize + 8, Theme->AccentColor, true);
	Panel->AddChildToVerticalBox(Title);
	AddSpacer(Panel, 12.f);

	UWidgetSwitcher* Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass());
	UHorizontalBox* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	const FText TabNames[] = { LOCTEXT("TabGraphics", "Graphics"), LOCTEXT("TabAudio", "Audio"), LOCTEXT("TabControls", "Controls"), LOCTEXT("TabGameplay", "Gameplay") };
	constexpr int32 NumTabs = static_cast<int32>(UE_ARRAY_COUNT(TabNames));
	for (int32 Index = 0; Index < NumTabs; ++Index)
	{
		UButton* Tab = MakeButton(TabNames[Index], [Pages, Index]()
		{
			Pages->SetActiveWidgetIndex(Index);
		});
		UHorizontalBoxSlot* TabSlot = Tabs->AddChildToHorizontalBox(Tab);
		TabSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TabSlot->SetPadding(FMargin(0.f, 0.f, Index + 1 < NumTabs ? 6.f : 0.f, 0.f));
	}
	Panel->AddChildToVerticalBox(Tabs);
	AddSpacer(Panel, 16.f);

	for (int32 Index = 0; Index < NumTabs; ++Index)
	{
		UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
		UVerticalBox* Page = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Scroll->AddChild(Page);
		switch (Index)
		{
		case 0: BuildGraphics(Page); break;
		case 1: BuildAudio(Page); break;
		case 2: BuildControls(Page); break;
		default: BuildGameplay(Page); break;
		}
		Pages->AddChild(Scroll);
	}
	USizeBox* PageSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	PageSize->SetHeightOverride(470.f);
	PageSize->SetContent(Pages);
	Panel->AddChildToVerticalBox(PageSize);
	AddSpacer(Panel, 16.f);
	Panel->AddChildToVerticalBox(MakeButton(LOCTEXT("Back", "Back"), [this]() { GoBack(); }, true));
}

void UAGBSettingsMenuWidget::GoBack()
{
	UGameUserSettings* Graphics = UGameUserSettings::GetGameUserSettings();
	Graphics->SaveSettings();
	if (UAGBPlayerSettingsSubsystem* Settings = UAGBPlayerSettingsSubsystem::Get(this))
	{
		Settings->SaveSettings();
	}
	Super::GoBack();
}

void UAGBSettingsMenuWidget::BuildGraphics(UVerticalBox* Box)
{
	UGameUserSettings* Graphics = UGameUserSettings::GetGameUserSettings();
	auto AddRow = [Box](UWidget* Row) { Box->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 4.f)); };
	// Resolution and window changes resize the editor in Play-in-Editor, so they apply in the real game only.
	auto ApplyResolution = [Graphics]()
	{
		if (GIsEditor)
		{
			Graphics->ApplyNonResolutionSettings();
		}
		else
		{
			Graphics->ApplySettings(false);
		}
	};

	const TArray<FString> Modes = { TEXT("Fullscreen"), TEXT("Borderless"), TEXT("Windowed") };
	const EWindowMode::Type Mode = Graphics->GetFullscreenMode();
	const FString CurrentMode = Mode == EWindowMode::Fullscreen ? Modes[0] : (Mode == EWindowMode::WindowedFullscreen ? Modes[1] : Modes[2]);
	AddRow(MakeComboRow(LOCTEXT("WindowMode", "Display mode"), Modes, CurrentMode, [Graphics, ApplyResolution, Modes](const FString& Value)
	{
		Graphics->SetFullscreenMode(Value == Modes[0] ? EWindowMode::Fullscreen : (Value == Modes[1] ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed));
		ApplyResolution();
	}));

	TArray<FIntPoint> Resolutions;
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
	const FIntPoint Current = Graphics->GetScreenResolution();
	Resolutions.AddUnique(Current);
	Resolutions.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X > B.X : A.Y > B.Y; });
	TArray<FString> ResolutionNames;
	for (const FIntPoint& Resolution : Resolutions)
	{
		ResolutionNames.AddUnique(ResolutionLabel(Resolution));
	}
	AddRow(MakeComboRow(LOCTEXT("Resolution", "Resolution"), ResolutionNames, ResolutionLabel(Current), [Graphics, ApplyResolution](const FString& Value)
	{
		FString Width, Height;
		if (Value.Split(TEXT(" x "), &Width, &Height))
		{
			Graphics->SetScreenResolution(FIntPoint(FCString::Atoi(*Width), FCString::Atoi(*Height)));
			ApplyResolution();
		}
	}));

	const TArray<FString> Qualities = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic"), TEXT("Cinematic") };
	TArray<FString> QualityOptions = Qualities;
	const int32 Level = Graphics->GetOverallScalabilityLevel();
	if (!Qualities.IsValidIndex(Level))
	{
		QualityOptions.Add(TEXT("Custom"));
	}
	UComboBoxString* Quality = nullptr;
	AddRow(MakeComboRow(LOCTEXT("Quality", "Quality"), QualityOptions, Qualities.IsValidIndex(Level) ? Qualities[Level] : FString(TEXT("Custom")), [Graphics, Qualities](const FString& Value)
	{
		const int32 NewLevel = Qualities.IndexOfByKey(Value);
		if (NewLevel != INDEX_NONE)
		{
			Graphics->SetOverallScalabilityLevel(NewLevel);
			Graphics->ApplyNonResolutionSettings();
		}
	}, &Quality));
	QualityCombo = Quality;

	float ScaleNormalized = 1.f, ScaleValue = 100.f, ScaleMin = 50.f, ScaleMax = 100.f;
	Graphics->GetResolutionScaleInformationEx(ScaleNormalized, ScaleValue, ScaleMin, ScaleMax);
	AddRow(MakeSliderRow(LOCTEXT("RenderScale", "Render scale"), ScaleNormalized, 0.f, 1.f, 0.05f,
		[ScaleMin, ScaleMax](float Value) { return FText::FromString(FString::Printf(TEXT("%.0f%%"), FMath::Lerp(ScaleMin, ScaleMax, Value))); },
		[Graphics](float Value) { Graphics->SetResolutionScaleNormalized(Value); Graphics->ApplyNonResolutionSettings(); }));

	AddRow(MakeCheckRow(LOCTEXT("VSync", "Vertical sync"), Graphics->IsVSyncEnabled(), [Graphics](bool bValue)
	{
		Graphics->SetVSyncEnabled(bValue);
		Graphics->ApplyNonResolutionSettings();
	}));

	TArray<FString> Limits = { TEXT("30"), TEXT("60"), TEXT("120"), TEXT("144"), TEXT("165"), TEXT("240"), TEXT("Unlimited") };
	const float Limit = Graphics->GetFrameRateLimit();
	const FString CurrentLimit = Limit <= 0.f ? FString(TEXT("Unlimited")) : FString::FromInt(FMath::RoundToInt(Limit));
	Limits.AddUnique(CurrentLimit);
	AddRow(MakeComboRow(LOCTEXT("FrameLimit", "Frame rate limit"), Limits, CurrentLimit, [Graphics](const FString& Value)
	{
		Graphics->SetFrameRateLimit(Value == TEXT("Unlimited") ? 0.f : FCString::Atof(*Value));
		Graphics->ApplyNonResolutionSettings();
	}));

	AddSpacer(Box, 10.f);
	Box->AddChildToVerticalBox(MakeButton(LOCTEXT("AutoDetect", "Auto-detect quality"), [this, Graphics, Qualities]()
	{
		Graphics->RunHardwareBenchmark();
		Graphics->ApplyHardwareBenchmarkResults();
		const int32 NewLevel = Graphics->GetOverallScalabilityLevel();
		if (QualityCombo)
		{
			QualityCombo->SetSelectedOption(Qualities.IsValidIndex(NewLevel) ? Qualities[NewLevel] : FString(TEXT("Custom")));
		}
	}));
	if (GIsEditor)
	{
		AddSpacer(Box, 8.f);
		Box->AddChildToVerticalBox(MakeText(LOCTEXT("EditorResolution", "Display mode and resolution apply in the packaged game (not in Play-in-Editor)."), Theme->BodySize - 3, Theme->DimTextColor));
	}
}

void UAGBSettingsMenuWidget::BuildAudio(UVerticalBox* Box)
{
	UAGBPlayerSettingsSubsystem* Settings = UAGBPlayerSettingsSubsystem::Get(this);
	if (!Settings)
	{
		return;
	}
	auto AddVolume = [this, Box, Settings](const FText& Label, float FAGBPlayerSettings::*Member)
	{
		Box->AddChildToVerticalBox(MakeSliderRow(Label, Settings->GetSettings().*Member, 0.f, 1.f, 0.05f, &Percent, [Settings, Member](float Value)
		{
			FAGBPlayerSettings NewSettings = Settings->GetSettings();
			NewSettings.*Member = Value;
			Settings->SetSettings(NewSettings);
		}))->SetPadding(FMargin(0.f, 4.f));
	};
	AddVolume(LOCTEXT("Master", "Master volume"), &FAGBPlayerSettings::MasterVolume);
	AddVolume(LOCTEXT("Music", "Music"), &FAGBPlayerSettings::MusicVolume);
	AddVolume(LOCTEXT("Effects", "Effects"), &FAGBPlayerSettings::EffectsVolume);
	AddVolume(LOCTEXT("Ambient", "Ambience"), &FAGBPlayerSettings::AmbientVolume);
	AddVolume(LOCTEXT("UIVolume", "Interface"), &FAGBPlayerSettings::UIVolume);
	if (GetDefault<UAGBUISettings>()->VolumeMix.IsNull())
	{
		AddSpacer(Box, 8.f);
		Box->AddChildToVerticalBox(MakeText(LOCTEXT("NoMix", "Volume groups are not set up yet (UITools.SetupUI)."), Theme->BodySize - 3, Theme->WarningColor));
	}
}

void UAGBSettingsMenuWidget::BuildControls(UVerticalBox* Box)
{
	UAGBPlayerSettingsSubsystem* Settings = UAGBPlayerSettingsSubsystem::Get(this);
	if (!Settings)
	{
		return;
	}
	auto AddRow = [Box](UWidget* Row) { Box->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 4.f)); };
	AddRow(MakeSliderRow(LOCTEXT("Sensitivity", "Mouse sensitivity"), Settings->GetSettings().MouseSensitivity, 0.1f, 3.f, 0.05f,
		[](float Value)
		{
			FNumberFormattingOptions Options;
			Options.MinimumFractionalDigits = 2;
			Options.MaximumFractionalDigits = 2;
			return FText::AsNumber(Value, &Options);
		},
		[Settings](float Value)
		{
			FAGBPlayerSettings NewSettings = Settings->GetSettings();
			NewSettings.MouseSensitivity = Value;
			Settings->SetSettings(NewSettings);
		}));
	AddRow(MakeCheckRow(LOCTEXT("InvertY", "Invert look up/down"), Settings->GetSettings().bInvertY, [Settings](bool bValue)
	{
		FAGBPlayerSettings NewSettings = Settings->GetSettings();
		NewSettings.bInvertY = bValue;
		Settings->SetSettings(NewSettings);
	}));
	AddSpacer(Box, 10.f);
	Box->AddChildToVerticalBox(MakeText(LOCTEXT("KeysTitle", "Keys (click, then press a key; Esc cancels)"), Theme->BodySize, Theme->AccentColor));
	AddSpacer(Box, 4.f);

	// Keys of the player's character; on the main menu, the default bindings with the player's changes.
	TSharedRef<TFunction<FKey(FName)>> CurrentKey = MakeShared<TFunction<FKey(FName)>>();
	*CurrentKey = [this, Settings](FName AssetName) -> FKey
	{
		const AAGBCharacter* Character = Cast<AAGBCharacter>(GetOwningPlayerPawn());
		FAGBInputSet Set;
		TArray<TObjectPtr<UInputMappingContext>> Contexts;
		if (Character)
		{
			Set = Character->Input;
			Contexts = Character->GetBoundContexts();
		}
		else
		{
			Set = AGBInput::CreateDefaultInput([this](UClass* Class, const FString& Name) -> UObject*
			{
				return NewObject<UObject>(GetTransientPackage(), Class, NAME_None, RF_Transient);
			});
			Contexts.Add(Settings->MakeBoundContext(Set.MappingContext, Set, GetTransientPackage()));
		}
		const UInputAction* Action = nullptr;
		AGBInput::ForEachAction(Set, [&Action, AssetName](TObjectPtr<UInputAction>& Candidate, const TCHAR* Name)
		{
			if (AssetName == Name)
			{
				Action = Candidate;
			}
		});
		return UAGBPlayerSettingsSubsystem::FindKey(Contexts, Action);
	};

	TSharedRef<TArray<TPair<FName, UInputKeySelector*>>> Selectors = MakeShared<TArray<TPair<FName, UInputKeySelector*>>>();
	auto Refresh = [Selectors, CurrentKey]()
	{
		for (const TPair<FName, UInputKeySelector*>& Entry : *Selectors)
		{
			Entry.Value->SetSelectedKey(FInputChord((*CurrentKey)(Entry.Key)));
		}
	};
	for (const FAGBRebindableAction& Action : UAGBPlayerSettingsSubsystem::GetRebindableActions())
	{
		UInputKeySelector* Selector = WidgetTree->ConstructWidget<UInputKeySelector>(UInputKeySelector::StaticClass());
		Selector->SetAllowGamepadKeys(false);
		Selector->SetAllowModifierKeys(false);
		Selector->SetEscapeKeys({ EKeys::Escape });
		Selector->SetKeySelectionText(LOCTEXT("PressKey", "Press a key..."));
		Selector->SetSelectedKey(FInputChord((*CurrentKey)(Action.AssetName)));
		const FName AssetName = Action.AssetName;
		UAGBUIProxy* Proxy = NewProxy();
		Proxy->Chord = [Settings, AssetName, Refresh](const FInputChord& Chord)
		{
			if (Chord.Key.IsValid())
			{
				Settings->RebindKey(AssetName, Chord.Key);
			}
			Refresh();
		};
		Selector->OnKeySelected.AddDynamic(Proxy, &UAGBUIProxy::HandleChord);
		Selectors->Add({ AssetName, Selector });
		AddRow(MakeLabeledRow(Action.Label, Selector));
	}
	AddSpacer(Box, 8.f);
	Box->AddChildToVerticalBox(MakeButton(LOCTEXT("ResetKeys", "Reset keys to default"), [Settings, Refresh]()
	{
		Settings->ResetKeyBindings();
		Refresh();
	}));
	AddSpacer(Box, 6.f);
	Box->AddChildToVerticalBox(MakeText(LOCTEXT("GamepadNote", "Movement, hotbar keys and gamepad buttons use the default layout."), Theme->BodySize - 3, Theme->DimTextColor));
}

void UAGBSettingsMenuWidget::BuildGameplay(UVerticalBox* Box)
{
	UAGBPlayerSettingsSubsystem* Settings = UAGBPlayerSettingsSubsystem::Get(this);
	if (!Settings)
	{
		return;
	}
	auto Change = [Settings](TFunction<void(FAGBPlayerSettings&)> Edit)
	{
		FAGBPlayerSettings NewSettings = Settings->GetSettings();
		Edit(NewSettings);
		Settings->SetSettings(NewSettings);
	};
	auto AddRow = [Box](UWidget* Row) { Box->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 4.f)); };
	AddRow(MakeSliderRow(LOCTEXT("FOV", "Field of view"), Settings->GetSettings().FieldOfView, 70.f, 110.f, 1.f,
		[](float Value) { return FText::AsNumber(FMath::RoundToInt(Value)); },
		[Change](float Value) { Change([Value](FAGBPlayerSettings& S) { S.FieldOfView = Value; }); }));
	AddRow(MakeSliderRow(LOCTEXT("HUDScale", "HUD size"), Settings->GetSettings().HUDScale, 0.6f, 1.6f, 0.05f, &Percent,
		[Change](float Value) { Change([Value](FAGBPlayerSettings& S) { S.HUDScale = Value; }); }));
	AddRow(MakeCheckRow(LOCTEXT("Crosshair", "Show crosshair"), Settings->GetSettings().bShowCrosshair,
		[Change](bool bValue) { Change([bValue](FAGBPlayerSettings& S) { S.bShowCrosshair = bValue; }); }));

	UEditableTextBox* Name = MakeTextField(LOCTEXT("NameHint", "Your name in multiplayer"), Settings->GetSettings().PlayerName, false);
	UAGBUIProxy* Proxy = NewProxy();
	Proxy->TextCommit = [Change](const FText& Text, ETextCommit::Type Method)
	{
		const FString Clean = UAGBNetworkSubsystem::CleanOption(Text.ToString());
		Change([Clean](FAGBPlayerSettings& S) { S.PlayerName = Clean; });
	};
	Name->OnTextCommitted.AddDynamic(Proxy, &UAGBUIProxy::HandleTextCommit);
	AddRow(MakeLabeledRow(LOCTEXT("PlayerName", "Player name"), Name));
}

// ---------------------------------------------------------------- host / join

void UAGBHostMenuWidget::BuildContent(UVerticalBox* Panel)
{
	Panel->AddChildToVerticalBox(MakeText(LOCTEXT("HostTitle", "Host a game"), Theme->HeadingSize + 8, Theme->AccentColor, true));
	Panel->AddChildToVerticalBox(MakeText(FText::Format(LOCTEXT("HostInfo", "Friends on your network join with {0}. Over the internet they need your public address and UDP port 7777 forwarded on your router."),
		FText::FromString(UAGBNetworkSubsystem::GetLocalAddress())), Theme->BodySize - 2, Theme->DimTextColor));
	AddSpacer(Panel, 14.f);

	const UAGBServerSettings* Server = GetDefault<UAGBServerSettings>();
	UEditableTextBox* Password = MakeTextField(LOCTEXT("PasswordHint", "Optional"), Server->Password, true);
	Panel->AddChildToVerticalBox(MakeLabeledRow(LOCTEXT("Password", "Password"), Password))->SetPadding(FMargin(0.f, 4.f));
	TSharedRef<int32> MaxPlayers = MakeShared<int32>(Server->MaxPlayers);
	Panel->AddChildToVerticalBox(MakeSliderRow(LOCTEXT("MaxPlayers", "Players"), static_cast<float>(*MaxPlayers), 2.f, 16.f, 1.f,
		[](float Value) { return FText::AsNumber(FMath::RoundToInt(Value)); },
		[MaxPlayers](float Value) { *MaxPlayers = FMath::RoundToInt(Value); }))->SetPadding(FMargin(0.f, 4.f));
	TSharedRef<bool> bNewWorld = MakeShared<bool>(false);
	if (UAGBMenuLibrary::HasSavedGame(this))
	{
		Panel->AddChildToVerticalBox(MakeCheckRow(LOCTEXT("NewWorld", "Start a new world (deletes the save)"), false, [bNewWorld](bool bValue) { *bNewWorld = bValue; }))->SetPadding(FMargin(0.f, 4.f));
	}
	AddSpacer(Panel, 16.f);
	Panel->AddChildToVerticalBox(MakeButton(LOCTEXT("StartHosting", "Start Hosting"), [this, Password, MaxPlayers, bNewWorld]()
	{
		if (UAGBNetworkSubsystem* Network = UAGBNetworkSubsystem::Get(this))
		{
			Network->HostGame(*bNewWorld, Password->GetText().ToString(), *MaxPlayers);
		}
	}, true))->SetPadding(FMargin(0.f, 4.f));
	Panel->AddChildToVerticalBox(MakeButton(LOCTEXT("Back", "Back"), [this]() { GoBack(); }))->SetPadding(FMargin(0.f, 4.f));
}

void UAGBJoinMenuWidget::BuildContent(UVerticalBox* Panel)
{
	Panel->AddChildToVerticalBox(MakeText(LOCTEXT("JoinTitle", "Join a game"), Theme->HeadingSize + 8, Theme->AccentColor, true));
	AddSpacer(Panel, 12.f);
	const UAGBPlayerSettingsSubsystem* Settings = UAGBPlayerSettingsSubsystem::Get(this);
	UEditableTextBox* Address = MakeTextField(LOCTEXT("AddressHint", "192.168.1.20 or name:port"), Settings ? Settings->GetSettings().LastJoinAddress : FString(), false);
	Panel->AddChildToVerticalBox(MakeLabeledRow(LOCTEXT("Address", "Address"), Address))->SetPadding(FMargin(0.f, 4.f));
	PasswordField = MakeTextField(LOCTEXT("PasswordHint", "Optional"), FString(), true);
	Panel->AddChildToVerticalBox(MakeLabeledRow(LOCTEXT("Password", "Password"), PasswordField))->SetPadding(FMargin(0.f, 4.f));
	Panel->AddChildToVerticalBox(MakeButton(LOCTEXT("Join", "Join"), [this, Address]()
	{
		if (UAGBNetworkSubsystem* Network = UAGBNetworkSubsystem::Get(this))
		{
			Network->JoinGame(Address->GetText().ToString(), PasswordField->GetText().ToString());
		}
	}, true))->SetPadding(FMargin(0.f, 6.f));

	AddSpacer(Panel, 14.f);
	Panel->AddChildToVerticalBox(MakeText(LOCTEXT("LanTitle", "Games on your network"), Theme->BodySize + 2, Theme->AccentColor));
	GameList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->AddChildToVerticalBox(GameList)->SetPadding(FMargin(0.f, 4.f));
	Panel->AddChildToVerticalBox(MakeButton(LOCTEXT("Refresh", "Refresh"), [this]() { Refresh(); }))->SetPadding(FMargin(0.f, 4.f));
	Panel->AddChildToVerticalBox(MakeButton(LOCTEXT("Back", "Back"), [this]() { GoBack(); }))->SetPadding(FMargin(0.f, 4.f));
	Refresh();
}

void UAGBJoinMenuWidget::Refresh()
{
	UAGBNetworkSubsystem* Network = UAGBNetworkSubsystem::Get(this);
	if (!GameList || !Network)
	{
		return;
	}
	GameList->ClearChildren();
	GameList->AddChildToVerticalBox(MakeText(LOCTEXT("Searching", "Searching..."), Theme->BodySize - 2, Theme->DimTextColor));
	TWeakObjectPtr<UAGBJoinMenuWidget> WeakThis(this);
	Network->FindLANGames([WeakThis](const TArray<FAGBFoundGame>& Games)
	{
		UAGBJoinMenuWidget* Self = WeakThis.Get();
		if (!Self || !Self->GameList)
		{
			return;
		}
		Self->GameList->ClearChildren();
		if (Games.IsEmpty())
		{
			Self->GameList->AddChildToVerticalBox(Self->MakeText(LOCTEXT("NoGames", "No games found. Type the host's address above."), Self->Theme->BodySize - 2, Self->Theme->DimTextColor));
			return;
		}
		for (const FAGBFoundGame& Game : Games)
		{
			const FString Address = Game.Address;
			const FText Label = FText::FromString(FString::Printf(TEXT("%s   %d/%d   %d ms"), *Game.Name, Game.Players, Game.MaxPlayers, Game.PingMs));
			Self->GameList->AddChildToVerticalBox(Self->MakeButton(Label, [WeakThis, Address]()
			{
				UAGBJoinMenuWidget* Menu = WeakThis.Get();
				UAGBNetworkSubsystem* Net = Menu ? UAGBNetworkSubsystem::Get(Menu) : nullptr;
				if (Net)
				{
					Net->JoinGame(Address, Menu->PasswordField ? Menu->PasswordField->GetText().ToString() : FString());
				}
			}))->SetPadding(FMargin(0.f, 2.f));
		}
	});
}

// ---------------------------------------------------------------- chat line

TSharedRef<SWidget> UAGBChatWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Root;
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Size->SetWidthOverride(560.f);
		Input = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("ChatInput"));
		Input->SetHintText(LOCTEXT("ChatHint", "Say something (Enter to send, Esc to cancel)"));
		Size->SetContent(Input);
		UOverlaySlot* InputSlot = Root->AddChildToOverlay(Size);
		InputSlot->SetHorizontalAlignment(HAlign_Left);
		InputSlot->SetVerticalAlignment(VAlign_Bottom);
		InputSlot->SetPadding(FMargin(18.f, 0.f, 0.f, 220.f));
		Proxy = NewObject<UAGBUIProxy>(this);
		TWeakObjectPtr<UAGBChatWidget> WeakThis(this);
		Proxy->TextCommit = [WeakThis](const FText& Text, ETextCommit::Type Method)
		{
			UAGBChatWidget* Self = WeakThis.Get();
			AAGBPlayerController* Player = Self ? Cast<AAGBPlayerController>(Self->GetOwningPlayer()) : nullptr;
			if (!Player)
			{
				return;
			}
			if (Method == ETextCommit::OnEnter && !Text.IsEmpty())
			{
				Player->ServerSay(Text.ToString());
			}
			Player->CloseChat();
		};
		Input->OnTextCommitted.AddDynamic(Proxy, &UAGBUIProxy::HandleTextCommit);
	}
	return Super::RebuildWidget();
}

void UAGBChatWidget::FocusInput()
{
	if (Input)
	{
		Input->SetKeyboardFocus();
	}
}

// ---------------------------------------------------------------- confirm

void UAGBConfirmWidget::BuildContent(UVerticalBox* Panel)
{
	Panel->AddChildToVerticalBox(MakeText(Message, Theme->BodySize + 2, Theme->TextColor));
	AddSpacer(Panel, 20.f);
	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UButton* No = MakeButton(LOCTEXT("Cancel", "Cancel"), [this]() { GoBack(); });
	UButton* Yes = MakeButton(YesLabel.IsEmpty() ? LOCTEXT("Yes", "Yes") : YesLabel, [this]()
	{
		TFunction<void()> Action = OnYes;
		GoBack();
		if (Action)
		{
			Action();
		}
	}, true);
	UHorizontalBoxSlot* NoSlot = Buttons->AddChildToHorizontalBox(No);
	NoSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	NoSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
	Buttons->AddChildToHorizontalBox(Yes)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Panel->AddChildToVerticalBox(Buttons);
}

// ---------------------------------------------------------------- menu stack

UAGBMenuSubsystem* UAGBMenuSubsystem::Get(const APlayerController* Player)
{
	const ULocalPlayer* LocalPlayer = Player ? Player->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UAGBMenuSubsystem>() : nullptr;
}

APlayerController* UAGBMenuSubsystem::GetPlayer() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer<ULocalPlayer>();
	return LocalPlayer ? LocalPlayer->PlayerController.Get() : nullptr;
}

void UAGBMenuSubsystem::Prune()
{
	// Level changes remove every widget from the screen.
	Stack.RemoveAll([](const TWeakObjectPtr<UUserWidget>& Widget) { return !Widget.IsValid() || !Widget->IsInViewport(); });
	if (Stack.IsEmpty())
	{
		bPausedGame = false;
	}
}

UUserWidget* UAGBMenuSubsystem::OpenMenu(TSubclassOf<UUserWidget> WidgetClass)
{
	APlayerController* Player = GetPlayer();
	return Player && WidgetClass ? Push(CreateWidget<UUserWidget>(Player, WidgetClass)) : nullptr;
}

UUserWidget* UAGBMenuSubsystem::Push(UUserWidget* Widget)
{
	Prune();
	if (!Widget)
	{
		return nullptr;
	}
	if (Stack.Num() > 0)
	{
		Stack.Last()->SetVisibility(ESlateVisibility::Collapsed);
	}
	Widget->AddToViewport(100 + Stack.Num());
	Stack.Add(Widget);
	if (UAGBAudioFXSubsystem* Audio = UAGBAudioFXSubsystem::Get(Widget))
	{
		Audio->PlayCue2D(TEXT("UI.Open"));
	}
	UpdateInputMode();
	return Widget;
}

void UAGBMenuSubsystem::OpenMainMenu()
{
	TSubclassOf<UUserWidget> Class = GetDefault<UAGBUISettings>()->MainMenuClass.LoadSynchronous();
	OpenMenu(Class ? Class : TSubclassOf<UUserWidget>(UAGBMainMenuWidget::StaticClass()));
}

void UAGBMenuSubsystem::OpenPauseMenu()
{
	if (IsMenuOpen())
	{
		return;
	}
	TSubclassOf<UUserWidget> Class = GetDefault<UAGBUISettings>()->PauseMenuClass.LoadSynchronous();
	OpenMenu(Class ? Class : TSubclassOf<UUserWidget>(UAGBPauseMenuWidget::StaticClass()));
}

void UAGBMenuSubsystem::OpenSettings()
{
	TSubclassOf<UUserWidget> Class = GetDefault<UAGBUISettings>()->SettingsMenuClass.LoadSynchronous();
	OpenMenu(Class ? Class : TSubclassOf<UUserWidget>(UAGBSettingsMenuWidget::StaticClass()));
}

void UAGBMenuSubsystem::Confirm(const FText& Message, const FText& YesLabel, TFunction<void()> OnYes)
{
	APlayerController* Player = GetPlayer();
	if (UAGBConfirmWidget* Widget = Player ? CreateWidget<UAGBConfirmWidget>(Player, UAGBConfirmWidget::StaticClass()) : nullptr)
	{
		Widget->Message = Message;
		Widget->YesLabel = YesLabel;
		Widget->OnYes = MoveTemp(OnYes);
		Push(Widget);
	}
}

void UAGBMenuSubsystem::CloseTop()
{
	Prune();
	if (Stack.IsEmpty())
	{
		return;
	}
	UUserWidget* Closing = Stack.Pop().Get();
	if (UAGBAudioFXSubsystem* Audio = UAGBAudioFXSubsystem::Get(Closing))
	{
		Audio->PlayCue2D(TEXT("UI.Close"));
	}
	Closing->RemoveFromParent();
	if (Stack.Num() > 0)
	{
		Stack.Last()->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	UpdateInputMode();
}

void UAGBMenuSubsystem::CloseAll()
{
	Prune();
	for (const TWeakObjectPtr<UUserWidget>& Widget : Stack)
	{
		Widget->RemoveFromParent();
	}
	Stack.Reset();
	UpdateInputMode();
}

bool UAGBMenuSubsystem::IsMenuOpen() const
{
	for (const TWeakObjectPtr<UUserWidget>& Widget : Stack)
	{
		if (Widget.IsValid() && Widget->IsInViewport())
		{
			return true;
		}
	}
	return false;
}

void UAGBMenuSubsystem::UpdateInputMode()
{
	APlayerController* Player = GetPlayer();
	if (!Player)
	{
		return;
	}
	if (Stack.Num() > 0)
	{
		FInputModeUIOnly Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Player->SetInputMode(Mode);
		Player->SetShowMouseCursor(true);
		if (UAGBMenuWidget* Menu = Cast<UAGBMenuWidget>(Stack.Last().Get()))
		{
			Menu->FocusFirst();
		}
		else
		{
			Stack.Last()->SetKeyboardFocus();
		}
		// Only a single-player game stops; multiplayer worlds keep running.
		if (!bPausedGame && Player->GetNetMode() == NM_Standalone && !Player->IsA<AAGBMenuPlayerController>())
		{
			bPausedGame = Player->SetPause(true);
		}
		return;
	}
	if (bPausedGame)
	{
		Player->SetPause(false);
		bPausedGame = false;
	}
	Player->SetInputMode(FInputModeGameOnly());
	Player->SetShowMouseCursor(false);
}

// ---------------------------------------------------------------- library

bool UAGBMenuLibrary::HasMainMenuMap()
{
	return !GetDefault<UAGBUISettings>()->MainMenuMap.IsNull();
}

bool UAGBMenuLibrary::HasGameMap()
{
	return !GetDefault<UAGBUISettings>()->GameMap.IsNull();
}

TArray<FString> UAGBMenuLibrary::FindGameSaves()
{
	TArray<FString> Saves;
	if (!HasGameMap())
	{
		return Saves;
	}
	// Saves are "<Slot>_<Map>.sav" (see UAGBSaveSubsystem::GetSlotName).
	const FString MapName = FPackageName::GetShortName(GetDefault<UAGBUISettings>()->GameMap.ToSoftObjectPath().GetLongPackageName());
	const FString Folder = FPaths::ProjectSavedDir() / TEXT("SaveGames");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Folder / FString::Printf(TEXT("*_%s.sav"), *MapName)), true, false);
	for (const FString& File : Files)
	{
		Saves.Add(FPaths::GetBaseFilename(File));
	}
	return Saves;
}

bool UAGBMenuLibrary::HasSavedGame(const UObject* WorldContext)
{
	return FindGameSaves().Num() > 0;
}

bool UAGBMenuLibrary::ContinueGame(const UObject* WorldContext)
{
	if (!HasGameMap())
	{
		return false;
	}
	UGameplayStatics::OpenLevelBySoftObjectPtr(WorldContext, GetDefault<UAGBUISettings>()->GameMap);
	return true;
}

bool UAGBMenuLibrary::NewGame(const UObject* WorldContext)
{
	if (!HasGameMap())
	{
		return false;
	}
	for (const FString& Save : FindGameSaves())
	{
		UGameplayStatics::DeleteGameInSlot(Save, 0);
	}
	UGameplayStatics::OpenLevelBySoftObjectPtr(WorldContext, GetDefault<UAGBUISettings>()->GameMap);
	return true;
}

bool UAGBMenuLibrary::QuitToMainMenu(const UObject* WorldContext)
{
	if (!HasMainMenuMap())
	{
		return false;
	}
	UGameplayStatics::OpenLevelBySoftObjectPtr(WorldContext, GetDefault<UAGBUISettings>()->MainMenuMap);
	return true;
}

void UAGBMenuLibrary::QuitGame(const UObject* WorldContext)
{
	UKismetSystemLibrary::QuitGame(WorldContext, FirstLocalPlayer(WorldContext), EQuitPreference::Quit, false);
}

void UAGBMenuLibrary::SaveGame(const UObject* WorldContext)
{
	if (AAGBPlayerController* Player = Cast<AAGBPlayerController>(FirstLocalPlayer(WorldContext)))
	{
		Player->AGBSave();
	}
}

void UAGBMenuLibrary::OpenSettings(const UObject* WorldContext)
{
	if (UAGBMenuSubsystem* Menus = UAGBMenuSubsystem::Get(FirstLocalPlayer(WorldContext)))
	{
		Menus->OpenSettings();
	}
}

void UAGBMenuLibrary::CloseMenu(const UObject* WorldContext)
{
	if (UAGBMenuSubsystem* Menus = UAGBMenuSubsystem::Get(FirstLocalPlayer(WorldContext)))
	{
		Menus->CloseTop();
	}
}

// ---------------------------------------------------------------- menu level

AAGBMenuGameMode::AAGBMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AAGBMenuPlayerController::StaticClass();
}

void AAGBMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		SetViewTarget(*It);
		break;
	}
	if (UAGBPlayerSettingsSubsystem* Settings = UAGBPlayerSettingsSubsystem::Get(this))
	{
		Settings->ApplyAudio(GetWorld());
	}
	if (UAGBMenuSubsystem* Menus = UAGBMenuSubsystem::Get(this))
	{
		Menus->OpenMainMenu();
	}
}

#undef LOCTEXT_NAMESPACE
