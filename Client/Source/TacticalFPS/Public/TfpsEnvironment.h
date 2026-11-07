// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "TfpsEnvironment.generated.h"

class UDirectionalLightComponent;
class USkyLightComponent;
class USkyAtmosphereComponent;
class UExponentialHeightFogComponent;

/** Dynamic outdoor lighting rig (sun, sky atmosphere, real-time sky light, fog) for Lumen. */
UCLASS()
class TACTICALFPS_API ATfpsEnvironment : public AActor
{
	GENERATED_BODY()

public:
	ATfpsEnvironment();

	UPROPERTY(VisibleAnywhere, Category = "Lighting") TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY(VisibleAnywhere, Category = "Lighting") TObjectPtr<USkyLightComponent> Sky;
	UPROPERTY(VisibleAnywhere, Category = "Lighting") TObjectPtr<USkyAtmosphereComponent> Atmosphere;
	UPROPERTY(VisibleAnywhere, Category = "Lighting") TObjectPtr<UExponentialHeightFogComponent> Fog;
};
