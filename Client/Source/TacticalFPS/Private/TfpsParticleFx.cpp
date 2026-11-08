// Copyright TacticalFPS project.
#include "TfpsParticleFx.h"
#include "TacticalFPS.h"
#include "TfpsFxSubsystem.h"
#include "TfpsNetSubsystem.h"
#include "TfpsPlaceholderAssets.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"

namespace
{
	struct FLookStyle
	{
		FLinearColor Color;
		bool bRound;
	};

	FLookStyle StyleOf(tfps::fx::Look L)
	{
		switch (L)
		{
		case tfps::fx::Look::Dust: return {FLinearColor(0.42f, 0.38f, 0.33f), true};
		case tfps::fx::Look::Smoke: return {FLinearColor(0.55f, 0.56f, 0.57f), true};
		case tfps::fx::Look::Fire: return {FLinearColor(4.0f, 1.8f, 0.4f), true};
		case tfps::fx::Look::Spark: return {FLinearColor(6.0f, 3.5f, 1.0f), false};
		case tfps::fx::Look::Glass: return {FLinearColor(0.6f, 0.75f, 0.85f), false};
		case tfps::fx::Look::Splinter: return {FLinearColor(0.45f, 0.30f, 0.16f), false};
		case tfps::fx::Look::Debris: return {FLinearColor(0.35f, 0.34f, 0.32f), false};
		case tfps::fx::Look::Blood: return {FLinearColor(0.35f, 0.02f, 0.02f), true};
		case tfps::fx::Look::Count: break;
		}
		return {FLinearColor::White, true};
	}
}

ATfpsParticleFx::ATfpsParticleFx()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ATfpsParticleFx::BeginPlay()
{
	Super::BeginPlay();
	for (int32 L = 0; L < tfps::fx::kLookCount; ++L)
	{
		const FLookStyle Style = StyleOf(static_cast<tfps::fx::Look>(L));
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
		ISM->SetStaticMesh(Style.bRound ? FTfpsPlaceholderAssets::Sphere() : FTfpsPlaceholderAssets::Cube());
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->SetCastShadow(false);
		ISM->SetMobility(EComponentMobility::Movable);
		ISM->SetupAttachment(GetRootComponent());
		if (UMaterialInstanceDynamic* MID = FTfpsPlaceholderAssets::MakeTinted(this, Style.Color))
		{
			ISM->SetMaterial(0, MID);
		}
		ISM->RegisterComponent();
		Meshes.Add(ISM);
		LastCounts.Add(0);
	}
}

void ATfpsParticleFx::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UGameInstance* GI = GetGameInstance();
	UTfpsFxSubsystem* FxSys = GI ? GI->GetSubsystem<UTfpsFxSubsystem>() : nullptr;
	if (FxSys == nullptr || Meshes.Num() != tfps::fx::kLookCount)
	{
		return;
	}
	const UTfpsNetSubsystem* Net = GI->GetSubsystem<UTfpsNetSubsystem>();
	tfps::fx::System& Fx = FxSys->Fx();
	Fx.SetMap(Net != nullptr ? Net->GetMap() : nullptr);
	Fx.Update(DeltaSeconds);

	TArray<TArray<FTransform>> PerLook;
	PerLook.SetNum(tfps::fx::kLookCount);
	for (const tfps::fx::Particle& P : Fx.Particles())
	{
		const float Fade = 0.35f + 0.65f * P.Alpha();
		// Engine shapes are 100 cm across: scale = diameter in metres x fade.
		const float Scale = P.Size() * Fade * P.Shade;
		const FRotator Spin(P.Age * 400.0f, P.Age * 250.0f, 0.0f);
		PerLook[static_cast<int32>(P.Kind)].Add(FTransform(Spin, FVector(P.Pos.X, P.Pos.Y, P.Pos.Z) * TfpsMetersToUnreal, FVector(Scale)));
	}
	for (int32 L = 0; L < tfps::fx::kLookCount; ++L)
	{
		UInstancedStaticMeshComponent* ISM = Meshes[L];
		const TArray<FTransform>& T = PerLook[L];
		if (T.Num() == LastCounts[L] && T.Num() > 0)
		{
			ISM->BatchUpdateInstancesTransforms(0, T, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
		}
		else
		{
			ISM->ClearInstances();
			if (T.Num() > 0)
			{
				ISM->AddInstances(T, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true);
			}
		}
		LastCounts[L] = T.Num();
	}
}
