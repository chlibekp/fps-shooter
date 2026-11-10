// Copyright TacticalFPS project.
//
// Procedural sound synthesis: every game sound is generated from noise,
// oscillators, filters and envelopes (no recorded or third-party audio).
// Engine-independent and deterministic: Render(cue, variant) always returns
// the same samples, so the sounds can be unit tested
// (Client/NetCoreTests/src/AudioTests.cpp), written to WAV files for review
// (tfps_render_sounds) and played by the Unreal client (UTfpsAudioSubsystem)
// without importing any asset.
#pragma once

#include <cstdint>
#include <vector>

namespace tfps
{
namespace audio
{

constexpr int kSampleRate = 48000;

enum class Cue : uint8_t
{
	// Weapons
	RifleShot,
	SmgShot,
	MarksmanShot,
	PistolShot,
	ShotgunShot,
	DryFire,
	MagOut,
	MagIn,
	BoltRack,
	// Movement
	StepConcrete,
	StepWood,
	StepMetal,
	StepDirt,
	Land,
	// Bullet impacts
	ImpactConcrete,
	ImpactWood,
	ImpactMetal,
	ImpactGlass,
	ImpactFlesh,
	// Destruction and gadgets
	GlassBreak,
	WoodBreak,
	WallBreak,
	Explosion,
	Flashbang,
	Tinnitus,
	SmokeHiss,
	GrenadeBounce,
	DroneHum,
	DeviceBeep,
	Reinforce,
	BarricadePlace,
	DoorOpen,
	DoorClose,
	// Feedback and interface
	HitMarker,
	KillConfirm,
	RoundStart,
	RoundWin,
	RoundLose,
	UiMove,
	UiAccept,
	Count
};

constexpr int kCueCount = static_cast<int>(Cue::Count);

struct CueInfo
{
	const char* Name;
	/** Loops seamlessly (the last sample flows into the first). */
	bool bLoop;
	/** Played at a world position (false: interface / own-player sound). */
	bool bSpatial;
	/** Distance in metres at which it fades to silence. */
	float Range;
	/** Number of distinct variations (Render's `Variant` is taken modulo this). */
	int Variants;
	/** Peak level 0..1 after normalisation (relative loudness between cues). */
	float Peak;
};

const CueInfo& Info(Cue C);

/** Mono 16-bit PCM at kSampleRate. Deterministic for (C, Variant). */
std::vector<int16_t> Render(Cue C, uint32_t Variant = 0);

/** A RIFF/WAVE file (PCM, 16-bit, mono). */
std::vector<uint8_t> EncodeWav(const std::vector<int16_t>& Pcm, int SampleRate = kSampleRate);

/** Gunshot for a weapon id from weapons.json (unknown ids: rifle). */
Cue ShotCue(uint8_t WeaponId);
/** Footstep / bullet impact for a map material id (tfps::EMaterial values; 0xFE = player). */
Cue StepCue(uint8_t Material);
Cue ImpactCue(uint8_t Material);

} // namespace audio
} // namespace tfps
