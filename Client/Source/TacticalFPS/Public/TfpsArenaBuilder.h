// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NetCore/TfpsMap.h"

#include "TfpsArenaBuilder.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;

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
	void AddSolid(const tfps::WorldSolid& S, const char* Tag = "");
	/** Imported prop for a solid whose tag and size match one (nullptr = graybox), and its yaw. */
	UStaticMesh* MatchProp(const tfps::WorldSolid& S, const char* Tag, float& OutYaw) const;
	void AddProp(UStaticMesh* Mesh, const tfps::Aabb& B, float Yaw);
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

	/** A destructible solid shown as an imported prop until its first cell breaks. */
	struct FDressing
	{
		UStaticMeshComponent* Prop = nullptr;
		bool bIntact = true;
		TMap<uint32, FTransform> Cells;
	};

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> DressingProps;
	UPROPERTY()
	TMap<FName, TObjectPtr<UStaticMesh>> PropMeshes;
	TArray<FDressing> Dressings;
	TMap<uint32, int32> CellDressing;

	int32 BuiltRevision = -1;
	/** Solids of the collision world drawn so far (map solids + runtime barricades). */
	int32 BuiltSolids = 0;
};
