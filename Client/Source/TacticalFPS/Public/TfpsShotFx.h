// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "TfpsShotFx.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;

/**
 * Visualises shots: muzzle flash light, tracers and impact marks, from
 * pooled components (no per-shot allocation). PLACEHOLDER visuals (engine
 * shapes, tinted materials) until Niagara effects and decals exist (Phase 7).
 * Purely cosmetic: hits are decided by the server.
 */
UCLASS()
class TACTICALFPS_API ATfpsShotFx : public AActor
{
	GENERATED_BODY()

public:
	ATfpsShotFx();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FPooled { UStaticMeshComponent* Mesh = nullptr; float Life = 0.0f; float MaxLife = 0.0f; };

	void SpawnTracer(const FVector& From, const FVector& To);
	void SpawnImpact(const FVector& At, uint8 Surface);
	void SpawnExplosion(const FVector& AtCm, float RadiusCm);

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> MeshRefs;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> MuzzleLight;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> BlastLight;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> BlastSphere;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SurfaceMaterials;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> TracerMaterial;

	TArray<FPooled> Tracers;
	TArray<FPooled> Impacts;
	int32 NextTracer = 0;
	int32 NextImpact = 0;
	float MuzzleLife = 0.0f;
	float BlastLife = 0.0f;
	float BlastRadiusCm = 0.0f;
};
