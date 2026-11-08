// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "TfpsHUD.generated.h"

class UTfpsNetSubsystem;
namespace tfps { class ClientCore; namespace ui { class Menu; } }

/**
 * Canvas HUD: crosshair, health, weapon/ammo, reload prompt, hit markers,
 * damage direction, kill feed, death/respawn screen, player list and network
 * diagnostics (F3), and the front-end / pause menus (tfps::ui::Menu).
 * Drawn with Canvas primitives 🔒 (no UMG widgets or authored art yet).
 */
UCLASS()
class TACTICALFPS_API ATfpsHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	/** Front-end / pause menu (tfps::ui::Menu), plus the lobby panel on the lobby screen. */
	void DrawMenu(const tfps::ui::Menu& Menu, const class UTfpsLobbySubsystem* Lobby, bool bOverGame);
	void DrawMatchHud(const UTfpsNetSubsystem& Net, const tfps::ClientCore& Core, float Scale);
	void DrawCombatHud(const UTfpsNetSubsystem& Net, const tfps::ClientCore& Core, float Scale);
};
