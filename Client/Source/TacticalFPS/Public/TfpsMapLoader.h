// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "NetCore/TfpsMap.h"
#include "NetCore/TfpsWeapon.h"
#include "NetCore/TfpsGadget.h"

/**
 * Loads a map's gameplay definition (Shared/Definitions/maps/<Name>.json).
 * Search order: <Project>/Content/Definitions/Maps (packaged builds, staged by
 * Scripts/package_client.*), then <Project>/../Shared/Definitions/maps (working
 * from the repository).
 */
struct FTfpsMapLoader
{
	static bool Load(const FString& MapName, tfps::MapDef& OutMap, FString& OutError, FString* OutPath = nullptr);

	/** Loads Shared/Definitions/weapons.json (same search order). */
	static bool LoadWeapons(tfps::WeaponSet& OutWeapons, FString& OutError);

	/** Loads Shared/Definitions/gadgets.json (same search order). */
	static bool LoadGadgets(tfps::GadgetSet& OutGadgets, FString& OutError);
};
