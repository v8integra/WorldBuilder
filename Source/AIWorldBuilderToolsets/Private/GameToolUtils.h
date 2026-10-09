#pragma once

#include "CoreMinimal.h"

class AAGBCharacter;
class AGameModeBase;
class UBlueprint;
class UWorld;
struct FHitResult;

/** Helpers shared by the AI Game Builder editor toolsets. */
namespace GameToolUtils
{
	/** Where a standing player lands at a point (m): traced on the player's collision channel from above all landscapes. */
	bool TraceGround(UWorld* World, double XM, double YM, FHitResult& OutHit);

	/** Requested label, or Fallback for "auto"/empty, made unique among the level's actor labels. */
	FString UniqueLabel(UWorld* World, const FString& Requested, const FString& Fallback);

	/** The game mode the open level plays with: its World Settings override, else the project default. */
	UClass* GetActiveGameModeClass(UWorld* World);

	/** The AI Game Builder player character Blueprint used by the active game mode, or nullptr. */
	UBlueprint* GetCharacterBlueprint(UWorld* World);
}
