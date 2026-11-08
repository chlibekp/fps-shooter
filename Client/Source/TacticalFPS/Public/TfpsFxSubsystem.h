// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Fx/TfpsParticles.h"

#include "TfpsFxSubsystem.generated.h"

/**
 * Owns the client's particle effects (tfps::fx::System, unit tested without
 * the engine). Effect sources (shots, explosions, destruction, gadgets) add
 * effects here in metres; ATfpsParticleFx advances and draws them.
 */
UCLASS()
class TACTICALFPS_API UTfpsFxSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	tfps::fx::System& Fx() { return Particles; }
	const tfps::fx::System& Fx() const { return Particles; }

	/** Unreal units (cm) to the simulation's metres. */
	static tfps::Vec3 ToMeters(const FVector& Cm)
	{
		return tfps::Vec3(static_cast<float>(Cm.X / 100.0), static_cast<float>(Cm.Y / 100.0), static_cast<float>(Cm.Z / 100.0));
	}

private:
	tfps::fx::System Particles{12345, 3000};
};
