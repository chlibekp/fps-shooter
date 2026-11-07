// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

#include "TfpsPlayerController.generated.h"

/** Rebindable keys (saved in the user's Input config). */
USTRUCT(BlueprintType)
struct FTfpsKeyBindings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey Forward = EKeys::W;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey Back = EKeys::S;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey Left = EKeys::A;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey Right = EKeys::D;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey Jump = EKeys::SpaceBar;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey Crouch = EKeys::LeftControl;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey CrouchAlt = EKeys::C;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey Sprint = EKeys::LeftShift;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey Walk = EKeys::LeftAlt;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey LeanLeft = EKeys::Q;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey LeanRight = EKeys::E;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey Aim = EKeys::RightMouseButton;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey Fire = EKeys::LeftMouseButton;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey Prone = EKeys::Z;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey Reload = EKeys::R;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey Use = EKeys::F;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey PrimaryWeapon = EKeys::One;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey SecondaryWeapon = EKeys::Two;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey NextWeapon = EKeys::MouseScrollDown;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey PrevWeapon = EKeys::MouseScrollUp;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey ToggleNetStats = EKeys::F3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey ReleaseMouse = EKeys::Escape;
};

/**
 * Samples keyboard/mouse every frame into a protocol MoveInput, drives the
 * network pump and then updates the local pawn's view. Only inputs are sent
 * to the server; the server decides where the player actually is.
 *
 * Command line: -connect=host:port -name=PlayerName -map=test_arena
 */
UCLASS(Config = Input)
class TACTICALFPS_API ATfpsPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATfpsPlayerController();

	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Input")
	FTfpsKeyBindings Keys;

	/** Degrees of rotation per mouse count. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Input")
	float MouseSensitivity = 0.07f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Input")
	bool bInvertY = false;

	/** Mouse sensitivity multiplier while fully aimed down sights. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Input")
	float AdsSensitivityMultiplier = 0.65f;

	/** Crouch key toggles instead of hold (the Ctrl key always holds). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Input")
	bool bToggleCrouch = true;

	UFUNCTION(BlueprintPure, Category = "TacticalFPS")
	bool IsNetStatsVisible() const { return bShowNetStats; }

private:
	bool KeyDown(const FKey& Key) const;
	void UpdateLook();
	uint16 SampleButtons() const;

	bool bShowNetStats = true;
	bool bMouseReleased = false;
	bool bCrouchToggled = false;
	bool bProneToggled = false;
	uint8 SelectedSlot = 0;
};
