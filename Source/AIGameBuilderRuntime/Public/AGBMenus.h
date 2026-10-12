#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Commands/InputChord.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Templates/Function.h"
#include "Types/SlateEnums.h"

#include "AGBMenus.generated.h"

class UAGBUITheme;
class UButton;
class UComboBoxString;
class UPanelWidget;
class UTextBlock;
class UVerticalBox;
class UWidget;

/** Forwards UMG events (dynamic delegates) to C++ lambdas for menus built in code. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBUIProxy : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void()> Click;
	TFunction<void(float)> Float;
	TFunction<void(bool)> Bool;
	TFunction<void(const FString&)> String;
	TFunction<void(const FInputChord&)> Chord;

	UFUNCTION()
	void HandleClick();

	UFUNCTION()
	void HandleHover();

	UFUNCTION()
	void HandleFloat(float Value) { if (Float) { Float(Value); } }

	UFUNCTION()
	void HandleBool(bool bValue) { if (Bool) { Bool(bValue); } }

	UFUNCTION()
	void HandleString(FString Value, ESelectInfo::Type SelectionType) { if (String && SelectionType != ESelectInfo::Direct) { String(Value); } }

	UFUNCTION()
	void HandleChord(FInputChord Value) { if (Chord) { Chord(Value); } }

	TFunction<void(const FText&, ETextCommit::Type)> TextCommit;

	UFUNCTION()
	void HandleTextCommit(const FText& Text, ETextCommit::Type Method) { if (TextCommit) { TextCommit(Text, Method); } }
};

/** Base of the built-in menus: builds its widgets in code from the UI theme. Escape / gamepad B goes back. */
UCLASS(Abstract)
class AIGAMEBUILDERRUNTIME_API UAGBMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Escape / B: close this menu (the main menu stays). */
	virtual void GoBack();

	/** Puts keyboard/gamepad focus on the first button. */
	void FocusFirst();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** Fills the centered panel. */
	virtual void BuildContent(UVerticalBox* Panel) {}

	/** Full-screen background: the theme's menu background image (main menu) or a dim backdrop. */
	virtual bool UsesMenuBackground() const { return false; }

	/** Panel width in slate units. */
	virtual float GetPanelWidth() const { return 520.f; }

	// Widget helpers (styled by the theme).
	UTextBlock* MakeText(const FText& Text, int32 Size, const FLinearColor& Color, bool bTitleFont = false);
	UButton* MakeButton(const FText& Label, TFunction<void()> OnClick, bool bAccent = false);
	UWidget* MakeSliderRow(const FText& Label, float Value, float Min, float Max, float Step, TFunction<FText(float)> Format, TFunction<void(float)> OnChanged);
	UWidget* MakeCheckRow(const FText& Label, bool bValue, TFunction<void(bool)> OnChanged);
	UWidget* MakeComboRow(const FText& Label, const TArray<FString>& Options, const FString& Selected, TFunction<void(const FString&)> OnChanged, UComboBoxString** OutCombo = nullptr);
	UWidget* MakeLabeledRow(const FText& Label, UWidget* Control);
	void AddSpacer(UPanelWidget* Panel, float Height);
	class UEditableTextBox* MakeTextField(const FText& Hint, const FString& Initial, bool bPassword);
	UAGBUIProxy* NewProxy();

	const UAGBUITheme* Theme = nullptr;

	/** Gets keyboard/gamepad focus when the menu opens. */
	UPROPERTY(Transient)
	TObjectPtr<UWidget> FirstFocus;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAGBUIProxy>> Proxies;
};

UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBMainMenuWidget : public UAGBMenuWidget
{
	GENERATED_BODY()

public:
	virtual void GoBack() override {}

protected:
	virtual void BuildContent(UVerticalBox* Panel) override;
	virtual bool UsesMenuBackground() const override { return true; }
	virtual float GetPanelWidth() const override { return 420.f; }
};

UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBPauseMenuWidget : public UAGBMenuWidget
{
	GENERATED_BODY()

protected:
	virtual void BuildContent(UVerticalBox* Panel) override;
	virtual float GetPanelWidth() const override { return 420.f; }
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
};

/** Graphics, audio, controls (key rebinding) and gameplay settings. Changes apply at once and are saved on leaving. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBSettingsMenuWidget : public UAGBMenuWidget
{
	GENERATED_BODY()

public:
	virtual void GoBack() override;

protected:
	virtual void BuildContent(UVerticalBox* Panel) override;
	virtual float GetPanelWidth() const override { return 760.f; }

private:
	void BuildGraphics(UVerticalBox* Box);
	void BuildAudio(UVerticalBox* Box);
	void BuildControls(UVerticalBox* Box);
	void BuildGameplay(UVerticalBox* Box);

	UPROPERTY(Transient)
	TObjectPtr<UComboBoxString> QualityCombo;
};

/** Host a game: friends join this computer (password, player limit, new world). */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBHostMenuWidget : public UAGBMenuWidget
{
	GENERATED_BODY()

protected:
	virtual void BuildContent(UVerticalBox* Panel) override;
};

/** Join a game: by address, or from the games found on the local network. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBJoinMenuWidget : public UAGBMenuWidget
{
	GENERATED_BODY()

protected:
	virtual void BuildContent(UVerticalBox* Panel) override;
	virtual float GetPanelWidth() const override { return 620.f; }

private:
	void Refresh();

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> GameList;

	UPROPERTY(Transient)
	TObjectPtr<class UEditableTextBox> PasswordField;
};

/** The chat line (Enter): type and press Enter to send, Escape to cancel. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBChatWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void FocusInput();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<class UEditableTextBox> Input;

	UPROPERTY(Transient)
	TObjectPtr<UAGBUIProxy> Proxy;
};

/** Yes/No question. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBConfirmWidget : public UAGBMenuWidget
{
	GENERATED_BODY()

public:
	FText Message;
	FText YesLabel;
	TFunction<void()> OnYes;

protected:
	virtual void BuildContent(UVerticalBox* Panel) override;
};

/** The open menus of one player (a stack: settings over pause over the game). Pauses single-player games. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBMenuSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	static UAGBMenuSubsystem* Get(const APlayerController* Player);

	UUserWidget* OpenMenu(TSubclassOf<UUserWidget> WidgetClass);
	void OpenMainMenu();
	void OpenPauseMenu();
	void OpenSettings();
	void Confirm(const FText& Message, const FText& YesLabel, TFunction<void()> OnYes);

	/** Closes the top menu (returns to the one below, or to the game). */
	void CloseTop();
	void CloseAll();
	bool IsMenuOpen() const;

private:
	UUserWidget* Push(UUserWidget* Widget);
	void Prune();
	void UpdateInputMode();
	APlayerController* GetPlayer() const;

	TArray<TWeakObjectPtr<UUserWidget>> Stack;
	bool bPausedGame = false;
};

/** Menu actions for the built-in menus and for your own Widget Blueprints. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API UAGBMenuLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Whether the game level has a save to continue. */
	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Menu", meta = (WorldContext = "WorldContext"))
	static bool HasSavedGame(const UObject* WorldContext);

	/** Opens the game level (its save loads automatically). */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Menu", meta = (WorldContext = "WorldContext"))
	static bool ContinueGame(const UObject* WorldContext);

	/** Deletes the game level's save and opens it fresh. */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Menu", meta = (WorldContext = "WorldContext"))
	static bool NewGame(const UObject* WorldContext);

	/** Back to the main menu level (the world saves on the way out). */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Menu", meta = (WorldContext = "WorldContext"))
	static bool QuitToMainMenu(const UObject* WorldContext);

	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Menu", meta = (WorldContext = "WorldContext"))
	static void QuitGame(const UObject* WorldContext);

	/** Saves the world now (host/single player). */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Menu", meta = (WorldContext = "WorldContext"))
	static void SaveGame(const UObject* WorldContext);

	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Menu", meta = (WorldContext = "WorldContext"))
	static void OpenSettings(const UObject* WorldContext);

	/** Closes the top menu. */
	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Menu", meta = (WorldContext = "WorldContext"))
	static void CloseMenu(const UObject* WorldContext);

	static bool HasMainMenuMap();
	static bool HasGameMap();
	static TArray<FString> FindGameSaves();
};

/** Game mode of the main menu level: no pawn, the menu controller shows the main menu. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AAGBMenuGameMode();
};

/** Shows the main menu; looks through the first camera placed in the menu level (a scenic backdrop), if any. */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
};
