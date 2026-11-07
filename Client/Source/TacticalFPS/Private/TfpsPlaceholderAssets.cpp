// Copyright TacticalFPS project.
#include "TfpsPlaceholderAssets.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"

UStaticMesh* FTfpsPlaceholderAssets::Cube()
{
	return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
}

UStaticMesh* FTfpsPlaceholderAssets::Cylinder()
{
	return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
}

UStaticMesh* FTfpsPlaceholderAssets::Sphere()
{
	return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
}

UMaterialInterface* FTfpsPlaceholderAssets::BaseMaterial()
{
	return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}

UMaterialInstanceDynamic* FTfpsPlaceholderAssets::MakeTinted(UObject* Outer, const FLinearColor& Color)
{
	UMaterialInterface* Base = BaseMaterial();
	if (Base == nullptr)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
	MID->SetVectorParameterValue(TEXT("Color"), Color);
	return MID;
}
