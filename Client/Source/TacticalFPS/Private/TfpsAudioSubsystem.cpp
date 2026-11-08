// Copyright TacticalFPS project.
#include "TfpsAudioSubsystem.h"
#include "TacticalFPS.h"

#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "NetCore/TfpsMap.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWaveProcedural.h"

namespace
{
	/** At most this many plays of one cue per window (the rest are dropped, they would only add noise). */
	constexpr int32 kMaxPerWindow = 4;
	constexpr double kWindowSeconds = 0.05;
}

void UTfpsAudioSubsystem::SetVolumes(float Master, float Effects)
{
	MasterVolume = FMath::Clamp(Master, 0.0f, 1.0f);
	EffectsVolume = FMath::Clamp(Effects, 0.0f, 1.0f);
}

float UTfpsAudioSubsystem::LevelFor(tfps::audio::Cue Cue, float Volume) const
{
	// Interface sounds follow the master volume only; everything else is an effect.
	const bool bInterface = Cue == tfps::audio::Cue::UiMove || Cue == tfps::audio::Cue::UiAccept;
	return Volume * MasterVolume * (bInterface ? 1.0f : EffectsVolume);
}

UTfpsAudioSubsystem::FPcm UTfpsAudioSubsystem::Pcm(tfps::audio::Cue Cue, uint32 Variant)
{
	const tfps::audio::CueInfo& Info = tfps::audio::Info(Cue);
	const uint32 V = Variant % static_cast<uint32>(FMath::Max(1, Info.Variants));
	const uint32 Key = static_cast<uint32>(Cue) << 8 | V;
	if (const FPcm* Found = Cache.Find(Key))
	{
		return *Found;
	}
	const std::vector<int16_t> Samples = tfps::audio::Render(Cue, V);
	TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> Bytes = MakeShared<TArray<uint8>, ESPMode::ThreadSafe>();
	Bytes->SetNumUninitialized(static_cast<int32>(Samples.size() * sizeof(int16_t)));
	FMemory::Memcpy(Bytes->GetData(), Samples.data(), Samples.size() * sizeof(int16_t));
	FPcm Data = Bytes;
	Cache.Add(Key, Data);
	return Data;
}

USoundWaveProcedural* UTfpsAudioSubsystem::MakeWave(const FPcm& Data, bool bLoop)
{
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	Wave->SetSampleRate(tfps::audio::kSampleRate);
	Wave->NumChannels = 1;
	Wave->SoundGroup = SOUNDGROUP_Default;
	if (bLoop)
	{
		Wave->Duration = INDEFINITELY_LOOPING_DURATION;
		Wave->bLooping = true;
		// Keep the queue fed from the audio thread; the PCM is immutable and shared.
		Wave->OnSoundWaveProceduralUnderflow = FOnSoundWaveProceduralUnderflow::CreateLambda(
			[Data](USoundWaveProcedural* W, int32 /*SamplesRequired*/) { W->QueueAudio(Data->GetData(), Data->Num()); });
		Wave->QueueAudio(Data->GetData(), Data->Num());
	}
	else
	{
		Wave->Duration = static_cast<float>(Data->Num() / 2) / static_cast<float>(tfps::audio::kSampleRate);
		Wave->bLooping = false;
	}
	Wave->QueueAudio(Data->GetData(), Data->Num());
	return Wave;
}

USoundAttenuation* UTfpsAudioSubsystem::AttenuationFor(tfps::audio::Cue Cue)
{
	const uint8 Key = static_cast<uint8>(Cue);
	if (const TObjectPtr<USoundAttenuation>* Found = Attenuations.Find(Key))
	{
		return Found->Get();
	}
	USoundAttenuation* Att = NewObject<USoundAttenuation>(this);
	FSoundAttenuationSettings& S = Att->Attenuation;
	S.bAttenuate = true;
	S.bSpatialize = true;
	S.AttenuationShape = EAttenuationShape::Sphere;
	S.AttenuationShapeExtents = FVector(150.0f, 0.0f, 0.0f); // full volume within 1.5 m
	S.FalloffDistance = tfps::audio::Info(Cue).Range * 100.0f;
	S.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
	Attenuations.Add(Key, Att);
	return Att;
}

bool UTfpsAudioSubsystem::Admit(tfps::audio::Cue Cue)
{
	const int32 I = static_cast<int32>(Cue);
	const double Now = FPlatformTime::Seconds();
	if (Now - CueLastWindow[I] > kWindowSeconds)
	{
		CueLastWindow[I] = Now;
		CueCountInWindow[I] = 0;
	}
	return ++CueCountInWindow[I] <= kMaxPerWindow;
}

void UTfpsAudioSubsystem::Prune()
{
	const double Now = FPlatformTime::Seconds();
	for (int32 I = Playing.Num() - 1; I >= 0; --I)
	{
		if (PlayingUntil[I] < Now)
		{
			Playing.RemoveAt(I);
			PlayingUntil.RemoveAt(I);
		}
	}
}

void UTfpsAudioSubsystem::PlayAt(tfps::audio::Cue Cue, const FVector& LocationCm, float Volume)
{
	if (!tfps::audio::Info(Cue).bSpatial)
	{
		Play2D(Cue, Volume);
		return;
	}
	const float Level = LevelFor(Cue, Volume);
	if (Level <= 0.001f || !Admit(Cue) || GetGameInstance() == nullptr)
	{
		return;
	}
	Prune();
	const FPcm Data = Pcm(Cue, VariantCounter++);
	USoundWaveProcedural* Wave = MakeWave(Data, false);
	UGameplayStatics::PlaySoundAtLocation(GetGameInstance(), Wave, LocationCm, Level, 1.0f, 0.0f, AttenuationFor(Cue));
	Playing.Add(Wave);
	PlayingUntil.Add(FPlatformTime::Seconds() + Wave->Duration + 1.0);
}

void UTfpsAudioSubsystem::Play2D(tfps::audio::Cue Cue, float Volume)
{
	const float Level = LevelFor(Cue, Volume);
	if (Level <= 0.001f || !Admit(Cue) || GetGameInstance() == nullptr)
	{
		return;
	}
	Prune();
	const FPcm Data = Pcm(Cue, VariantCounter++);
	USoundWaveProcedural* Wave = MakeWave(Data, false);
	UGameplayStatics::PlaySound2D(GetGameInstance(), Wave, Level);
	Playing.Add(Wave);
	PlayingUntil.Add(FPlatformTime::Seconds() + Wave->Duration + 1.0);
}

UAudioComponent* UTfpsAudioSubsystem::StartLoop(tfps::audio::Cue Cue, const FVector& LocationCm, float Volume)
{
	if (GetGameInstance() == nullptr)
	{
		return nullptr;
	}
	USoundWaveProcedural* Wave = MakeWave(Pcm(Cue, 0), true);
	// The component owns the wave while it plays; it is destroyed when stopped (bAutoDestroy).
	return UGameplayStatics::SpawnSoundAtLocation(GetGameInstance(), Wave, LocationCm, FRotator::ZeroRotator, LevelFor(Cue, Volume), 1.0f, 0.0f,
		AttenuationFor(Cue));
}

bool FTfpsStepTracker::Update(const FVector& FeetMeters, bool bOnGround, float StrideMeters, bool& bOutLanded)
{
	bOutLanded = false;
	if (!bHasLast)
	{
		Last = FeetMeters;
		bHasLast = true;
		bWasOnGround = bOnGround;
		return false;
	}
	const FVector Delta = FeetMeters - Last;
	Last = FeetMeters;
	// A teleport (respawn, round reset) is not a step.
	if (Delta.Size() > 2.0)
	{
		Travelled = 0.0f;
		bWasOnGround = bOnGround;
		return false;
	}
	if (!bOnGround)
	{
		if (bWasOnGround)
		{
			AirZ = static_cast<float>(FeetMeters.Z);
		}
		AirZ = FMath::Max(AirZ, static_cast<float>(FeetMeters.Z));
		bWasOnGround = false;
		return false;
	}
	if (!bWasOnGround)
	{
		bWasOnGround = true;
		// Landing from a jump or drop of more than ~40 cm.
		bOutLanded = AirZ - static_cast<float>(FeetMeters.Z) > 0.4f;
		Travelled = 0.0f;
		return false;
	}
	Travelled += static_cast<float>(FVector(Delta.X, Delta.Y, 0.0).Size());
	if (Travelled >= StrideMeters)
	{
		Travelled -= StrideMeters;
		return true;
	}
	return false;
}

uint8 UTfpsAudioSubsystem::GroundMaterial(const tfps::MapDef* Map, const FVector& FeetMeters)
{
	if (Map == nullptr)
	{
		return 0;
	}
	const tfps::Vec3 Origin(static_cast<float>(FeetMeters.X), static_cast<float>(FeetMeters.Y), static_cast<float>(FeetMeters.Z) + 0.1f);
	float T = 0.0f;
	tfps::EMaterial Mat = tfps::EMaterial::Concrete;
	if (tfps::RaycastSolids(*Map, Origin, tfps::Vec3(0.0f, 0.0f, -1.0f), 0.6f, T, Mat))
	{
		return static_cast<uint8>(Mat);
	}
	return static_cast<uint8>(tfps::EMaterial::Concrete); // nothing under the feet (maps model the ground as a solid)
}

void UTfpsAudioSubsystem::Footstep(const tfps::MapDef* Map, const FVector& FeetMeters, float Volume, bool bLocal)
{
	const tfps::audio::Cue Cue = tfps::audio::StepCue(GroundMaterial(Map, FeetMeters));
	if (bLocal)
	{
		Play2D(Cue, Volume * 0.6f);
	}
	else
	{
		PlayAt(Cue, FeetMeters * TfpsMetersToUnreal, Volume);
	}
}
