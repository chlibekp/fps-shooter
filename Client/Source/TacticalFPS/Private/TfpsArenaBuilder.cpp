// Copyright TacticalFPS project.
#include "TfpsArenaBuilder.h"
#include "TacticalFPS.h"
#include "TfpsNetSubsystem.h"
#include "TfpsPlaceholderAssets.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

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
	const UTfpsNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTfpsNetSubsystem>() : nullptr;
	if (Net != nullptr && Net->GetMap() != nullptr && Net->GetMapRevision() != BuiltRevision)
	{
		BuiltRevision = Net->GetMapRevision();
		Rebuild();
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
	TMap<uint8, UInstancedStaticMeshComponent*> ByMaterial;
	for (const tfps::Solid& S : Map->Solids)
	{
		const uint8 Key = static_cast<uint8>(S.Material);
		UInstancedStaticMeshComponent*& ISM = ByMaterial.FindOrAdd(Key);
		if (ISM == nullptr)
		{
			ISM = NewObject<UInstancedStaticMeshComponent>(this);
			ISM->SetStaticMesh(Cube);
			ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			ISM->SetMobility(EComponentMobility::Movable);
			ISM->SetupAttachment(GetRootComponent());
			if (UMaterialInstanceDynamic* MID = FTfpsPlaceholderAssets::MakeTinted(this, MaterialTint(S.Material)))
			{
				ISM->SetMaterial(0, MID);
			}
			ISM->RegisterComponent();
			MaterialMeshes.Add(ISM);
		}
		const tfps::Vec3& Mn = S.Bounds.Min;
		const tfps::Vec3& Mx = S.Bounds.Max;
		const FVector Center((Mn.X + Mx.X) * 0.5f, (Mn.Y + Mx.Y) * 0.5f, (Mn.Z + Mx.Z) * 0.5f);
		const FVector Size(Mx.X - Mn.X, Mx.Y - Mn.Y, Mx.Z - Mn.Z);
		// Engine cube: 1 m edge, centered pivot.
		ISM->AddInstance(FTransform(FRotator::ZeroRotator, Center * TfpsMetersToUnreal, Size), /*bWorldSpace=*/true);
	}
	UE_LOG(LogTfps, Log, TEXT("Built graybox for '%s': %d solids in %d material batches"), UTF8_TO_TCHAR(Map->Name.c_str()),
		static_cast<int32>(Map->Solids.size()), MaterialMeshes.Num());
}
