// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "TfpsHUD.generated.h"

/**
 * Phase 1 HUD: crosshair, connection status, player list with connection
 * state, and network diagnostics (F3). The styled tactical HUD (UMG) is
 * Phase 7 work, see Documentation/STATUS.md.
 */
UCLASS()
class TACTICALFPS_API ATfpsHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
