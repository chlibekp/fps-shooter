// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "NetCore/TfpsWeapon.h"
#include "Ui/TfpsMenu.h"
#include "TfpsAudioSubsystem.h"

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
	/** Gadget slots (throw / place with Fire; Aim + Fire = underhand lob / detonate charges). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey Gadget1 = EKeys::Three;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey Gadget2 = EKeys::Four;
	/** Third gadget slot (attackers: drones; Fire deploys, then the drone is driven while the slot is held). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey Gadget3 = EKeys::Five;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey Gadget4 = EKeys::Six;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey NextWeapon = EKeys::MouseScrollDown;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey PrevWeapon = EKeys::MouseScrollUp;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey ToggleNetStats = EKeys::F3;
	/** Opens / closes the menu (not rebindable from the menu). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keys") FKey ReleaseMouse = EKeys::Escape;
};

/**
 * Samples keyboard/mouse every frame into a protocol MoveInput, drives the
 * network pump and then updates the local pawn's view. Only inputs are sent
 * to the server; the server decides where the player actually is.
 *
 * Out of a match it shows the front-end menu (host / join a lobby by invite
 * code, direct connect, settings, key bindings); in a match Esc opens the
 * pause menu. The menu logic is tfps::ui::Menu (engine-independent, unit
 * tested); this class feeds it input, applies its settings and performs its
 * actions. User settings are saved to Saved/TfpsUser.cfg.
 *
 * Command line: -connect=host:port -name=PlayerName -map=test_arena
 * (connects directly), -lobby=http://host:7700 [-invite=CODE].
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

	/** Horizontal field of view from the user settings. */
	UFUNCTION(BlueprintPure, Category = "TacticalFPS")
	float GetFieldOfView() const { return Menu.Settings().FieldOfView; }

	/** Volumes 0..1 from the user settings (master x effects for gameplay sounds). */
	UFUNCTION(BlueprintPure, Category = "TacticalFPS")
	float GetEffectsVolume() const { return Menu.Settings().MasterVolume * Menu.Settings().EffectsVolume; }

	const tfps::ui::Menu& GetMenu() const { return Menu; }

	// Lobby console commands (with -lobby=http://host:7700).
	/** Create a lobby; the invite code is shown on screen. */
	UFUNCTION(Exec) void TfpsHost();
	/** Join a friend's lobby by invite code. */
	UFUNCTION(Exec) void TfpsJoin(const FString& Code);
	/** Host: map, mode, team_size, rounds_to_win, round_time, prep_time, score_limit, friendly_fire, tick_rate. */
	UFUNCTION(Exec) void TfpsSet(const FString& Key, const FString& Value);
	UFUNCTION(Exec) void TfpsSquad(int32 Squad);
	UFUNCTION(Exec) void TfpsReady();
	UFUNCTION(Exec) void TfpsUnready();
	UFUNCTION(Exec) void TfpsStart();
	UFUNCTION(Exec) void TfpsLeave();

private:
	class UTfpsLobbySubsystem* Lobby() const;
	FString UserSettingsPath() const;
	void LoadUserSettings();
	void SaveUserSettings() const;
	/** Copies the menu's settings into the input properties and the pawn. */
	void ApplyUserSettings();
	/** Syncs the menu with the lobby / match state and feeds it this frame's input. */
	void TickMenu();
	/** Performs the menu's queued actions; returns how many there were. */
	int32 HandleMenuActions();
	/** Own footsteps, reload and dry-fire sounds from the predicted state. */
	void PlayLocalSounds();
	void SetMenuInputMode(bool bOpen);

	tfps::ui::Menu Menu;
	bool bMenuInputMode = false;
	FTfpsStepTracker LocalSteps;
	bool bWasReloading = false;
	bool bReloadWasEmpty = false;
	uint8 ReloadSlot = 0;
	int32 LastMenuFocus = -1;
	tfps::ui::Screen LastMenuScreen = tfps::ui::Screen::None;
	bool bFireHeldFromMenu = false;
	float LastMouseX = -1.0f;
	float LastMouseY = -1.0f;
	FString PlayerName;
	bool KeyDown(const FKey& Key) const;
	void UpdateLook();
	uint16 SampleButtons() const;

	bool bShowNetStats = true;
	bool bCrouchToggled = false;
	bool bProneToggled = false;
	uint8 SelectedSlot = 0;
	/** Weapon slot to return to after throwing a grenade. */
	uint8 LastWeaponSlot = 0;
	uint8 LastGadgetCount[tfps::kGadgetSlots] = {};
};
