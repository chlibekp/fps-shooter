// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NetCore/TfpsMap.h"

#include "TfpsArenaBuilder.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;

/**
 * Builds the graybox of the current map from its shared gameplay definition
 * (the same boxes the server and client prediction collide with), one
 * instanced mesh per material. Destructible solids are drawn as their
 * destruction cells; destroyed cells disappear and spawn local physics
 * debris (debris is cosmetic and never replicated). PLACEHOLDER visuals:
 * tinted engine cubes until modular Blender assets replace them.
 */
UCLASS()
class TACTICALFPS_API ATfpsArenaBuilder : public AActor
{
	GENERATED_BODY()

public:
	ATfpsArenaBuilder();

protected:
	virtual void Tick(float DeltaSeconds) override;

private:
	void Rebuild();

	void RemoveCell(uint32 Cell);
	int32 MeshFor(tfps::EMaterial M);
	int32 AddBox(int32 MeshIndex, const tfps::Aabb& B);
	/** Adds a solid (whole box, or one instance per destruction cell). */
	void AddSolid(const tfps::WorldSolid& S);
	void ReinforceSolid(const tfps::WorldSolid& S);
	void SpawnDebris(const FVector& CenterCm, const FVector& SizeCm, uint8 Material);

	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> MaterialMeshes;

	/** Pooled client-side physics debris (never replicated). */
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Debris;
	TArray<float> DebrisLife;
	int32 NextDebris = 0;

	/** Cell id -> (material mesh index, instance index). */
	TMap<uint32, TPair<int32, int32>> CellInstances;
	TMap<uint8, int32> MeshForMaterial;
	TArray<uint8> MeshMaterial;

	int32 BuiltRevision = -1;
	/** Solids of the collision world drawn so far (map solids + runtime barricades). */
	int32 BuiltSolids = 0;
};
