// Copyright TacticalFPS project.
#include "TfpsShotFx.h"
#include "TacticalFPS.h"
#include "TfpsNetSubsystem.h"
#include "TfpsPlaceholderAssets.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NetCore/TfpsMap.h"

namespace
{
	constexpr int32 TracerPool = 48;
	constexpr int32 ImpactPool = 96;
	constexpr float TracerLife = 0.06f;
	constexpr float ImpactLife = 4.0f;

	FLinearColor SurfaceColor(uint8 Surface)
	{
		switch (Surface)
		{
		case 0: return FLinearColor(0.55f, 0.55f, 0.52f); // concrete dust
		case 1: return FLinearColor(0.5f, 0.22f, 0.15f);  // brick
		case 2: return FLinearColor(0.45f, 0.32f, 0.18f); // wood
		case 3: return FLinearColor(0.85f, 0.85f, 0.82f); // drywall
		case 4: return FLinearColor(0.9f, 0.75f, 0.4f);   // metal spark
		case 5: return FLinearColor(0.7f, 0.85f, 0.9f);   // glass
		case 0xFE: return FLinearColor(0.5f, 0.02f, 0.02f); // player hit
		default: return FLinearColor(0.3f, 0.25f, 0.2f);
		}
	}
}

ATfpsShotFx::ATfpsShotFx()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	MuzzleLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("MuzzleLight"));
	MuzzleLight->SetupAttachment(GetRootComponent());
	MuzzleLight->SetIntensity(0.0f);
	MuzzleLight->SetAttenuationRadius(450.0f);
	MuzzleLight->SetLightColor(FLinearColor(1.0f, 0.75f, 0.45f));
	MuzzleLight->SetCastShadows(false);
}

void ATfpsShotFx::BeginPlay()
{
	Super::BeginPlay();
	TracerMaterial = FTfpsPlaceholderAssets::MakeTinted(this, FLinearColor(4.0f, 3.2f, 1.6f));
	for (int32 S = 0; S < 8; ++S)
	{
		SurfaceMaterials.Add(FTfpsPlaceholderAssets::MakeTinted(this, SurfaceColor(static_cast<uint8>(S))));
	}
	SurfaceMaterials.Add(FTfpsPlaceholderAssets::MakeTinted(this, SurfaceColor(0xFE)));
	auto MakePool = [this](TArray<FPooled>& Pool, int32 Count, UStaticMesh* Mesh)
	{
		for (int32 I = 0; I < Count; ++I)
		{
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
			C->SetStaticMesh(Mesh);
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetCastShadow(false);
			C->SetMobility(EComponentMobility::Movable);
			C->SetupAttachment(GetRootComponent());
			C->RegisterComponent();
			C->SetVisibility(false);
			MeshRefs.Add(C);
			FPooled P;
			P.Mesh = C;
			Pool.Add(P);
		}
	};
	MakePool(Tracers, TracerPool, FTfpsPlaceholderAssets::Cylinder());
	MakePool(Impacts, ImpactPool, FTfpsPlaceholderAssets::Cube());
	for (FPooled& T : Tracers)
	{
		T.Mesh->SetMaterial(0, TracerMaterial);
	}
}

void ATfpsShotFx::SpawnTracer(const FVector& From, const FVector& To)
{
	FPooled& T = Tracers[NextTracer];
	NextTracer = (NextTracer + 1) % Tracers.Num();
	const FVector Delta = To - From;
	const float Len = static_cast<float>(Delta.Size());
	if (Len < 1.0f)
	{
		return;
	}
	// Engine cylinder: 1 m tall along Z, centred. Rotate Z onto the shot direction.
	const FRotator Rot = FRotationMatrix::MakeFromZ(Delta / Len).Rotator();
	T.Mesh->SetWorldLocationAndRotation(From + Delta * 0.5f, Rot);
	T.Mesh->SetWorldScale3D(FVector(0.012f, 0.012f, Len / 100.0f));
	T.Mesh->SetVisibility(true);
	T.Life = TracerLife;
	T.MaxLife = TracerLife;
}

void ATfpsShotFx::SpawnImpact(const FVector& At, uint8 Surface)
{
	FPooled& I = Impacts[NextImpact];
	NextImpact = (NextImpact + 1) % Impacts.Num();
	const int32 MatIndex = Surface == 0xFE ? 8 : FMath::Min<int32>(Surface, 7);
	I.Mesh->SetMaterial(0, SurfaceMaterials[MatIndex]);
	I.Mesh->SetWorldLocationAndRotation(At, FRotator::ZeroRotator);
	I.Mesh->SetWorldScale3D(FVector(Surface == 0xFE ? 0.06f : 0.04f));
	I.Mesh->SetVisibility(true);
	I.Life = Surface == 0xFE ? 0.5f : ImpactLife;
	I.MaxLife = I.Life;
}

void ATfpsShotFx::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UTfpsNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTfpsNetSubsystem>() : nullptr;
	if (Net != nullptr && Tracers.Num() > 0)
	{
		const tfps::MapDef* Map = Net->GetMap();
		for (const tfps::FxShot& S : Net->GetPendingShots())
		{
			FVector From;
			FVector To;
			uint8 Surface = 0xFF;
			if (S.bLocal)
			{
				const tfps::Vec3 Dir = tfps::DirectionFromAngles(S.YawDeg, S.PitchDeg);
				float T = 120.0f;
				tfps::EMaterial Mat = tfps::EMaterial::Concrete;
				if (Map != nullptr && tfps::RaycastSolids(*Map, S.Origin, Dir, 120.0f, T, Mat))
				{
					Surface = static_cast<uint8>(Mat);
				}
				From = FVector(S.Origin.X, S.Origin.Y, S.Origin.Z) * TfpsMetersToUnreal;
				To = FVector(S.Origin.X + Dir.X * T, S.Origin.Y + Dir.Y * T, S.Origin.Z + Dir.Z * T) * TfpsMetersToUnreal;
				// Start the tracer a little in front of the eye (the muzzle).
				From += (To - From).GetSafeNormal() * 60.0f;
				MuzzleLight->SetWorldLocation(From);
				MuzzleLight->SetIntensity(9000.0f);
				MuzzleLife = 0.035f;
			}
			else
			{
				From = FVector(S.Origin.X, S.Origin.Y, S.Origin.Z) * TfpsMetersToUnreal;
				To = FVector(S.End.X, S.End.Y, S.End.Z) * TfpsMetersToUnreal;
				Surface = S.SurfaceKind;
			}
			SpawnTracer(From, To);
			if (Surface != 0xFF)
			{
				SpawnImpact(To, Surface);
			}
		}
		Net->GetPendingShots().Reset();
	}
	auto Age = [DeltaSeconds](TArray<FPooled>& Pool)
	{
		for (FPooled& P : Pool)
		{
			if (P.Life > 0.0f)
			{
				P.Life -= DeltaSeconds;
				if (P.Life <= 0.0f)
				{
					P.Mesh->SetVisibility(false);
				}
			}
		}
	};
	Age(Tracers);
	Age(Impacts);
	if (MuzzleLife > 0.0f)
	{
		MuzzleLife -= DeltaSeconds;
		if (MuzzleLife <= 0.0f)
		{
			MuzzleLight->SetIntensity(0.0f);
		}
	}
}
