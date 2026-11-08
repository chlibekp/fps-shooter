// Copyright TacticalFPS project.
#include "TfpsPlayerController.h"
#include "TacticalFPS.h"
#include "TfpsNetSubsystem.h"
#include "TfpsLobbySubsystem.h"
#include "TfpsPlayerPawn.h"

#include "Engine/GameInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "InputCoreTypes.h"
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
	LoadUserSettings();

	tfps::ui::UserSettings Prefs = Menu.Settings();
	FString Name;
	if (FParse::Value(FCommandLine::Get(), TEXT("name="), Name))
	{
		Prefs.PlayerName = TCHAR_TO_UTF8(*Name);
	}
	FString LobbyUrl;
	if (FParse::Value(FCommandLine::Get(), TEXT("lobby="), LobbyUrl))
	{
		Prefs.LobbyUrl = TCHAR_TO_UTF8(*LobbyUrl);
	}
	Menu.SetSettings(Prefs);
	PlayerName = UTF8_TO_TCHAR(Menu.Settings().PlayerName.c_str());
	ApplyUserSettings();

	UTfpsLobbySubsystem* L = Lobby();
	if (L != nullptr && !Menu.Settings().LobbyUrl.empty())
	{
		L->SetService(UTF8_TO_TCHAR(Menu.Settings().LobbyUrl.c_str()));
		FString Invite;
		if (FParse::Value(FCommandLine::Get(), TEXT("invite="), Invite))
		{
			L->Join(Invite, PlayerName);
		}
	}

	// -connect= skips the menu and joins a server directly.
	FString Address;
	if (FParse::Value(FCommandLine::Get(), TEXT("connect="), Address))
	{
		FString MapName = TEXT("test_arena");
		FParse::Value(FCommandLine::Get(), TEXT("map="), MapName);
		if (UTfpsNetSubsystem* Net = GetGameInstance()->GetSubsystem<UTfpsNetSubsystem>())
		{
			if (Net->GetStatus() == ETfpsConnectionStatus::Offline)
			{
				Net->Connect(Address, PlayerName, MapName);
			}
		}
	}
	TickMenu();
}

FString ATfpsPlayerController::UserSettingsPath() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TfpsUser.cfg"));
}

void ATfpsPlayerController::LoadUserSettings()
{
	FString Text;
	if (FFileHelper::LoadFileToString(Text, *UserSettingsPath()))
	{
		Menu.SetSettings(tfps::ui::UserSettings::Parse(TCHAR_TO_UTF8(*Text)));
		return;
	}
	// First run: start from the Input config values.
	tfps::ui::UserSettings Prefs;
	Prefs.MouseSensitivity = MouseSensitivity;
	Prefs.AdsMultiplier = AdsSensitivityMultiplier;
	Prefs.bInvertY = bInvertY;
	Prefs.bToggleCrouch = bToggleCrouch;
	Menu.SetSettings(Prefs);
}

void ATfpsPlayerController::SaveUserSettings() const
{
	tfps::ui::UserSettings Prefs = Menu.Settings();
	Prefs.Sanitize();
	if (!FFileHelper::SaveStringToFile(FString(UTF8_TO_TCHAR(Prefs.Serialize().c_str())), *UserSettingsPath()))
	{
		UE_LOG(LogTfps, Warning, TEXT("could not save user settings to %s"), *UserSettingsPath());
	}
}

void ATfpsPlayerController::ApplyUserSettings()
{
	const tfps::ui::UserSettings& Prefs = Menu.Settings();
	MouseSensitivity = Prefs.MouseSensitivity;
	AdsSensitivityMultiplier = Prefs.AdsMultiplier;
	bInvertY = Prefs.bInvertY;
	bToggleCrouch = Prefs.bToggleCrouch;
	bShowNetStats = Prefs.bShowNetStats;
	if (!Prefs.PlayerName.empty())
	{
		PlayerName = UTF8_TO_TCHAR(Prefs.PlayerName.c_str());
	}
	auto Bind = [&Prefs](FKey& Key, const char* Action)
	{
		if (const std::string* Name = Prefs.KeyFor(Action))
		{
			Key = FKey(FName(UTF8_TO_TCHAR(Name->c_str())));
		}
	};
	Bind(Keys.Forward, "Forward");
	Bind(Keys.Back, "Back");
	Bind(Keys.Left, "Left");
	Bind(Keys.Right, "Right");
	Bind(Keys.Jump, "Jump");
	Bind(Keys.Crouch, "Crouch");
	Bind(Keys.CrouchAlt, "CrouchAlt");
	Bind(Keys.Prone, "Prone");
	Bind(Keys.Sprint, "Sprint");
	Bind(Keys.Walk, "Walk");
	Bind(Keys.LeanLeft, "LeanLeft");
	Bind(Keys.LeanRight, "LeanRight");
	Bind(Keys.Fire, "Fire");
	Bind(Keys.Aim, "Aim");
	Bind(Keys.Reload, "Reload");
	Bind(Keys.Use, "Use");
	Bind(Keys.PrimaryWeapon, "PrimaryWeapon");
	Bind(Keys.SecondaryWeapon, "SecondaryWeapon");
	Bind(Keys.Gadget1, "Gadget1");
	Bind(Keys.Gadget2, "Gadget2");
	Bind(Keys.Gadget3, "Gadget3");
	Bind(Keys.Gadget4, "Gadget4");
	Bind(Keys.NextWeapon, "NextWeapon");
	Bind(Keys.PrevWeapon, "PrevWeapon");
	Bind(Keys.ToggleNetStats, "ToggleNetStats");
	if (ATfpsPlayerPawn* TfpsPawn = Cast<ATfpsPlayerPawn>(GetPawn()))
	{
		TfpsPawn->FieldOfView = Prefs.FieldOfView;
	}
	if (UTfpsAudioSubsystem* Audio = GetGameInstance()->GetSubsystem<UTfpsAudioSubsystem>())
	{
		Audio->SetVolumes(Prefs.MasterVolume, Prefs.EffectsVolume);
	}
}

void ATfpsPlayerController::SetMenuInputMode(bool bOpen)
{
	if (bOpen == bMenuInputMode)
	{
		return;
	}
	bMenuInputMode = bOpen;
	bShowMouseCursor = bOpen;
	// The click that closed the menu must not also fire.
	bFireHeldFromMenu = !bOpen;
	if (bOpen) { SetInputMode(FInputModeGameAndUI()); }
	else { SetInputMode(FInputModeGameOnly()); }
}

void ATfpsPlayerController::TickMenu()
{
	const UTfpsLobbySubsystem* L = Lobby();
	const UTfpsNetSubsystem* Net = GetGameInstance()->GetSubsystem<UTfpsNetSubsystem>();
	tfps::ui::LobbyState State;
	if (L != nullptr)
	{
		const UTfpsLobbySubsystem::FView& V = L->GetView();
		State.bConfigured = L->IsConfigured();
		State.bInLobby = L->InLobby();
		State.Code = TCHAR_TO_UTF8(*V.Code);
		State.Status = TCHAR_TO_UTF8(*V.Status);
		State.Host = V.Host;
		State.You = V.You;
		for (const UTfpsLobbySubsystem::FMember& M : V.Members)
		{
			State.Members.push_back({M.Id, std::string(TCHAR_TO_UTF8(*M.Name)), M.Squad, M.bReady});
		}
		for (const FString& M : V.Maps)
		{
			State.Maps.push_back(TCHAR_TO_UTF8(*M));
		}
		State.Map = TCHAR_TO_UTF8(*V.Map);
		State.Mode = TCHAR_TO_UTF8(*V.Mode);
		State.TeamSize = V.TeamSize;
		State.RoundsToWin = V.RoundsToWin;
		State.ScoreLimit = V.ScoreLimit;
		State.TickRate = V.TickRate;
		State.RoundTime = V.RoundTime;
		State.PrepTime = V.PrepTime;
		State.bFriendlyFire = V.bFriendlyFire;
		State.Error = TCHAR_TO_UTF8(*L->GetLastError());
	}
	const bool bInMatch = Net != nullptr && Net->GetStatus() != ETfpsConnectionStatus::Offline;
	Menu.Sync(State, bInMatch);

	if (WasInputKeyJustPressed(Keys.ReleaseMouse) && !Menu.IsCapturingKey())
	{
		Menu.TogglePause();
	}
	else if (Menu.IsCapturingKey())
	{
		// Rebinding: the first key pressed (not gamepad / touch / analog axes).
		TArray<FKey> AllKeys;
		EKeys::GetAllKeys(AllKeys);
		for (const FKey& K : AllKeys)
		{
			if (K.IsGamepadKey() || K.IsTouch() || K.IsAxis1D() || K.IsAxis2D() || !WasInputKeyJustPressed(K))
			{
				continue;
			}
			Menu.OnKeyCaptured(TCHAR_TO_UTF8(*K.GetFName().ToString()));
			break;
		}
	}
	else if (Menu.IsOpen())
	{
		auto Pressed = [this](const TCHAR* Name) { return WasInputKeyJustPressed(FKey(FName(Name))); };
		if (Pressed(TEXT("Up"))) { Menu.OnNav(tfps::ui::Nav::Up); }
		if (Pressed(TEXT("Down")) || Pressed(TEXT("Tab"))) { Menu.OnNav(tfps::ui::Nav::Down); }
		if (Pressed(TEXT("Left"))) { Menu.OnNav(tfps::ui::Nav::Left); }
		if (Pressed(TEXT("Right"))) { Menu.OnNav(tfps::ui::Nav::Right); }
		if (Pressed(TEXT("Enter"))) { Menu.OnNav(tfps::ui::Nav::Accept); }
		if (Menu.IsEditingText())
		{
			if (Pressed(TEXT("BackSpace"))) { Menu.OnBackspace(); }
			const bool bShift = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(FKey(FName(TEXT("RightShift"))));
			static const TCHAR* const Letters[] = {TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D"), TEXT("E"), TEXT("F"), TEXT("G"),
				TEXT("H"), TEXT("I"), TEXT("J"), TEXT("K"), TEXT("L"), TEXT("M"), TEXT("N"), TEXT("O"), TEXT("P"), TEXT("Q"),
				TEXT("R"), TEXT("S"), TEXT("T"), TEXT("U"), TEXT("V"), TEXT("W"), TEXT("X"), TEXT("Y"), TEXT("Z")};
			for (int32 I = 0; I < 26; ++I)
			{
				if (Pressed(Letters[I])) { Menu.OnChar(static_cast<char>((bShift ? 'A' : 'a') + I)); }
			}
			static const TCHAR* const Digits[] = {TEXT("Zero"), TEXT("One"), TEXT("Two"), TEXT("Three"), TEXT("Four"),
				TEXT("Five"), TEXT("Six"), TEXT("Seven"), TEXT("Eight"), TEXT("Nine")};
			static const TCHAR* const PadDigits[] = {TEXT("NumPadZero"), TEXT("NumPadOne"), TEXT("NumPadTwo"), TEXT("NumPadThree"),
				TEXT("NumPadFour"), TEXT("NumPadFive"), TEXT("NumPadSix"), TEXT("NumPadSeven"), TEXT("NumPadEight"), TEXT("NumPadNine")};
			for (int32 I = 0; I < 10; ++I)
			{
				if ((Pressed(Digits[I]) && !bShift) || Pressed(PadDigits[I])) { Menu.OnChar(static_cast<char>('0' + I)); }
			}
			if (Pressed(TEXT("Period")) || Pressed(TEXT("Decimal"))) { Menu.OnChar('.'); }
			if (Pressed(TEXT("Semicolon")) && bShift) { Menu.OnChar(':'); }
			if (Pressed(TEXT("Hyphen")) || Pressed(TEXT("Subtract"))) { Menu.OnChar(bShift ? '_' : '-'); }
			if (Pressed(TEXT("Slash")) || Pressed(TEXT("Divide"))) { Menu.OnChar('/'); }
		}
		// Mouse, in the menu's virtual 1920x1080 space (the HUD draws it scaled to fit).
		float MX = 0.0f;
		float MY = 0.0f;
		int32 VW = 0;
		int32 VH = 0;
		GetViewportSize(VW, VH);
		if (VW > 0 && VH > 0 && GetMousePosition(MX, MY))
		{
			const float S = FMath::Min(VW / tfps::ui::kVirtualWidth, VH / tfps::ui::kVirtualHeight);
			const float OX = (VW - tfps::ui::kVirtualWidth * S) * 0.5f;
			const float OY = (VH - tfps::ui::kVirtualHeight * S) * 0.5f;
			const float X = (MX - OX) / S;
			const float Y = (MY - OY) / S;
			if (MX != LastMouseX || MY != LastMouseY)
			{
				Menu.OnMouseMove(X, Y);
				LastMouseX = MX;
				LastMouseY = MY;
			}
			if (WasInputKeyJustPressed(EKeys::LeftMouseButton)) { Menu.OnClick(X, Y); }
		}
	}
	const int32 Actions = HandleMenuActions();
	// Interface clicks: moving the focus ticks, confirming / changing screens blips.
	if (UTfpsAudioSubsystem* Audio = GetGameInstance()->GetSubsystem<UTfpsAudioSubsystem>())
	{
		if (Menu.IsOpen() && Menu.Current() == LastMenuScreen && Menu.Focus() != LastMenuFocus)
		{
			Audio->Play2D(tfps::audio::Cue::UiMove);
		}
		if (Actions > 0 || (Menu.Current() != LastMenuScreen && LastMenuScreen != tfps::ui::Screen::None && Menu.IsOpen()))
		{
			Audio->Play2D(tfps::audio::Cue::UiAccept);
		}
	}
	LastMenuScreen = Menu.Current();
	LastMenuFocus = Menu.Focus();
	SetMenuInputMode(Menu.IsOpen());
}

int32 ATfpsPlayerController::HandleMenuActions()
{
	UTfpsLobbySubsystem* L = Lobby();
	UTfpsNetSubsystem* Net = GetGameInstance()->GetSubsystem<UTfpsNetSubsystem>();
	const std::vector<tfps::ui::Action> Actions = Menu.TakeActions();
	for (const tfps::ui::Action& A : Actions)
	{
		const FString Key = UTF8_TO_TCHAR(A.Key.c_str());
		const FString Value = UTF8_TO_TCHAR(A.Value.c_str());
		switch (A.Kind)
		{
		case tfps::ui::ActionKind::HostLobby: if (L) { L->Host(PlayerName); } break;
		case tfps::ui::ActionKind::JoinLobby: if (L) { L->Join(Value, PlayerName); } break;
		case tfps::ui::ActionKind::DirectConnect: if (Net) { Net->Connect(Value, PlayerName, Key); } break;
		case tfps::ui::ActionKind::LobbySetting: if (L) { L->SetSetting(Key, Value); } break;
		case tfps::ui::ActionKind::SetSquad: if (L) { L->SetSquad(FCString::Atoi(*Value)); } break;
		case tfps::ui::ActionKind::SetReady: if (L) { L->SetReady(Value == TEXT("true")); } break;
		case tfps::ui::ActionKind::StartMatch: if (L) { L->Start(); } break;
		case tfps::ui::ActionKind::LeaveLobby: if (L) { L->Leave(); } break;
		case tfps::ui::ActionKind::LeaveMatch: if (Net) { Net->Disconnect(); } break;
		case tfps::ui::ActionKind::Resume: break;
		case tfps::ui::ActionKind::Quit: ConsoleCommand(TEXT("quit")); break;
		case tfps::ui::ActionKind::SettingsChanged:
			ApplyUserSettings();
			SaveUserSettings();
			if (L != nullptr && !Menu.Settings().LobbyUrl.empty() && !L->InLobby())
			{
				L->SetService(UTF8_TO_TCHAR(Menu.Settings().LobbyUrl.c_str()));
			}
			break;
		}
	}
	// Typing a name saves on every key: that is not a "click".
	int32 Count = 0;
	for (const tfps::ui::Action& A : Actions)
	{
		Count += A.Kind == tfps::ui::ActionKind::SettingsChanged && Menu.IsEditingText() ? 0 : 1;
	}
	return Count;
}

void ATfpsPlayerController::PlayLocalSounds()
{
	UTfpsAudioSubsystem* Audio = GetGameInstance()->GetSubsystem<UTfpsAudioSubsystem>();
	const UTfpsNetSubsystem* Net = GetGameInstance()->GetSubsystem<UTfpsNetSubsystem>();
	const tfps::ClientCore* Core = Net != nullptr ? Net->GetCore() : nullptr;
	if (Audio == nullptr || Core == nullptr || !Core->IsConnected() || Core->Health() == 0)
	{
		return;
	}
	const tfps::MoveState& M = Core->Predicted();
	const FVector Feet(M.Pos.X, M.Pos.Y, M.Pos.Z);
	const bool bSprint = !Menu.IsOpen() && KeyDown(Keys.Sprint);
	const bool bQuiet = M.bCrouched || M.bProne || (!Menu.IsOpen() && KeyDown(Keys.Walk));
	bool bLanded = false;
	if (LocalSteps.Update(Feet, M.bOnGround, bSprint ? 2.2f : 1.7f, bLanded))
	{
		Audio->Footstep(Net->GetMap(), Feet, bQuiet ? 0.4f : 0.8f, true);
	}
	if (bLanded)
	{
		Audio->Play2D(tfps::audio::Cue::Land, 0.7f);
	}
	// Reload: magazine out when it starts, in when it completes on the same weapon.
	const tfps::WeaponState& W = Core->Weapon();
	const bool bReloading = W.IsReloading();
	if (bReloading && !bWasReloading)
	{
		Audio->Play2D(tfps::audio::Cue::MagOut);
		bReloadWasEmpty = W.bReloadEmpty;
		ReloadSlot = W.Active;
	}
	else if (!bReloading && bWasReloading && W.Active == ReloadSlot)
	{
		Audio->Play2D(tfps::audio::Cue::MagIn);
		if (bReloadWasEmpty)
		{
			Audio->Play2D(tfps::audio::Cue::BoltRack);
		}
	}
	bWasReloading = bReloading;
	// Dry fire: trigger pulled on an empty gun.
	if (!Menu.IsOpen() && !bFireHeldFromMenu && WasInputKeyJustPressed(Keys.Fire) && W.Active < tfps::kWeaponSlots && !bReloading)
	{
		const tfps::SlotAmmo& A = W.Slots[W.Active];
		if (A.Ammo == 0 && !A.bChambered)
		{
			Audio->Play2D(tfps::audio::Cue::DryFire);
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
	if (Menu.IsOpen())
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
	if (KeyDown(Keys.Fire) && !bFireHeldFromMenu) { B |= tfps::Buttons::Fire; }
	if (bProneToggled) { B |= tfps::Buttons::Prone; }
	if (KeyDown(Keys.Reload)) { B |= tfps::Buttons::Reload; }
	if (KeyDown(Keys.Use)) { B |= tfps::Buttons::Use; }
	return B;
}

void ATfpsPlayerController::UpdateLook()
{
	if (Menu.IsOpen())
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
	TickMenu();
	if (bFireHeldFromMenu && !KeyDown(Keys.Fire))
	{
		bFireHeldFromMenu = false;
	}
	if (!Menu.IsOpen() && WasInputKeyJustPressed(Keys.ToggleNetStats))
	{
		bShowNetStats = !bShowNetStats;
	}

	// Stance toggles and weapon selection (edge-triggered).
	if (!Menu.IsOpen())
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
	PlayLocalSounds();

	if (ATfpsPlayerPawn* TfpsPawn = Cast<ATfpsPlayerPawn>(GetPawn()))
	{
		TfpsPawn->FieldOfView = Menu.Settings().FieldOfView;
		TfpsPawn->UpdateFromNetwork(*Net, Rot, DeltaTime);
	}
}
