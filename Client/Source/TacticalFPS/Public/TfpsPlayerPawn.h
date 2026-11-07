// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"

#include "TfpsPlayerPawn.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
class UTfpsNetSubsystem;

/**
 * The local player's first-person view. Has no movement logic of its own: its
 * transform comes from the NetCore predicted state (interpolated between
 * fixed simulation ticks for smooth rendering at any frame rate).
 */
UCLASS(Config = Game)
class TACTICALFPS_API ATfpsPlayerPawn : public APawn
{
	GENERATED_BODY()

public:
	ATfpsPlayerPawn();

	/** Called by the controller after the network pump each frame. */
	void UpdateFromNetwork(const UTfpsNetSubsystem& Net, const FRotator& ViewRotation, float DeltaTime);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	/** PLACEHOLDER first-person weapon (engine cube) until weapon art exists. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UStaticMeshComponent> ViewWeapon;

	/** Horizontal field of view in degrees. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float FieldOfView = 90.0f;

	/** Field of view multiplier at full ADS. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float AdsFovMultiplier = 0.78f;

	/** Camera roll at full lean (degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float LeanRollDegrees = 9.0f;

	/** How fast the eye height follows stance changes (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float EyeHeightSmoothing = 14.0f;

	/** Visual-only smoothing of reconciliation corrections (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CorrectionSmoothing = 18.0f;

protected:
	virtual void BeginPlay() override;

private:
	float CurrentEyeHeight = 165.0f;
	float WeaponKick = 0.0f;
	uint32 LastShotCounter = 0;
	FVector LastNetLocation = FVector::ZeroVector;
	FVector CorrectionOffset = FVector::ZeroVector;
	bool bHasLocation = false;
	FVector DroneCamera = FVector::ZeroVector;
	bool bDroneCamera = false;
};
