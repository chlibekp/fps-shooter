// Copyright TacticalFPS project.
#include "TfpsGadgetFx.h"
#include "TfpsAudioSubsystem.h"
#include "TfpsFxSubsystem.h"
#include "Components/AudioComponent.h"
#include "HAL/PlatformTime.h"
#include "TacticalFPS.h"
#include "TfpsNetSubsystem.h"
#include "TfpsPlaceholderAssets.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NetCore/TfpsGadget.h"
#include "NetCore/TfpsProtocol.h"

namespace
{
	constexpr int32 SlotCount = static_cast<int32>(tfps::kMaxGadgetEntities);
	constexpr float FlashLightLife = 0.25f;
	/** Smoke clouds grow to full size over this many seconds. */
	constexpr float SmokeGrowTime = 1.5f;
}

ATfpsGadgetFx::ATfpsGadgetFx()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	FlashLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FlashLight"));
	FlashLight->SetupAttachment(GetRootComponent());
	FlashLight->SetIntensity(0.0f);
	FlashLight->SetAttenuationRadius(2500.0f);
	FlashLight->SetLightColor(FLinearColor(1.0f, 1.0f, 0.95f));
	FlashLight->SetCastShadows(false);
}

void ATfpsGadgetFx::BeginPlay()
{
	Super::BeginPlay();
	GrenadeMaterial = FTfpsPlaceholderAssets::MakeTinted(this, FLinearColor(0.12f, 0.16f, 0.1f));
	ChargeMaterial = FTfpsPlaceholderAssets::MakeTinted(this, FLinearColor(0.35f, 0.28f, 0.12f));
	ArmedMaterial = FTfpsPlaceholderAssets::MakeTinted(this, FLinearColor(2.5f, 0.15f, 0.08f));
	SmokeMaterial = FTfpsPlaceholderAssets::MakeTinted(this, FLinearColor(0.62f, 0.62f, 0.6f));
	DeviceMaterial = FTfpsPlaceholderAssets::MakeTinted(this, FLinearColor(0.1f, 0.9f, 1.4f));
	// Indexed by tfps::EGadgetKind.
	const TCHAR* const Names[] = {TEXT("FragGrenade"), TEXT("FragGrenade"), TEXT("FragGrenade"), TEXT("BreachCharge"),
		TEXT("ReconDrone"), TEXT("WallCamera"), TEXT("WallCamera"), TEXT("WallCamera"), nullptr};
	for (const TCHAR* Name : Names)
	{
		ImportedMeshes.Add(Name != nullptr ? FTfpsPlaceholderAssets::ImportedProp(Name) : nullptr);
	}
	for (int32 I = 0; I < SlotCount; ++I)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetMobility(EComponentMobility::Movable);
		C->SetupAttachment(GetRootComponent());
		C->RegisterComponent();
		C->SetVisibility(false);
		MeshRefs.Add(C);
		FSlot S;
		S.Mesh = C;
		Slots.Add(S);
	}
}

void ATfpsGadgetFx::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UTfpsNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTfpsNetSubsystem>() : nullptr;
	const tfps::ClientCore* Core = Net != nullptr ? Net->GetCore() : nullptr;

	UTfpsAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTfpsAudioSubsystem>() : nullptr;
	UTfpsFxSubsystem* FxSys = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTfpsFxSubsystem>() : nullptr;
	const double Now = FPlatformTime::Seconds();
	for (FSlot& S : Slots)
	{
		S.bActive = false;
	}
	if (Core != nullptr && Slots.Num() > 0)
	{
		for (const tfps::GadgetEntityState& G : Core->Gadgets())
		{
			// Keep each entity in the slot it already has (smooth motion, smoke age).
			FSlot* Slot = nullptr;
			for (FSlot& S : Slots)
			{
				if (S.EntityId == G.Id && S.bShown) { Slot = &S; break; }
			}
			bool bNew = false;
			if (Slot == nullptr)
			{
				for (FSlot& S : Slots)
				{
					if (!S.bShown && !S.bActive) { Slot = &S; bNew = true; break; }
				}
			}
			if (Slot == nullptr)
			{
				break;
			}
			Slot->bActive = true;
			Slot->EntityId = G.Id;
			const tfps::Vec3 P = G.Position();
			const FVector Target = FVector(P.X, P.Y, P.Z) * TfpsMetersToUnreal;
			Slot->PosCm = bNew ? Target : FMath::Lerp(Slot->PosCm, Target, FMath::Min(1.0f, DeltaSeconds * 25.0f));
			const tfps::GadgetDef* Def = Net->GetGadgets().Get(G.Gadget);
			if (bNew)
			{
				Slot->LastPhase = 255;
				Slot->LastTarget = Target;
				Slot->LastMoveTime = Now;
				Slot->LastVz = 0.0f;
			}
			if (Audio != nullptr && Def != nullptr)
			{
				const bool bPhaseChanged = G.Phase != Slot->LastPhase;
				if (bPhaseChanged && G.Phase == tfps::GadgetPhase::Smoking)
				{
					Audio->PlayAt(tfps::audio::Cue::SmokeHiss, Target);
				}
				if (bPhaseChanged && Slot->LastPhase != 255 && G.Phase == tfps::GadgetPhase::Armed && tfps::IsPlacedGadget(Def->Kind))
				{
					Audio->PlayAt(tfps::audio::Cue::DeviceBeep, Target);
				}
				if (tfps::IsGrenade(Def->Kind))
				{
					// Snapshots move the entity in steps: estimate vertical speed between changes.
					if (!Target.Equals(Slot->LastTarget, 0.1f))
					{
						const float Dt = static_cast<float>(FMath::Max(0.001, Now - Slot->LastMoveTime));
						const float Vz = static_cast<float>(Target.Z - Slot->LastTarget.Z) / Dt;
						const bool bBounce = G.Phase == tfps::GadgetPhase::Flying && Slot->LastVz < -150.0f && Vz > 50.0f;
						if (bBounce)
						{
							Audio->PlayAt(tfps::audio::Cue::GrenadeBounce, Target, 0.8f);
						}
						Slot->LastVz = Vz;
						Slot->LastTarget = Target;
						Slot->LastMoveTime = Now;
					}
					if (bPhaseChanged && Slot->LastPhase == tfps::GadgetPhase::Flying && G.Phase == tfps::GadgetPhase::Resting)
					{
						Audio->PlayAt(tfps::audio::Cue::GrenadeBounce, Target, 0.6f);
					}
				}
				if (Def->Kind == tfps::EGadgetKind::Drone)
				{
					if (!Slot->Loop.IsValid()) { Slot->Loop = Audio->StartLoop(tfps::audio::Cue::DroneHum, Target, 0.8f); }
					else { Slot->Loop->SetWorldLocation(Target); }
				}
			}
			Slot->LastPhase = G.Phase;
			UStaticMeshComponent* M = Slot->Mesh;
			const int32 KindIndex = Def != nullptr ? static_cast<int32>(Def->Kind) : -1;
			UStaticMesh* Imported = ImportedMeshes.IsValidIndex(KindIndex) ? ImportedMeshes[KindIndex].Get() : nullptr;
			if (Imported != nullptr && G.Phase != tfps::GadgetPhase::Smoking)
			{
				// Real asset: authored in meters (imported at 100 uu/m), with its own material.
				Slot->SmokeAge = 0.0f;
				M->SetStaticMesh(Imported);
				M->SetMaterial(0, Imported->GetMaterial(0));
				M->SetWorldScale3D(FVector(1.0f));
				M->SetWorldLocation(Slot->PosCm);
			}
			else if (G.Phase == tfps::GadgetPhase::Smoking && FxSys != nullptr)
			{
				// The particle cloud is the smoke; hide the grenade itself.
				const float Radius = Def != nullptr ? Def->Radius : 4.0f;
				const float Duration = Def != nullptr ? Def->Duration : 15.0f;
				FxSys->Fx().Cloud(G.Id, UTfpsFxSubsystem::ToMeters(Target), Radius, Duration + 2.0f);
				Slot->bSmokeCloud = true;
				M->SetWorldScale3D(FVector(0.001f));
				M->SetWorldLocation(Target);
			}
			else if (G.Phase == tfps::GadgetPhase::Smoking)
			{
				Slot->SmokeAge = bNew ? SmokeGrowTime : Slot->SmokeAge + DeltaSeconds;
				const float RadiusCm = (Def != nullptr ? Def->Radius : 4.0f) * TfpsMetersToUnreal;
				const float Grow = FMath::Clamp(Slot->SmokeAge / SmokeGrowTime, 0.1f, 1.0f);
				M->SetStaticMesh(FTfpsPlaceholderAssets::Sphere());
				M->SetMaterial(0, SmokeMaterial);
				// Engine sphere is 100 cm across; flatten slightly so the cloud hugs the floor.
				M->SetWorldScale3D(FVector(2.0f * RadiusCm / 100.0f * Grow, 2.0f * RadiusCm / 100.0f * Grow, 1.4f * RadiusCm / 100.0f * Grow));
				M->SetWorldLocation(Slot->PosCm + FVector(0.0f, 0.0f, 0.4f * RadiusCm * Grow));
			}
			else if (Def != nullptr && Def->Kind != tfps::EGadgetKind::Breach && tfps::IsPlacedGadget(Def->Kind))
			{
				// Camera / motion sensor / jammer: small box, armed = lit.
				Slot->SmokeAge = 0.0f;
				M->SetStaticMesh(FTfpsPlaceholderAssets::Cube());
				M->SetMaterial(0, G.Phase == tfps::GadgetPhase::Armed ? DeviceMaterial : ChargeMaterial);
				M->SetWorldScale3D(FVector(0.16f));
				M->SetWorldLocation(Slot->PosCm);
			}
			else if (Def != nullptr && Def->Kind == tfps::EGadgetKind::Drone)
			{
				Slot->SmokeAge = 0.0f;
				M->SetStaticMesh(FTfpsPlaceholderAssets::Cube());
				M->SetMaterial(0, DeviceMaterial);
				M->SetWorldScale3D(FVector(0.3f, 0.3f, 0.12f));
				M->SetWorldLocation(Slot->PosCm);
			}
			else if (G.Phase == tfps::GadgetPhase::Planted || G.Phase == tfps::GadgetPhase::Armed)
			{
				Slot->SmokeAge = 0.0f;
				M->SetStaticMesh(FTfpsPlaceholderAssets::Cube());
				M->SetMaterial(0, G.Phase == tfps::GadgetPhase::Armed ? ArmedMaterial : ChargeMaterial);
				M->SetWorldScale3D(FVector(0.12f, 0.12f, 0.2f));
				M->SetWorldLocation(Slot->PosCm);
			}
			else
			{
				Slot->SmokeAge = 0.0f;
				M->SetStaticMesh(FTfpsPlaceholderAssets::Sphere());
				M->SetMaterial(0, GrenadeMaterial);
				M->SetWorldScale3D(FVector(0.12f));
				M->SetWorldLocation(Slot->PosCm);
			}
			if (!Slot->bShown)
			{
				M->SetVisibility(true);
				Slot->bShown = true;
			}
		}
	}
	for (FSlot& S : Slots)
	{
		if (!S.bActive && S.bShown)
		{
			S.Mesh->SetVisibility(false);
			S.bShown = false;
		}
		if (!S.bActive && S.bSmokeCloud)
		{
			if (FxSys != nullptr) { FxSys->Fx().StopCloud(S.EntityId); }
			S.bSmokeCloud = false;
		}
		if (!S.bActive && S.Loop.IsValid())
		{
			S.Loop->Stop();
			S.Loop.Reset();
		}
	}

	if (Net != nullptr)
	{
		for (const FVector& At : Net->GetPendingFlashes())
		{
			FlashLight->SetWorldLocation(At * TfpsMetersToUnreal);
			if (FxSys != nullptr) { FxSys->Fx().Flash(UTfpsFxSubsystem::ToMeters(At * TfpsMetersToUnreal)); }
			FlashLight->SetIntensity(400000.0f);
			FlashLife = FlashLightLife;
		}
		Net->GetPendingFlashes().Reset();
	}
	if (FlashLife > 0.0f)
	{
		FlashLife -= DeltaSeconds;
		FlashLight->SetIntensity(FlashLife > 0.0f ? 400000.0f * (FlashLife / FlashLightLife) : 0.0f);
	}
}
