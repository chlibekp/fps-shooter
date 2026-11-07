// Copyright TacticalFPS project.
#include "TfpsPlayerPawn.h"
#include "TacticalFPS.h"
#include "TfpsNetSubsystem.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"

ATfpsPlayerPawn::ATfpsPlayerPawn()
{
	PrimaryActorTick.bCanEverTick = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetRelativeLocation(FVector(0.0, 0.0, CurrentEyeHeight));
	Camera->bUsePawnControlRotation = false;
	Camera->SetFieldOfView(FieldOfView);
}

void ATfpsPlayerPawn::UpdateFromNetwork(const UTfpsNetSubsystem& Net, const FRotator& ViewRotation, float DeltaTime)
{
	const tfps::ClientCore* Core = Net.GetCore();
	if (Core == nullptr || !Core->IsConnected())
	{
		Camera->SetWorldRotation(ViewRotation);
		return;
	}
	// Interpolate between the last two fixed ticks for smooth motion.
	const tfps::MoveState& Prev = Core->PreviousPredicted();
	const tfps::MoveState& Cur = Core->Predicted();
	const float Alpha = Core->TickAlpha();
	const FVector P0(Prev.Pos.X, Prev.Pos.Y, Prev.Pos.Z);
	const FVector P1(Cur.Pos.X, Cur.Pos.Y, Cur.Pos.Z);
	const FVector NetLocation = FMath::Lerp(P0, P1, Alpha) * TfpsMetersToUnreal;

	// A reconciliation correction would snap the camera; instead absorb the
	// jump into an offset that decays quickly (visual only).
	if (bHasLocation)
	{
		const FVector Jump = NetLocation - LastNetLocation;
		const double ExpectedMax = 8.0 * TfpsMetersToUnreal * FMath::Max(DeltaTime, 0.001f) + 5.0;
		if (Jump.Size() > ExpectedMax && Jump.Size() < 200.0)
		{
			CorrectionOffset -= Jump - (Jump.GetSafeNormal() * ExpectedMax);
		}
	}
	CorrectionOffset *= FMath::Exp(-CorrectionSmoothing * DeltaTime);
	LastNetLocation = NetLocation;
	bHasLocation = true;
	SetActorLocation(NetLocation + CorrectionOffset);

	const float TargetEye = Cur.EyeHeight() * TfpsMetersToUnreal;
	CurrentEyeHeight = FMath::FInterpTo(CurrentEyeHeight, TargetEye, DeltaTime, EyeHeightSmoothing);
	Camera->SetRelativeLocation(FVector(0.0, 0.0, CurrentEyeHeight));
	Camera->SetWorldRotation(ViewRotation);
	Camera->SetFieldOfView(FieldOfView);
}
