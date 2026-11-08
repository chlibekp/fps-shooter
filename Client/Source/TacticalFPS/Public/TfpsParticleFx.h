// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fx/TfpsParticles.h"

#include "TfpsParticleFx.generated.h"

class UInstancedStaticMeshComponent;

/**
 * Advances the client's particle effects (UTfpsFxSubsystem) every frame and
 * draws them with one instanced mesh per look (dust, smoke, fire, sparks,
 * glass, splinters, debris, blood). 🔒 Placeholder look: engine spheres and
 * cubes in flat tints; fading is shown by shrinking, since the placeholder
 * material is opaque. Replaced by Niagara systems with authored textures.
 */
UCLASS()
class TACTICALFPS_API ATfpsParticleFx : public AActor
{
	GENERATED_BODY()

public:
	ATfpsParticleFx();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Meshes;
	/** Instances per look last frame (to resize instead of clearing when the count is unchanged). */
	TArray<int32> LastCounts;
};
