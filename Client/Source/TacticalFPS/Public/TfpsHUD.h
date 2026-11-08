// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "TfpsHUD.generated.h"

class UTfpsNetSubsystem;
namespace tfps { class ClientCore; }

/**
 * Canvas HUD: crosshair, health, weapon/ammo, reload prompt, hit markers,
 * damage direction, kill feed, death/respawn screen, player list and network
 * diagnostics (F3). The styled UMG tactical HUD is Phase 7 work
 * (Documentation/STATUS.md).
 */
UCLASS()
class TACTICALFPS_API ATfpsHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawMatchHud(const UTfpsNetSubsystem& Net, const tfps::ClientCore& Core, float Scale);
	void DrawCombatHud(const UTfpsNetSubsystem& Net, const tfps::ClientCore& Core, float Scale);
};
