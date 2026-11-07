// Copyright TacticalFPS project.
#include "TfpsPlayerController.h"
#include "TacticalFPS.h"
#include "TfpsNetSubsystem.h"
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

	if (UTfpsNetSubsystem* Net = GetGameInstance()->GetSubsystem<UTfpsNetSubsystem>())
	{
		if (Net->GetStatus() == ETfpsConnectionStatus::Offline)
		{
			Net->Connect(Address, Name, MapName);
		}
	}
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
	if (KeyDown(Keys.Crouch) || KeyDown(Keys.CrouchAlt)) { B |= tfps::Buttons::Crouch; }
	if (KeyDown(Keys.Sprint)) { B |= tfps::Buttons::Sprint; }
	if (KeyDown(Keys.Walk)) { B |= tfps::Buttons::Walk; }
	if (KeyDown(Keys.LeanLeft)) { B |= tfps::Buttons::LeanLeft; }
	if (KeyDown(Keys.LeanRight)) { B |= tfps::Buttons::LeanRight; }
	if (KeyDown(Keys.Aim)) { B |= tfps::Buttons::Aim; }
	if (KeyDown(Keys.Fire)) { B |= tfps::Buttons::Fire; }
	if (KeyDown(Keys.Prone)) { B |= tfps::Buttons::Prone; }
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
	FRotator Rot = GetControlRotation();
	Rot.Yaw = FRotator::ClampAxis(Rot.Yaw + DX * MouseSensitivity);
	const float PitchDelta = DY * MouseSensitivity * (bInvertY ? -1.0f : 1.0f);
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

	UpdateLook();

	UTfpsNetSubsystem* Net = GetGameInstance()->GetSubsystem<UTfpsNetSubsystem>();
	if (Net == nullptr)
	{
		return;
	}
	const FRotator Rot = GetControlRotation();
	tfps::MoveInput Input;
	Input.Buttons = SampleButtons();
	Input.YawQ = tfps::QuantizeYawDeg(static_cast<float>(Rot.Yaw));
	Input.PitchQ = tfps::QuantizePitchDeg(static_cast<float>(FRotator::NormalizeAxis(Rot.Pitch)));
	Net->SetLocalInput(Input);
	Net->Pump();

	if (ATfpsPlayerPawn* TfpsPawn = Cast<ATfpsPlayerPawn>(GetPawn()))
	{
		TfpsPawn->UpdateFromNetwork(*Net, Rot, DeltaTime);
	}
}
