#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AGBCameraTour.generated.h"

class APlayerController;
class UCameraComponent;

/**
 * A camera flight through the world for trailers: glides smoothly through its points with the HUD hidden. Start it
 * in the game with the console command "AGBTour" (again to stop) and record the screen (Win+Alt+R, OBS...).
 * Created by PublishTools.CreateTrailerTour from the photo spots.
 */
UCLASS()
class AIGAMEBUILDERRUNTIME_API AAGBCameraTour : public AActor
{
	GENERATED_BODY()

public:
	AAGBCameraTour();

	/** Camera positions and directions, in order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Tour")
	TArray<FTransform> Points;

	/** Seconds from one point to the next. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Tour", meta = (ClampMin = "1"))
	float SecondsPerShot = 6.f;

	/** Time of day for the tour (0-24); negative = keep the current time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Tour")
	float Hour = 17.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI Game Builder|Tour")
	float FieldOfView = 70.f;

	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Tour")
	void Play(APlayerController* Player);

	UFUNCTION(BlueprintCallable, Category = "AI Game Builder|Tour")
	void Stop();

	UFUNCTION(BlueprintPure, Category = "AI Game Builder|Tour")
	bool IsPlaying() const { return Viewer.IsValid(); }

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "AI Game Builder|Tour")
	TObjectPtr<UCameraComponent> Camera;

	TWeakObjectPtr<APlayerController> Viewer;
	float Elapsed = 0.f;
};
