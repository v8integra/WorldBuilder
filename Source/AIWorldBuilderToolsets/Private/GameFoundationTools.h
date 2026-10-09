#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "GameFoundationTools.generated.h"

/// Result of the game foundation tools, including the project's current game setup.
USTRUCT(BlueprintType)
struct FGameFoundationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString Message;

	/// Assets created or updated.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FString> Assets;

	/// Project-wide default game mode (Project Settings > Maps & Modes).
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString ProjectGameMode;

	/// Game mode override of the open level (World Settings), empty if none.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString LevelGameMode;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString CurrentLevel;

	/// Map the packaged game starts in.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString GameDefaultMap;

	/// Map the editor opens on start.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString EditorStartupMap;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	int32 PlayerStartCount = 0;

	/// Player start locations (m).
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	TArray<FVector> PlayerStartsM;

	/// Whether the project uses Enhanced Input (required).
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	bool bEnhancedInput = false;

	/// Character Blueprint in use, its model and default camera.
	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString CharacterBlueprint;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString CharacterMesh;

	UPROPERTY(BlueprintReadOnly, Category = "AIGameBuilder")
	FString DefaultCameraMode;
};

/// Game foundation for AI Game Builder games: creates the game mode, player character (third/first person, Enhanced Input, sprint, crouch, interaction) and player controller as editable Blueprints on the plugin's replication-ready runtime classes, makes them the project/level defaults, places player starts and sets startup maps. Run SetupGameFoundation once per project (again to change perspective or character model). Units: meters. Save all afterwards.
UCLASS(BlueprintType, Hidden)
class UGameFoundationTools : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override;

	/**
	 * Creates (or updates) the game's core Blueprints in Folder: BP_AGB_GameMode, BP_AGB_Character, BP_AGB_PlayerController,
	 * plus Enhanced Input assets in Folder/Input (IA_AGB_* and IMC_AGB_Default; existing input assets are kept so user rebinds survive).
	 * Sets the game mode as the project default (optional) and as the open level's override. Saves the assets.
	 * Example: SetupGameFoundation("ThirdPerson") for a survival game with a switchable camera.
	 * @param Perspective "ThirdPerson" or "FirstPerson": the starting camera (players switch with V).
	 * @param CharacterMeshPath Skeletal mesh for the player, "auto" (finds the UE mannequin SKM_Manny_Simple/SKM_Manny if the project has it, otherwise keeps the current one or the placeholder body), or "none" for the placeholder body.
	 * @param AnimBlueprintPath Animation Blueprint for that mesh, or "auto" (finds ABP_Unarmed/ABP_Manny with the same skeleton).
	 * @param Folder Content folder for the Blueprints.
	 * @param bSetProjectDefault Also make the game mode the project-wide default (Project Settings > Maps & Modes).
	 * @param bAllowCameraToggle Whether players may switch camera with V.
	 * @return Created assets and the resulting setup.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Foundation")
	static FGameFoundationResult SetupGameFoundation(const FString& Perspective = TEXT("ThirdPerson"), const FString& CharacterMeshPath = TEXT("auto"),
		const FString& AnimBlueprintPath = TEXT("auto"), const FString& Folder = TEXT("/Game/AIGameBuilder/Core"), bool bSetProjectDefault = true,
		bool bAllowCameraToggle = true);

	/**
	 * Places a player start on the ground (traced like the player would land) at a point. One undo step.
	 * Example: PlacePlayerStart(120, -340, 90) spawns players there facing +Y.
	 * @param XM World X, meters.
	 * @param YM World Y, meters.
	 * @param YawDeg Facing direction, degrees (0 = +X, 90 = +Y).
	 * @param bReplaceExisting Remove the level's other player starts first.
	 * @return The player starts in the level afterwards.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Foundation")
	static FGameFoundationResult PlacePlayerStart(double XM, double YM, double YawDeg = 0.0, bool bReplaceExisting = true);

	/**
	 * Places a lamp post the player can switch on and off with the interact key: a ready-made test of the interaction
	 * system (and a working example for Blueprint authors). Replicated. One undo step.
	 * @param XM World X, meters.
	 * @param YM World Y, meters.
	 * @param Label Outliner label, or "auto".
	 * @return Confirmation.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Foundation")
	static FGameFoundationResult SpawnInteractableLight(double XM, double YM, const FString& Label = TEXT("auto"));

	/**
	 * Sets which map the packaged game starts in and/or which map the editor opens. The level must be saved.
	 * @param MapPath Map package path (e.g. "/Game/Maps/Island"), or "current" for the open level.
	 * @param bGameDefault Set Game Default Map.
	 * @param bEditorStartup Set Editor Startup Map.
	 * @return The project's map settings.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Foundation")
	static FGameFoundationResult SetStartupMap(const FString& MapPath = TEXT("current"), bool bGameDefault = true, bool bEditorStartup = true);

	/**
	 * Reports the game setup: project and level game modes, startup maps, player starts, Enhanced Input, character Blueprint, model and camera.
	 * @return The setup and what is missing.
	 */
	UFUNCTION(meta = (AICallable), Category = "AIGameBuilder|Foundation")
	static FGameFoundationResult GetGameFoundationStatus();
};
