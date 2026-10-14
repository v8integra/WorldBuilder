#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "PublishTools.generated.h"

/// A saved camera view for store screenshots and the trailer tour.
USTRUCT(BlueprintType)
struct FAGBPhotoSpot
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Store")
	FTransform Camera;

	/// Time of day (0-24); negative = whatever time it is.
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Store")
	float Hour = -1.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Store")
	FString Label;
};

/// Photo spots saved for the store kit (Config/DefaultGame.ini).
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "AI Game Builder Store Kit"))
class UAGBStoreKitSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Store")
	TArray<FAGBPhotoSpot> PhotoSpots;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};

/// Result of the publishing tools.
USTRUCT(BlueprintType)
struct FPublishToolsResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString Message;

	/// Details, progress or checklist lines.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Lines;
};

/// Packaging and publishing AI Game Builder games: prepares and builds the Windows game (cook, build, package, in the background), takes store screenshots in the game, makes store capsule images, creates a trailer camera tour, and keeps a release checklist. Store accounts, payments, legal forms and submission are the user's: prepare everything and say exactly what to click. Store text: GameDesignTools.get_store_kit_template / save_store_page.
UCLASS(BlueprintType, Hidden)
class UPublishTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Gets the project ready to package: game name, version, company, description; the main menu and game level in
	 * the cook list; the game's data folders always cooked (they are found at runtime, not referenced); crash
	 * reporter; build configuration. Empty = unchanged.
	 * @param GameName Name shown in the window title and store.
	 * @param Version Version like "0.1.0" (raise it for every release).
	 * @param CompanyName Developer name.
	 * @param Description One line about the game.
	 * @param Configuration "Development" (test builds, logs and console) or "Shipping" (release).
	 * @return Settings now.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Publish")
	static FPublishToolsResult PreparePackaging(const FString& GameName = TEXT(""), const FString& Version = TEXT(""), const FString& CompanyName = TEXT(""),
		const FString& Description = TEXT(""), const FString& Configuration = TEXT(""));

	/**
	 * Packages the game for Windows in the background (compile, cook, pak, archive): takes 5-60 minutes the first
	 * time. Poll GetPackagingStatus. The editor stays usable but slower; saved changes only.
	 * @param Configuration "Development" or "Shipping" ("" = the prepared one).
	 * @param OutputFolder Folder for the finished game ("" = <Project>/Packaged/<Configuration>).
	 * @return Started or why not.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Publish")
	static FPublishToolsResult PackageGame(const FString& Configuration = TEXT(""), const FString& OutputFolder = TEXT(""));

	/**
	 * Progress of packaging: running/succeeded/failed, elapsed time, errors, recent output, and when done the game's
	 * folder, executable and size.
	 * @return Status lines.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Publish")
	static FPublishToolsResult GetPackagingStatus();

	/**
	 * Stops a running packaging job.
	 * @return Confirmation.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Publish")
	static FPublishToolsResult CancelPackaging();

	/**
	 * Saves the editor viewport's current view as a photo spot (the user flies the camera to a nice view first).
	 * @param Label Short name ("Lake at sunset").
	 * @param Hour Time of day for the photo (0-24; negative = default by shot).
	 * @return All photo spots.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Publish")
	static FPublishToolsResult AddPhotoSpot(const FString& Label = TEXT(""), double Hour = -1.0);

	/**
	 * Removes all saved photo spots.
	 * @return Confirmation.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Publish")
	static FPublishToolsResult ClearPhotoSpots();

	/**
	 * Takes store screenshots in the game (Play starts by itself, HUD hidden, clear weather, chosen time of day) at
	 * the saved photo spots plus automatic ones (overlook, lake shore, the base, wildlife, sunset, night by the fire).
	 * Saved to Saved/StoreKit/Screenshots. Poll PlaytestTools.GetPlaytestStatus.
	 * @param Count Number of screenshots (saved spots first).
	 * @param Resolution "1920x1080" (Steam) or "3840x2160".
	 * @return Started, with the spots.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Publish")
	static FPublishToolsResult CaptureStoreScreenshots(int32 Count = 8, const FString& Resolution = TEXT("1920x1080"));

	/**
	 * Crops a screenshot to every Steam capsule size (header, small, main, vertical, library, hero) into
	 * Saved/StoreKit/Capsules, as backgrounds for the game's logo (the user adds the title text).
	 * @param ScreenshotFile File in Saved/StoreKit/Screenshots ("" = the first).
	 * @return Images made.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Publish")
	static FPublishToolsResult MakeStoreImages(const FString& ScreenshotFile = TEXT(""));

	/**
	 * Creates (or updates) a camera tour through the photo spots for recording a trailer: in the game type "AGBTour" in
	 * the console (` key) and record the screen (Win+Alt+R). Save the level afterwards.
	 * @param SecondsPerShot Seconds between spots.
	 * @param Hour Time of day for the tour (negative = keep).
	 * @return The tour.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Publish")
	static FPublishToolsResult CreateTrailerTour(double SecondsPerShot = 6.0, double Hour = 17.5);

	/**
	 * Release checklist: automatic checks (data, packaging, version, playtests, performance, store kit, audio) and the
	 * steps only the user can do (store account, payments, rating, legal, testing on another PC, upload).
	 * @return Checklist lines ([x] done, [ ] to do, [!] problem, [user] the user's step).
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Publish")
	static FPublishToolsResult GetReleaseChecklist();
};
