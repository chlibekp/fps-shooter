// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UObject;

/**
 * PLACEHOLDER ART (Phase 1). Engine basic shapes and a tinted engine
 * material are used until the Blender asset pipeline output
 * (Assets/Exports, see Documentation/ASSET_PIPELINE.md) is imported.
 * Everything that uses these is listed in Documentation/STATUS.md.
 */
struct FTfpsPlaceholderAssets
{
	static UStaticMesh* Cube();
	static UStaticMesh* Cylinder();
	static UStaticMesh* Sphere();
	static UMaterialInterface* BaseMaterial();
	/**
	 * A mesh imported from the Blender pipeline (Tools/AssetPipeline/unreal_import.py puts
	 * SM_<Asset> into /Game/Environment/Props/<Asset>), or nullptr if it is not in the
	 * project yet, so callers fall back to the placeholder shapes.
	 */
	static UStaticMesh* ImportedProp(const TCHAR* Asset);
	/** Tinted instance of the base material ("Color" vector parameter). */
	static UMaterialInstanceDynamic* MakeTinted(UObject* Outer, const FLinearColor& Color);
};
