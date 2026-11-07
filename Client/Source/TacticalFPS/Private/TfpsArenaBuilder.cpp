// Copyright TacticalFPS project.
#include "TfpsArenaBuilder.h"
#include "TacticalFPS.h"
#include "TfpsNetSubsystem.h"
#include "TfpsPlaceholderAssets.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/StaticMeshComponent.h"
#include "NetCore/TfpsMap.h"

namespace
{
	FLinearColor MaterialTint(tfps::EMaterial M)
	{
		switch (M)
		{
		case tfps::EMaterial::Concrete: return FLinearColor(0.42f, 0.42f, 0.40f);
		case tfps::EMaterial::Brick: return FLinearColor(0.38f, 0.15f, 0.10f);
		case tfps::EMaterial::Wood: return FLinearColor(0.40f, 0.26f, 0.13f);
		case tfps::EMaterial::Drywall: return FLinearColor(0.78f, 0.76f, 0.72f);
		case tfps::EMaterial::Metal: return FLinearColor(0.22f, 0.28f, 0.30f);
		case tfps::EMaterial::Glass: return FLinearColor(0.55f, 0.70f, 0.75f);
		case tfps::EMaterial::Reinforced: return FLinearColor(0.20f, 0.22f, 0.25f);
		case tfps::EMaterial::Dirt: return FLinearColor(0.20f, 0.16f, 0.11f);
		}
		return FLinearColor::Gray;
	}
}

ATfpsArenaBuilder::ATfpsArenaBuilder()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f; // only polls for map changes
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ATfpsArenaBuilder::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UTfpsNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTfpsNetSubsystem>() : nullptr;
	if (Net == nullptr)
	{
		return;
	}
	if (Net->GetMap() != nullptr && Net->GetMapRevision() != BuiltRevision)
	{
		BuiltRevision = Net->GetMapRevision();
		Rebuild();
	}
	for (const uint32 Cell : Net->GetPendingDestroyedCells())
	{
		RemoveCell(Cell);
	}
	Net->GetPendingDestroyedCells().Reset();
	for (int32 I = 0; I < Debris.Num(); ++I)
	{
		if (DebrisLife[I] > 0.0f)
		{
			DebrisLife[I] -= DeltaSeconds;
			if (DebrisLife[I] <= 0.0f)
			{
				Debris[I]->SetSimulatePhysics(false);
				Debris[I]->SetVisibility(false);
			}
		}
	}
}

void ATfpsArenaBuilder::RemoveCell(uint32 Cell)
{
	const TPair<int32, int32>* Where = CellInstances.Find(Cell);
	if (Where == nullptr || !MaterialMeshes.IsValidIndex(Where->Key))
	{
		return;
	}
	UInstancedStaticMeshComponent* ISM = MaterialMeshes[Where->Key];
	FTransform T;
	if (!ISM->GetInstanceTransform(Where->Value, T, /*bWorldSpace=*/true))
	{
		return;
	}
	const FVector Center = T.GetLocation();
	const FVector Size = T.GetScale3D() * 100.0;
	// Collapse the instance instead of removing it (keeps instance indices stable).
	ISM->UpdateInstanceTransform(Where->Value, FTransform(FRotator::ZeroRotator, Center, FVector(0.001)), /*bWorldSpace=*/true,
		/*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
	SpawnDebris(Center, Size, MeshMaterial.IsValidIndex(Where->Key) ? MeshMaterial[Where->Key] : 0);
	CellInstances.Remove(Cell);
}

void ATfpsArenaBuilder::SpawnDebris(const FVector& CenterCm, const FVector& SizeCm, uint8 Material)
{
	constexpr int32 MaxDebris = 160;
	if (Debris.Num() < MaxDebris)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(FTfpsPlaceholderAssets::Cube());
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionProfileName(TEXT("PhysicsActor"));
		C->SetupAttachment(GetRootComponent());
		C->RegisterComponent();
		Debris.Add(C);
		DebrisLife.Add(0.0f);
	}
	// Three chunks per cell (glass: small shards), short-lived.
	const bool bGlass = Material == static_cast<uint8>(tfps::EMaterial::Glass);
	const int32 Chunks = bGlass ? 2 : 3;
	for (int32 K = 0; K < Chunks; ++K)
	{
		const int32 Index = NextDebris;
		NextDebris = (NextDebris + 1) % Debris.Num();
		UStaticMeshComponent* C = Debris[Index];
		if (const int32* MeshIndex = MeshForMaterial.Find(Material))
		{
			C->SetMaterial(0, MaterialMeshes[*MeshIndex]->GetMaterial(0));
		}
		const FVector Jitter(FMath::FRandRange(-0.3f, 0.3f), FMath::FRandRange(-0.3f, 0.3f), FMath::FRandRange(-0.3f, 0.3f));
		C->SetSimulatePhysics(false);
		C->SetWorldLocationAndRotation(CenterCm + SizeCm * Jitter, FRotator(FMath::FRandRange(0.0f, 360.0f), FMath::FRandRange(0.0f, 360.0f), 0.0f));
		C->SetWorldScale3D(SizeCm / 100.0 * (bGlass ? 0.25f : 0.45f));
		C->SetVisibility(true);
		C->SetSimulatePhysics(true);
		C->SetPhysicsLinearVelocity(FVector(FMath::FRandRange(-150.0f, 150.0f), FMath::FRandRange(-150.0f, 150.0f), FMath::FRandRange(50.0f, 250.0f)));
		DebrisLife[Index] = bGlass ? 2.0f : 5.0f;
	}
}

void ATfpsArenaBuilder::Rebuild()
{
	for (UInstancedStaticMeshComponent* C : MaterialMeshes)
	{
		if (C) { C->DestroyComponent(); }
	}
	MaterialMeshes.Reset();

	const UTfpsNetSubsystem* Net = GetGameInstance()->GetSubsystem<UTfpsNetSubsystem>();
	const tfps::MapDef* Map = Net->GetMap();
	UStaticMesh* Cube = FTfpsPlaceholderAssets::Cube();
	if (Map == nullptr || Cube == nullptr)
	{
		return;
	}
	for (UStaticMeshComponent* D : Debris)
	{
		if (D) { D->DestroyComponent(); }
	}
	Debris.Reset();
	DebrisLife.Reset();
	NextDebris = 0;
	CellInstances.Reset();
	MeshForMaterial.Reset();
	MeshMaterial.Reset();
	auto MeshFor = [this, Cube](tfps::EMaterial M) -> int32
	{
		const uint8 Key = static_cast<uint8>(M);
		if (const int32* Found = MeshForMaterial.Find(Key))
		{
			return *Found;
		}
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
		ISM->SetStaticMesh(Cube);
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->SetMobility(EComponentMobility::Movable);
		ISM->SetupAttachment(GetRootComponent());
		if (UMaterialInstanceDynamic* MID = FTfpsPlaceholderAssets::MakeTinted(this, MaterialTint(M)))
		{
			ISM->SetMaterial(0, MID);
		}
		ISM->RegisterComponent();
		const int32 Index = MaterialMeshes.Add(ISM);
		MeshMaterial.Add(Key);
		MeshForMaterial.Add(Key, Index);
		return Index;
	};
	auto AddBox = [this](int32 MeshIndex, const tfps::Aabb& B) -> int32
	{
		const FVector Center((B.Min.X + B.Max.X) * 0.5f, (B.Min.Y + B.Max.Y) * 0.5f, (B.Min.Z + B.Max.Z) * 0.5f);
		const FVector Size(B.Max.X - B.Min.X, B.Max.Y - B.Min.Y, B.Max.Z - B.Min.Z);
		// Engine cube: 1 m edge, centred pivot.
		return MaterialMeshes[MeshIndex]->AddInstance(FTransform(FRotator::ZeroRotator, Center * TfpsMetersToUnreal, Size), /*bWorldSpace=*/true);
	};
	// Same subdivision as the simulation (CollisionWorld), so cell ids match the server's.
	const tfps::CollisionWorld World = tfps::CollisionWorld::FromMap(*Map);
	for (const tfps::WorldSolid& S : World.Solids)
	{
		const int32 MeshIndex = MeshFor(S.Material);
		if (!S.bHasGrid)
		{
			AddBox(MeshIndex, S.Bounds);
			continue;
		}
		for (uint32 IZ = 0; IZ < S.Grid.N[2]; ++IZ)
		{
			for (uint32 IY = 0; IY < S.Grid.N[1]; ++IY)
			{
				for (uint32 IX = 0; IX < S.Grid.N[0]; ++IX)
				{
					const uint32 Id = S.Grid.First + IX + S.Grid.N[0] * (IY + S.Grid.N[1] * IZ);
					const int32 Instance = AddBox(MeshIndex, tfps::CollisionWorld::CellBounds(S.Bounds, S.Grid.N, IX, IY, IZ));
					CellInstances.Add(Id, TPair<int32, int32>(MeshIndex, Instance));
				}
			}
		}
	}
	// Cells already destroyed before we built (e.g. joined mid-match) are in the subsystem's queue.
	UE_LOG(LogTfps, Log, TEXT("Built graybox for '%s': %d solids (%d destruction cells) in %d material batches"), UTF8_TO_TCHAR(Map->Name.c_str()),
		static_cast<int32>(Map->Solids.size()), CellInstances.Num(), MaterialMeshes.Num());
}
