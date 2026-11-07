// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "TfpsArenaBuilder.generated.h"

class UInstancedStaticMeshComponent;

/**
 * Builds the graybox of the current map from its shared gameplay definition
 * (the same boxes the server and client prediction collide with), one
 * instanced mesh per material. PLACEHOLDER visuals: the boxes are tinted
 * engine cubes until modular Blender assets replace them.
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

	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> MaterialMeshes;

	int32 BuiltRevision = -1;
};
