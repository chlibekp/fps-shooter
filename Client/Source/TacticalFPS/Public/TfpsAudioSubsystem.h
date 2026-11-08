// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Audio/TfpsSynth.h"

#include "TfpsAudioSubsystem.generated.h"

class UAudioComponent;
class USoundAttenuation;
class USoundBase;
class USoundWaveProcedural;
namespace tfps { struct MapDef; }

/** Turns a moving player's feet position into footstep / landing events. */
struct FTfpsStepTracker
{
	/** Returns true when a footstep is due; sets bOutLanded on touching down after a fall. */
	bool Update(const FVector& FeetMeters, bool bOnGround, float StrideMeters, bool& bOutLanded);

private:
	FVector Last = FVector::ZeroVector;
	float Travelled = 0.0f;
	bool bHasLast = false;
	bool bWasOnGround = true;
	float AirZ = 0.0f;
};

/**
 * Plays the procedural game sounds (tfps::audio, rendered on first use and
 * cached) through USoundWaveProcedural, so no audio asset has to be imported.
 * World sounds use distance attenuation per cue (CueInfo::Range); own-player
 * and interface sounds play in 2D. Volumes come from the user settings.
 *
 * Not yet run in Unreal Engine (mock-checked only); the synthesis itself is
 * unit tested in Client/NetCoreTests.
 */
UCLASS()
class TACTICALFPS_API UTfpsAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	void SetVolumes(float Master, float Effects);

	/** World sound at a location in Unreal units (a random variation of the cue). */
	void PlayAt(tfps::audio::Cue Cue, const FVector& LocationCm, float Volume = 1.0f);
	/** Own-player / interface sound without position. */
	void Play2D(tfps::audio::Cue Cue, float Volume = 1.0f);
	/** Starts a looping cue (drone motor); move it with SetWorldLocation and Stop() it when done. */
	UAudioComponent* StartLoop(tfps::audio::Cue Cue, const FVector& LocationCm, float Volume = 1.0f);

	/** Footstep on whatever the map has under the feet (meters); 2D for the local player. */
	void Footstep(const tfps::MapDef* Map, const FVector& FeetMeters, float Volume, bool bLocal);
	/** Map material id under a point (tfps::EMaterial; concrete if nothing is found). */
	static uint8 GroundMaterial(const tfps::MapDef* Map, const FVector& FeetMeters);

private:
	using FPcm = TSharedPtr<const TArray<uint8>, ESPMode::ThreadSafe>;
	FPcm Pcm(tfps::audio::Cue Cue, uint32 Variant);
	USoundWaveProcedural* MakeWave(const FPcm& Data, bool bLoop);
	USoundAttenuation* AttenuationFor(tfps::audio::Cue Cue);
	/** False when this cue already played too often in the last few frames (e.g. many cells breaking at once). */
	bool Admit(tfps::audio::Cue Cue);
	float LevelFor(tfps::audio::Cue Cue, float Volume) const;
	void Prune();

	TMap<uint32, FPcm> Cache;
	UPROPERTY() TMap<uint8, TObjectPtr<USoundAttenuation>> Attenuations;
	/** Waves currently playing are referenced here so they are not garbage collected mid-sound. */
	UPROPERTY() TArray<TObjectPtr<USoundBase>> Playing;
	TArray<double> PlayingUntil;
	double CueLastWindow[tfps::audio::kCueCount] = {};
	int32 CueCountInWindow[tfps::audio::kCueCount] = {};
	uint32 VariantCounter = 0;
	float MasterVolume = 0.8f;
	float EffectsVolume = 1.0f;
};
