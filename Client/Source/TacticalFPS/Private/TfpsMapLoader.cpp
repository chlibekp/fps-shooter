// Copyright TacticalFPS project.
#include "TfpsMapLoader.h"
#include "TacticalFPS.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <string>

bool FTfpsMapLoader::Load(const FString& MapName, tfps::MapDef& OutMap, FString& OutError, FString* OutPath)
{
	// Map names are simple identifiers; refuse anything that could escape the directory.
	for (const TCHAR C : MapName)
	{
		if (!FChar::IsAlnum(C) && C != TEXT('_') && C != TEXT('-'))
		{
			OutError = FString::Printf(TEXT("invalid map name '%s'"), *MapName);
			return false;
		}
	}
	const FString FileName = MapName + TEXT(".json");
	const TArray<FString> Candidates = {
		FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Definitions"), TEXT("Maps"), FileName),
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("Shared"), TEXT("Definitions"), TEXT("maps"), FileName),
	};
	for (const FString& Path : Candidates)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			continue;
		}
		const std::string Utf8(TCHAR_TO_UTF8(*Text));
		std::string Error;
		if (!tfps::ParseMapJson(Utf8, OutMap, Error))
		{
			OutError = FString::Printf(TEXT("%s: %s"), *Path, UTF8_TO_TCHAR(Error.c_str()));
			return false;
		}
		if (OutPath != nullptr)
		{
			*OutPath = FPaths::ConvertRelativePathToFull(Path);
		}
		UE_LOG(LogTfps, Log, TEXT("Loaded map '%s' from %s (%d solids, hash %016llx)"), *MapName, *Path,
			static_cast<int32>(OutMap.Solids.size()), static_cast<unsigned long long>(OutMap.GeometryHash()));
		return true;
	}
	OutError = FString::Printf(TEXT("map '%s' not found (looked in %s and %s)"), *MapName, *Candidates[0], *Candidates[1]);
	return false;
}

bool FTfpsMapLoader::LoadWeapons(tfps::WeaponSet& OutWeapons, FString& OutError)
{
	const TArray<FString> Candidates = {
		FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Definitions"), TEXT("weapons.json")),
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("Shared"), TEXT("Definitions"), TEXT("weapons.json")),
	};
	for (const FString& Path : Candidates)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			continue;
		}
		const std::string Utf8(TCHAR_TO_UTF8(*Text));
		std::string Error;
		if (!tfps::ParseWeaponsJson(Utf8, OutWeapons, Error))
		{
			OutError = FString::Printf(TEXT("%s: %s"), *Path, UTF8_TO_TCHAR(Error.c_str()));
			return false;
		}
		UE_LOG(LogTfps, Log, TEXT("Loaded %d weapons from %s (hash %016llx)"), static_cast<int32>(OutWeapons.Weapons.size()), *Path,
			static_cast<unsigned long long>(OutWeapons.Hash()));
		return true;
	}
	OutError = FString::Printf(TEXT("weapons.json not found (looked in %s and %s)"), *Candidates[0], *Candidates[1]);
	return false;
}

bool FTfpsMapLoader::LoadGadgets(tfps::GadgetSet& OutGadgets, FString& OutError)
{
	const TArray<FString> Candidates = {
		FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Definitions"), TEXT("gadgets.json")),
		FPaths::Combine(FPaths::ProjectDir(), TEXT(".."), TEXT("Shared"), TEXT("Definitions"), TEXT("gadgets.json")),
	};
	for (const FString& Path : Candidates)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			continue;
		}
		const std::string Utf8(TCHAR_TO_UTF8(*Text));
		std::string Error;
		if (!tfps::ParseGadgetsJson(Utf8, OutGadgets, Error))
		{
			OutError = FString::Printf(TEXT("%s: %s"), *Path, UTF8_TO_TCHAR(Error.c_str()));
			return false;
		}
		UE_LOG(LogTfps, Log, TEXT("Loaded %d gadgets from %s"), static_cast<int32>(OutGadgets.Gadgets.size()), *Path);
		return true;
	}
	OutError = FString::Printf(TEXT("gadgets.json not found (looked in %s and %s)"), *Candidates[0], *Candidates[1]);
	return false;
}
