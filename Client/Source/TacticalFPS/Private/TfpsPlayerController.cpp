// Copyright TacticalFPS project.
#include "TfpsPlayerController.h"
#include "TacticalFPS.h"
#include "TfpsNetSubsystem.h"
#include "TfpsLobbySubsystem.h"
#include "TfpsPlayerPawn.h"

#include "Engine/GameInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NetCore/TfpsMath.h"
#include "NetCore/TfpsMovement.h"

ATfpsPlayerController::ATfpsPlayerController()
{
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bShowMouseCursor = false;
}

void ATfpsPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	SetInputMode(FInputModeGameOnly());

	FString Address = TEXT("127.0.0.1:7777");
	FString Name = FString::Printf(TEXT("Operator%d"), FMath::RandRange(100, 999));
	FString MapName = TEXT("test_arena");
	FParse::Value(FCommandLine::Get(), TEXT("connect="), Address);
	FParse::Value(FCommandLine::Get(), TEXT("name="), Name);
	FParse::Value(FCommandLine::Get(), TEXT("map="), MapName);
	PlayerName = Name;

	// With a lobby service (-lobby=http://host:7700) players host or join by
	// invite code from the console; the lobby connects us when the match starts.
	FString LobbyUrl;
	if (FParse::Value(FCommandLine::Get(), TEXT("lobby="), LobbyUrl))
	{
		if (UTfpsLobbySubsystem* Lobby = GetGameInstance()->GetSubsystem<UTfpsLobbySubsystem>())
		{
			Lobby->SetService(LobbyUrl);
			FString Invite;
			if (FParse::Value(FCommandLine::Get(), TEXT("invite="), Invite))
			{
				Lobby->Join(Invite, Name);
			}
		}
		return;
	}

	if (UTfpsNetSubsystem* Net = GetGameInstance()->GetSubsystem<UTfpsNetSubsystem>())
	{
		if (Net->GetStatus() == ETfpsConnectionStatus::Offline)
		{
			Net->Connect(Address, Name, MapName);
		}
	}
}

UTfpsLobbySubsystem* ATfpsPlayerController::Lobby() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UTfpsLobbySubsystem>() : nullptr;
}

void ATfpsPlayerController::TfpsHost()
{
	if (UTfpsLobbySubsystem* L = Lobby()) { L->Host(PlayerName); }
}

void ATfpsPlayerController::TfpsJoin(const FString& Code)
{
	if (UTfpsLobbySubsystem* L = Lobby()) { L->Join(Code, PlayerName); }
}

void ATfpsPlayerController::TfpsSet(const FString& Key, const FString& Value)
{
	if (UTfpsLobbySubsystem* L = Lobby()) { L->SetSetting(Key, Value); }
}

void ATfpsPlayerController::TfpsSquad(int32 Squad)
{
	if (UTfpsLobbySubsystem* L = Lobby()) { L->SetSquad(Squad); }
}

void ATfpsPlayerController::TfpsReady()
{
	if (UTfpsLobbySubsystem* L = Lobby()) { L->SetReady(true); }
}

void ATfpsPlayerController::TfpsUnready()
{
	if (UTfpsLobbySubsystem* L = Lobby()) { L->SetReady(false); }
}

void ATfpsPlayerController::TfpsStart()
{
	if (UTfpsLobbySubsystem* L = Lobby()) { L->Start(); }
}

void ATfpsPlayerController::TfpsLeave()
{
	if (UTfpsLobbySubsystem* L = Lobby()) { L->Leave(); }
}

bool ATfpsPlayerController::KeyDown(const FKey& Key) const
{
	return Key.IsValid() && IsInputKeyDown(Key);
}

uint16 ATfpsPlayerController::SampleButtons() const
{
	if (bMouseReleased)
	{
		return 0;
	}
	uint16 B = 0;
	if (KeyDown(Keys.Forward)) { B |= tfps::Buttons::Forward; }
	if (KeyDown(Keys.Back)) { B |= tfps::Buttons::Back; }
	if (KeyDown(Keys.Left)) { B |= tfps::Buttons::Left; }
	if (KeyDown(Keys.Right)) { B |= tfps::Buttons::Right; }
	if (KeyDown(Keys.Jump)) { B |= tfps::Buttons::Jump; }
	if (KeyDown(Keys.Crouch) || (bToggleCrouch ? bCrouchToggled : KeyDown(Keys.CrouchAlt))) { B |= tfps::Buttons::Crouch; }
	if (KeyDown(Keys.Sprint)) { B |= tfps::Buttons::Sprint; }
	if (KeyDown(Keys.Walk)) { B |= tfps::Buttons::Walk; }
	if (KeyDown(Keys.LeanLeft)) { B |= tfps::Buttons::LeanLeft; }
	if (KeyDown(Keys.LeanRight)) { B |= tfps::Buttons::LeanRight; }
	if (KeyDown(Keys.Aim)) { B |= tfps::Buttons::Aim; }
	if (KeyDown(Keys.Fire)) { B |= tfps::Buttons::Fire; }
	if (bProneToggled) { B |= tfps::Buttons::Prone; }
	if (KeyDown(Keys.Reload)) { B |= tfps::Buttons::Reload; }
	if (KeyDown(Keys.Use)) { B |= tfps::Buttons::Use; }
	return B;
}

void ATfpsPlayerController::UpdateLook()
{
	if (bMouseReleased)
	{
		return;
	}
	float DX = 0.0f;
	float DY = 0.0f;
	GetInputMouseDelta(DX, DY);
	float Sensitivity = MouseSensitivity;
	if (const UTfpsNetSubsystem* Net = GetGameInstance()->GetSubsystem<UTfpsNetSubsystem>())
	{
		if (const tfps::ClientCore* Core = Net->GetCore())
		{
			Sensitivity *= FMath::Lerp(1.0f, AdsSensitivityMultiplier, Core->Predicted().Aim);
		}
	}
	FRotator Rot = GetControlRotation();
	Rot.Yaw = FRotator::ClampAxis(Rot.Yaw + DX * Sensitivity);
	const float PitchDelta = DY * Sensitivity * (bInvertY ? -1.0f : 1.0f);
	Rot.Pitch = FMath::ClampAngle(static_cast<float>(FRotator::NormalizeAxis(Rot.Pitch)) + PitchDelta, -89.0f, 89.0f);
	Rot.Roll = 0.0f;
	SetControlRotation(Rot);
}

void ATfpsPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!IsLocalController())
	{
		return;
	}
	if (WasInputKeyJustPressed(Keys.ToggleNetStats))
	{
		bShowNetStats = !bShowNetStats;
	}
	if (WasInputKeyJustPressed(Keys.ReleaseMouse))
	{
		bMouseReleased = !bMouseReleased;
		bShowMouseCursor = bMouseReleased;
		if (bMouseReleased) { SetInputMode(FInputModeGameAndUI()); }
		else { SetInputMode(FInputModeGameOnly()); }
	}

	// Stance toggles and weapon selection (edge-triggered).
	if (!bMouseReleased)
	{
		if (WasInputKeyJustPressed(Keys.CrouchAlt)) { bCrouchToggled = !bCrouchToggled; bProneToggled = false; }
		if (WasInputKeyJustPressed(Keys.Prone)) { bProneToggled = !bProneToggled; bCrouchToggled = false; }
		if (WasInputKeyJustPressed(Keys.Jump) && bProneToggled) { bProneToggled = false; }
		if (WasInputKeyJustPressed(Keys.PrimaryWeapon)) { SelectedSlot = 0; }
		if (WasInputKeyJustPressed(Keys.SecondaryWeapon)) { SelectedSlot = 1; }
		if (WasInputKeyJustPressed(Keys.NextWeapon) || WasInputKeyJustPressed(Keys.PrevWeapon))
		{
			SelectedSlot = tfps::IsGadgetSlot(SelectedSlot) ? LastWeaponSlot : (SelectedSlot == 0 ? 1 : 0);
		}
		if (WasInputKeyJustPressed(Keys.Gadget1)) { SelectedSlot = tfps::kWeaponSlots; }
		if (WasInputKeyJustPressed(Keys.Gadget2)) { SelectedSlot = tfps::kWeaponSlots + 1; }
		if (WasInputKeyJustPressed(Keys.Gadget3)) { SelectedSlot = tfps::kWeaponSlots + 2; }
		if (WasInputKeyJustPressed(Keys.Gadget4)) { SelectedSlot = tfps::kWeaponSlots + 3; }
	}
	if (!tfps::IsGadgetSlot(SelectedSlot))
	{
		LastWeaponSlot = SelectedSlot;
	}

	UpdateLook();

	UTfpsNetSubsystem* Net = GetGameInstance()->GetSubsystem<UTfpsNetSubsystem>();
	if (Net == nullptr)
	{
		return;
	}
	// After a grenade leaves the hand (the server's count dropped), go back to the gun.
	if (const tfps::ClientCore* Core = Net->GetCore())
	{
		for (int32 G = 0; G < tfps::kGadgetSlots; ++G)
		{
			const tfps::GadgetStock Stock = Core->LocalGadget(G);
			const tfps::GadgetDef* Def = Net->GetGadgets().Get(Stock.Id);
			const bool bThrown = Def != nullptr && tfps::IsGrenade(Def->Kind);
			if (SelectedSlot == tfps::kWeaponSlots + G && bThrown && Stock.Count < LastGadgetCount[G])
			{
				SelectedSlot = LastWeaponSlot;
			}
			LastGadgetCount[G] = Stock.Count;
		}
	}
	const FRotator Rot = GetControlRotation();
	tfps::MoveInput Input;
	Input.Buttons = SampleButtons();
	Input.YawQ = tfps::QuantizeYawDeg(static_cast<float>(Rot.Yaw));
	Input.PitchQ = tfps::QuantizePitchDeg(static_cast<float>(FRotator::NormalizeAxis(Rot.Pitch)));
	Input.Slot = SelectedSlot;
	// Drone / camera slot with a usable device of ours: view through it (the server freezes the body).
	if (tfps::IsGadgetSlot(SelectedSlot) && Net->GetMyRemoteDevice(SelectedSlot - tfps::kWeaponSlots) != nullptr)
	{
		Input.Slot |= tfps::kRemoteControl;
	}
	Net->SetLocalInput(Input);
	Net->Pump();

	if (ATfpsPlayerPawn* TfpsPawn = Cast<ATfpsPlayerPawn>(GetPawn()))
	{
		TfpsPawn->UpdateFromNetwork(*Net, Rot, DeltaTime);
	}
}
