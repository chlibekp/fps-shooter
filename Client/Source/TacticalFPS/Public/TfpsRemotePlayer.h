// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "TfpsRemotePlayer.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * Visual representation of another player, driven by interpolated server
 * snapshots. PLACEHOLDER body (engine shapes) until character art exists.
 */
UCLASS()
class TACTICALFPS_API ATfpsRemotePlayer : public AActor
{
	GENERATED_BODY()

public:
	ATfpsRemotePlayer();

	/** Applies the interpolated state (meters / degrees). */
	void ApplyState(const FVector& FeetMeters, float YawDeg, bool bCrouched, const FString& Name, uint8 Team, bool bInterrupted);

	UPROPERTY(VisibleAnywhere, Category = "Visual") TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, Category = "Visual") TObjectPtr<UStaticMeshComponent> Head;
	UPROPERTY(VisibleAnywhere, Category = "Visual") TObjectPtr<UStaticMeshComponent> Visor;
	UPROPERTY(VisibleAnywhere, Category = "Visual") TObjectPtr<UTextRenderComponent> NameTag;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	float VisualHeight = 180.0f;
	float TargetHeight = 180.0f;
	int32 AppliedTeam = -1;
};
