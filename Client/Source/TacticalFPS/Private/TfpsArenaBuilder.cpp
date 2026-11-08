// Copyright TacticalFPS project.
#include "TfpsArenaBuilder.h"
#include "TfpsAudioSubsystem.h"

#include <cstring>
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
	UTfpsAudioSubsystem* Audio = GetGameInstance()->GetSubsystem<UTfpsAudioSubsystem>();
	if (Net->GetMap() != nullptr && Net->GetMapRevision() != BuiltRevision)
	{
		BuiltRevision = Net->GetMapRevision();
		Rebuild();
		// The map's own solids are not "placed" barricades: no sound for them.
		Audio = nullptr;
	}
	// Solids added at runtime (barricades) must exist before their cells can be destroyed.
	if (const tfps::ClientCore* Core = Net->GetCore())
	{
		const tfps::CollisionWorld& World = Core->World();
		auto Centre = [](const tfps::WorldSolid& S)
		{
			return FVector((S.Bounds.Min.X + S.Bounds.Max.X) * 0.5f, (S.Bounds.Min.Y + S.Bounds.Max.Y) * 0.5f,
				(S.Bounds.Min.Z + S.Bounds.Max.Z) * 0.5f) * TfpsMetersToUnreal;
		};
		while (BuiltRevision >= 0 && BuiltSolids < static_cast<int32>(World.Solids.size()))
		{
			const tfps::WorldSolid& Added = World.Solids[static_cast<size_t>(BuiltSolids)];
			AddSolid(Added);
			++BuiltSolids;
			if (Audio != nullptr) { Audio->PlayAt(tfps::audio::Cue::BarricadePlace, Centre(Added)); }
		}
		for (const uint32 Index : Net->GetPendingReinforced())
		{
			if (Index < World.Solids.size())
			{
				ReinforceSolid(World.Solids[Index]);
				if (Audio != nullptr) { Audio->PlayAt(tfps::audio::Cue::Reinforce, Centre(World.Solids[Index])); }
			}
		}
	}
	Net->GetPendingReinforced().Reset();
	for (const uint32 Cell : Net->GetPendingDestroyedCells())
	{
		RemoveCell(Cell);
	}
	Net->GetPendingDestroyedCells().Reset();
	// One breaking sound per material per frame, at the middle of what broke.
	UTfpsAudioSubsystem* BreakAudio = GetGameInstance()->GetSubsystem<UTfpsAudioSubsystem>();
	for (const TPair<uint8, FBreakSound>& B : BreakSounds)
	{
		if (BreakAudio == nullptr || B.Value.Count == 0)
		{
			continue;
		}
		tfps::audio::Cue Cue = tfps::audio::Cue::WallBreak;
		switch (static_cast<tfps::EMaterial>(B.Key))
		{
		case tfps::EMaterial::Glass: Cue = tfps::audio::Cue::GlassBreak; break;
		case tfps::EMaterial::Wood: Cue = tfps::audio::Cue::WoodBreak; break;
		case tfps::EMaterial::Metal:
		case tfps::EMaterial::Reinforced: Cue = tfps::audio::Cue::ImpactMetal; break;
		default: break;
		}
		BreakAudio->PlayAt(Cue, B.Value.Sum / static_cast<double>(B.Value.Count), FMath::Min(1.0f, 0.6f + 0.05f * B.Value.Count));
	}
	BreakSounds.Reset();
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

int32 ATfpsArenaBuilder::MeshFor(tfps::EMaterial M)
{
	const uint8 Key = static_cast<uint8>(M);
	if (const int32* Found = MeshForMaterial.Find(Key))
	{
		return *Found;
	}
	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
	ISM->SetStaticMesh(FTfpsPlaceholderAssets::Cube());
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
}

int32 ATfpsArenaBuilder::AddBox(int32 MeshIndex, const tfps::Aabb& B)
{
	const FVector Center((B.Min.X + B.Max.X) * 0.5f, (B.Min.Y + B.Max.Y) * 0.5f, (B.Min.Z + B.Max.Z) * 0.5f);
	const FVector Size(B.Max.X - B.Min.X, B.Max.Y - B.Min.Y, B.Max.Z - B.Min.Z);
	// Engine cube: 1 m edge, centred pivot.
	return MaterialMeshes[MeshIndex]->AddInstance(FTransform(FRotator::ZeroRotator, Center * TfpsMetersToUnreal, Size), /*bWorldSpace=*/true);
}

UStaticMesh* ATfpsArenaBuilder::MatchProp(const tfps::WorldSolid& S, const char* Tag, float& OutYaw) const
{
	const float SX = S.Bounds.Max.X - S.Bounds.Min.X;
	const float SY = S.Bounds.Max.Y - S.Bounds.Min.Y;
	const float SZ = S.Bounds.Max.Z - S.Bounds.Min.Z;
	auto Near = [](float A, float B) { return FMath::Abs(A - B) < 0.06f; };
	auto Find = [this](const TCHAR* Name) -> UStaticMesh*
	{
		const TObjectPtr<UStaticMesh>* M = PropMeshes.Find(FName(Name));
		return M != nullptr ? M->Get() : nullptr;
	};
	// The meshes are authored with their long/wide side along X (thin along Y), except
	// the barrier (long along Y); rotate 90 degrees when the solid is the other way round.
	if (std::strcmp(Tag, "crate") == 0 && Near(SX, 1.2f) && Near(SY, 1.2f) && Near(SZ, 1.2f))
	{
		OutYaw = 0.0f;
		return Find(TEXT("WoodCrate"));
	}
	if (std::strcmp(Tag, "barrier") == 0 && Near(SZ, 0.9f) && ((Near(SX, 3.0f) && Near(SY, 0.6f)) || (Near(SX, 0.6f) && Near(SY, 3.0f))))
	{
		OutYaw = SX > SY ? 90.0f : 0.0f;
		return Find(TEXT("JerseyBarrier"));
	}
	if (std::strcmp(Tag, "door") == 0 && Near(SZ, 2.15f) && (Near(FMath::Max(SX, SY), 1.0f)))
	{
		OutYaw = SX > SY ? 0.0f : 90.0f;
		return Find(TEXT("WoodDoor"));
	}
	return nullptr;
}

void ATfpsArenaBuilder::AddProp(UStaticMesh* Mesh, const tfps::Aabb& B, float Yaw)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetMobility(EComponentMobility::Movable);
	C->SetupAttachment(GetRootComponent());
	C->RegisterComponent();
	// Pivot at the bottom centre (pipeline convention).
	const FVector BottomCentre((B.Min.X + B.Max.X) * 0.5f, (B.Min.Y + B.Max.Y) * 0.5f, B.Min.Z);
	C->SetWorldLocationAndRotation(BottomCentre * TfpsMetersToUnreal, FRotator(0.0f, Yaw, 0.0f));
	DressingProps.Add(C);
}

void ATfpsArenaBuilder::AddSolid(const tfps::WorldSolid& S, const char* Tag)
{
	const int32 MeshIndex = MeshFor(S.Material);
	// Window frames decorate glass panes and stay when the glass breaks.
	if (std::strcmp(Tag, "window_glass") == 0)
	{
		const TObjectPtr<UStaticMesh>* Frame = PropMeshes.Find(FName(TEXT("WindowFrame")));
		const float SX = S.Bounds.Max.X - S.Bounds.Min.X;
		const float SY = S.Bounds.Max.Y - S.Bounds.Min.Y;
		const float SZ = S.Bounds.Max.Z - S.Bounds.Min.Z;
		if (Frame != nullptr && Frame->Get() != nullptr && FMath::Abs(FMath::Max(SX, SY) - 1.4f) < 0.06f && FMath::Abs(SZ - 1.1f) < 0.06f)
		{
			AddProp(Frame->Get(), S.Bounds, SX > SY ? 0.0f : 90.0f);
		}
	}
	float Yaw = 0.0f;
	UStaticMesh* Prop = MatchProp(S, Tag, Yaw);
	if (!S.bHasGrid)
	{
		if (Prop != nullptr)
		{
			AddProp(Prop, S.Bounds, Yaw);
		}
		else
		{
			AddBox(MeshIndex, S.Bounds);
		}
		return;
	}
	// Destructible solid with a prop: the prop stands in for its cells until the
	// first cell breaks; then the surviving cells are revealed (gameplay truth).
	int32 Dressing = INDEX_NONE;
	if (Prop != nullptr)
	{
		AddProp(Prop, S.Bounds, Yaw);
		FDressing D;
		D.Prop = DressingProps.Last();
		Dressing = Dressings.Add(D);
	}
	for (uint32 IZ = 0; IZ < S.Grid.N[2]; ++IZ)
	{
		for (uint32 IY = 0; IY < S.Grid.N[1]; ++IY)
		{
			for (uint32 IX = 0; IX < S.Grid.N[0]; ++IX)
			{
				const uint32 Id = S.Grid.First + IX + S.Grid.N[0] * (IY + S.Grid.N[1] * IZ);
				const tfps::Aabb CB = tfps::CollisionWorld::CellBounds(S.Bounds, S.Grid.N, IX, IY, IZ);
				const int32 Instance = AddBox(MeshIndex, CB);
				CellInstances.Add(Id, TPair<int32, int32>(MeshIndex, Instance));
				if (Dressing != INDEX_NONE)
				{
					// Hidden behind the prop for now; remember where it belongs.
					FTransform T;
					MaterialMeshes[MeshIndex]->GetInstanceTransform(Instance, T, /*bWorldSpace=*/true);
					Dressings[Dressing].Cells.Add(Id, T);
					MaterialMeshes[MeshIndex]->UpdateInstanceTransform(Instance, FTransform(FRotator::ZeroRotator, T.GetLocation(), FVector(0.001)),
						true, true, true);
					CellDressing.Add(Id, Dressing);
				}
			}
		}
	}
}

void ATfpsArenaBuilder::ReinforceSolid(const tfps::WorldSolid& S)
{
	if (!S.bHasGrid)
	{
		return;
	}
	// Move the solid's remaining cells into the reinforced (metal-plated) batch.
	const int32 Target = MeshFor(tfps::EMaterial::Reinforced);
	for (uint32 Id = S.Grid.First; Id < S.Grid.First + S.Grid.N[0] * S.Grid.N[1] * S.Grid.N[2]; ++Id)
	{
		TPair<int32, int32>* Where = CellInstances.Find(Id);
		if (Where == nullptr || Where->Key == Target)
		{
			continue;
		}
		UInstancedStaticMeshComponent* Old = MaterialMeshes[Where->Key];
		FTransform T;
		if (!Old->GetInstanceTransform(Where->Value, T, /*bWorldSpace=*/true))
		{
			continue;
		}
		Old->UpdateInstanceTransform(Where->Value, FTransform(FRotator::ZeroRotator, T.GetLocation(), FVector(0.001)), true, true, true);
		const int32 Instance = MaterialMeshes[Target]->AddInstance(T, /*bWorldSpace=*/true);
		*Where = TPair<int32, int32>(Target, Instance);
	}
}

void ATfpsArenaBuilder::RemoveCell(uint32 Cell)
{
	if (const int32* DI = CellDressing.Find(Cell))
	{
		FDressing& D = Dressings[*DI];
		if (D.bIntact)
		{
			// First damage: swap the prop for its cells.
			D.bIntact = false;
			if (D.Prop) { D.Prop->SetVisibility(false); }
			for (const TPair<uint32, FTransform>& C : D.Cells)
			{
				if (const TPair<int32, int32>* W = CellInstances.Find(C.Key))
				{
					MaterialMeshes[W->Key]->UpdateInstanceTransform(W->Value, C.Value, true, true, true);
				}
			}
		}
	}
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
	const uint8 Material = MeshMaterial.IsValidIndex(Where->Key) ? MeshMaterial[Where->Key] : 0;
	SpawnDebris(Center, Size, Material);
	FBreakSound& Break = BreakSounds.FindOrAdd(Material);
	Break.Sum = Break.Sum + Center;
	++Break.Count;
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
	// Same subdivision as the simulation (CollisionWorld), so cell ids match the server's.
	const tfps::CollisionWorld World = tfps::CollisionWorld::FromMap(*Map);
	// Props imported from the Blender pipeline (nullptr until imported: graybox only).
	for (UStaticMeshComponent* P : DressingProps)
	{
		if (P) { P->DestroyComponent(); }
	}
	DressingProps.Reset();
	Dressings.Reset();
	CellDressing.Reset();
	PropMeshes.Reset();
	for (const TCHAR* Name : {TEXT("WoodCrate"), TEXT("JerseyBarrier"), TEXT("WoodDoor"), TEXT("WindowFrame")})
	{
		PropMeshes.Add(FName(Name), FTfpsPlaceholderAssets::ImportedProp(Name));
	}
	for (size_t I = 0; I < World.Solids.size(); ++I)
	{
		AddSolid(World.Solids[I], I < Map->Solids.size() ? Map->Solids[I].Tag.c_str() : "");
	}
	BuiltSolids = static_cast<int32>(World.Solids.size());
	// Cells already destroyed before we built (e.g. joined mid-match) are in the subsystem's queue.
	UE_LOG(LogTfps, Log, TEXT("Built graybox for '%s': %d solids (%d destruction cells) in %d material batches"), UTF8_TO_TCHAR(Map->Name.c_str()),
		static_cast<int32>(Map->Solids.size()), CellInstances.Num(), MaterialMeshes.Num());
}
