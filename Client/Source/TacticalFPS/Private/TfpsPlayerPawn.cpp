// Copyright TacticalFPS project.
#include "TfpsPlayerPawn.h"
#include "TacticalFPS.h"
#include "TfpsNetSubsystem.h"

#include "Camera/CameraComponent.h"
#include "NetCore/TfpsMath.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TfpsPlaceholderAssets.h"

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

	ViewWeapon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ViewWeapon"));
	ViewWeapon->SetupAttachment(Camera);
	ViewWeapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ViewWeapon->SetCastShadow(false);
}

void ATfpsPlayerPawn::BeginPlay()
{
	Super::BeginPlay();
	ViewWeapon->SetStaticMesh(FTfpsPlaceholderAssets::Cube());
	if (UMaterialInstanceDynamic* M = FTfpsPlaceholderAssets::MakeTinted(this, FLinearColor(0.03f, 0.03f, 0.035f)))
	{
		ViewWeapon->SetMaterial(0, M);
	}
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

	// Eye: predicted eye height (stance) smoothed, plus the lean offset of the
	// shared simulation so the camera is exactly where the server's eye is.
	const uint16 YawQ = tfps::QuantizeYawDeg(static_cast<float>(ViewRotation.Yaw));
	const tfps::Vec3 Eye = Cur.EyePosition(YawQ);
	const float TargetEye = Cur.EyeHeight() * TfpsMetersToUnreal;
	CurrentEyeHeight = FMath::FInterpTo(CurrentEyeHeight, TargetEye, DeltaTime, EyeHeightSmoothing);
	const FVector LeanOffset((Eye.X - Cur.Pos.X) * TfpsMetersToUnreal, (Eye.Y - Cur.Pos.Y) * TfpsMetersToUnreal, 0.0);
	const float LeanDrop = (Cur.EyeHeight() - (Eye.Z - Cur.Pos.Z)) * TfpsMetersToUnreal;
	Camera->SetWorldLocation(GetActorLocation() + LeanOffset + FVector(0.0, 0.0, CurrentEyeHeight - LeanDrop));

	// View = aim angles sent to the server + the weapon's recoil kick (the
	// server adds the same kick to the shot direction), rolled while leaning.
	const tfps::WeaponState& W = Core->Weapon();
	FRotator View = ViewRotation;
	View.Pitch += W.RecoilPitch;
	View.Yaw += W.RecoilYaw;
	View.Roll = Cur.Lean * LeanRollDegrees;
	Camera->SetWorldRotation(View);
	Camera->SetFieldOfView(FieldOfView * FMath::Lerp(1.0f, AdsFovMultiplier, Cur.Aim));

	// Placeholder viewmodel: hip position -> centred when aiming, kicks back on each shot.
	if (W.ShotCounter != LastShotCounter)
	{
		LastShotCounter = W.ShotCounter;
		WeaponKick = 1.0f;
	}
	WeaponKick = FMath::FInterpTo(WeaponKick, 0.0f, DeltaTime, 18.0f);
	const tfps::WeaponDef* Def = Core->LoadoutWeapon(W.Active);
	const bool bPistol = Def != nullptr && Def->Class == "pistol";
	const float Length = bPistol ? 0.22f : 0.62f;
	const FVector Hip(32.0f, 14.0f, -14.0f);
	const FVector Ads(26.0f, 0.0f, -6.5f);
	FVector Offset = FMath::Lerp(Hip, Ads, Cur.Aim);
	Offset.X -= WeaponKick * 4.0f;
	if (W.ReloadTimer > 0.0f)
	{
		Offset.Z -= 10.0f; // lowered while reloading
	}
	if (W.EquipTimer > 0.0f && Def != nullptr && Def->EquipTime > 0.0f)
	{
		Offset.Z -= 20.0f * (W.EquipTimer / Def->EquipTime);
	}
	ViewWeapon->SetRelativeLocation(Offset);
	ViewWeapon->SetRelativeRotation(FRotator(WeaponKick * 3.0f, 0.0f, 0.0f));
	ViewWeapon->SetRelativeScale3D(FVector(Length, 0.05f, 0.08f));
	ViewWeapon->SetVisibility(Core->IsAlive());
}
