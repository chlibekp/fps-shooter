// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "TfpsGadgetFx.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;

/**
 * Draws the server's gadget entities (grenades in flight, placed breaching
 * charges, smoke clouds) and flashbang detonations. Positions come from
 * snapshots and are smoothed between them; gadgets are not predicted.
 * PLACEHOLDER visuals (engine shapes, tinted materials) until the Blender
 * gadget meshes and Niagara smoke exist (Phase 5/7).
 */
UCLASS()
class TACTICALFPS_API ATfpsGadgetFx : public AActor
{
	GENERATED_BODY()

public:
	ATfpsGadgetFx();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FSlot
	{
		UStaticMeshComponent* Mesh = nullptr;
		uint16 EntityId = 0;
		bool bActive = false;
		bool bShown = false;
		FVector PosCm = FVector::ZeroVector;
		float SmokeAge = 0.0f;
	};

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> MeshRefs;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> FlashLight;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GrenadeMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ChargeMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ArmedMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> SmokeMaterial;

	TArray<FSlot> Slots;
	float FlashLife = 0.0f;
};
