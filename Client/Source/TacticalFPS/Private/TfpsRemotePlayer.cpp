// Copyright TacticalFPS project.
#include "TfpsRemotePlayer.h"
#include "TacticalFPS.h"
#include "TfpsPlaceholderAssets.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"

ATfpsRemotePlayer::ATfpsRemotePlayer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Head = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
	Head->SetupAttachment(Root);
	Visor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visor"));
	Visor->SetupAttachment(Head);
	for (UStaticMeshComponent* C : {Body.Get(), Head.Get(), Visor.Get()})
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(true);
	}
	NameTag = CreateDefaultSubobject<UTextRenderComponent>(TEXT("NameTag"));
	NameTag->SetupAttachment(Root);
	NameTag->SetHorizontalAlignment(EHTA_Center);
	NameTag->SetWorldSize(14.0f);
	NameTag->SetRelativeLocation(FVector(0.0, 0.0, 205.0));
}

void ATfpsRemotePlayer::BeginPlay()
{
	Super::BeginPlay();
	Body->SetStaticMesh(FTfpsPlaceholderAssets::Cylinder());
	Head->SetStaticMesh(FTfpsPlaceholderAssets::Sphere());
	Visor->SetStaticMesh(FTfpsPlaceholderAssets::Cube());
	// Visor shows which way the player is facing (+X).
	Visor->SetRelativeLocation(FVector(42.0, 0.0, 8.0));
	Visor->SetRelativeScale3D(FVector(0.35, 0.9, 0.35));
	if (UMaterialInstanceDynamic* Dark = FTfpsPlaceholderAssets::MakeTinted(this, FLinearColor(0.02f, 0.02f, 0.025f)))
	{
		Visor->SetMaterial(0, Dark);
	}
}

void ATfpsRemotePlayer::ApplyState(const FVector& FeetMeters, float YawDeg, bool bCrouched, const FString& Name, uint8 Team, bool bInterrupted)
{
	SetActorLocationAndRotation(FeetMeters * TfpsMetersToUnreal, FRotator(0.0, YawDeg, 0.0));
	TargetHeight = (bCrouched ? 1.2f : 1.8f) * TfpsMetersToUnreal;
	if (AppliedTeam != Team)
	{
		AppliedTeam = Team;
		const FLinearColor TeamColor = Team == 0 ? FLinearColor(0.10f, 0.22f, 0.45f) : FLinearColor(0.45f, 0.18f, 0.08f);
		if (UMaterialInstanceDynamic* M = FTfpsPlaceholderAssets::MakeTinted(this, TeamColor))
		{
			Body->SetMaterial(0, M);
		}
		if (UMaterialInstanceDynamic* Skin = FTfpsPlaceholderAssets::MakeTinted(this, FLinearColor(0.35f, 0.27f, 0.22f)))
		{
			Head->SetMaterial(0, Skin);
		}
	}
	NameTag->SetText(FText::FromString(bInterrupted ? Name + TEXT(" (connection interrupted)") : Name));
	NameTag->SetTextRenderColor(bInterrupted ? FColor(255, 170, 60) : FColor(235, 235, 235));
}

void ATfpsRemotePlayer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	VisualHeight = FMath::FInterpTo(VisualHeight, TargetHeight, DeltaSeconds, 12.0f);
	// Body: 0.6 m wide cylinder up to shoulders, head sphere on top.
	const float HeadSize = 24.0f;
	const float BodyHeight = VisualHeight - HeadSize - 4.0f;
	Body->SetRelativeScale3D(FVector(0.55, 0.45, BodyHeight / 100.0f));
	Body->SetRelativeLocation(FVector(0.0, 0.0, BodyHeight * 0.5f));
	Head->SetRelativeScale3D(FVector(HeadSize / 100.0f));
	Head->SetRelativeLocation(FVector(0.0, 0.0, BodyHeight + 4.0f + HeadSize * 0.5f));
	NameTag->SetRelativeLocation(FVector(0.0, 0.0, VisualHeight + 22.0f));

	// Billboard the name tag toward the local camera.
	if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		const FVector ToCam = Cam->GetCameraLocation() - NameTag->GetComponentLocation();
		NameTag->SetWorldRotation(FRotator(0.0, ToCam.Rotation().Yaw, 0.0));
	}
}
