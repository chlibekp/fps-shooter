// Procedural sound synthesis (Audio/TfpsSynth).
#include "TestFramework.h"
#include "Audio/TfpsSynth.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace tfps::audio;

namespace
{
	int Peak(const std::vector<int16_t>& P)
	{
		int M = 0;
		for (int16_t S : P)
		{
			M = std::max(M, std::abs(static_cast<int>(S)));
		}
		return M;
	}

	/** Fraction of signal energy below ~400 Hz (one-pole split). */
	double LowFraction(const std::vector<int16_t>& P)
	{
		const double A = std::exp(-2.0 * 3.14159265358979 * 400.0 / kSampleRate);
		double Low = 0.0, ELow = 0.0, ETotal = 0.0;
		for (int16_t S : P)
		{
			const double X = S / 32768.0;
			Low = (1.0 - A) * X + A * Low;
			ELow += Low * Low;
			ETotal += X * X;
		}
		return ETotal > 0.0 ? ELow / ETotal : 0.0;
	}
}

TFPS_TEST(AudioEveryCueRendersCleanly)
{
	for (int C = 0; C < kCueCount; ++C)
	{
		const Cue Id = static_cast<Cue>(C);
		const CueInfo& I = Info(Id);
		const std::vector<int16_t> P = Render(Id, 0);
		const double Seconds = static_cast<double>(P.size()) / kSampleRate;
		if (!(Seconds >= 0.03 && Seconds <= 3.0))
		{
			std::fprintf(stderr, "  %s: %.3f s\n", I.Name, Seconds);
		}
		TFPS_EXPECT(Seconds >= 0.03 && Seconds <= 3.0);
		// Normalised to the cue's peak level, not clipped.
		const int Want = static_cast<int>(std::lround(I.Peak * 32767.0f));
		TFPS_EXPECT(std::abs(Peak(P) - Want) <= 1);
		if (!I.bLoop)
		{
			// Ends in silence: no click when the sound stops.
			int Tail = 0;
			for (size_t S = P.size() - kSampleRate / 1000; S < P.size(); ++S)
			{
				Tail = std::max(Tail, std::abs(static_cast<int>(P[S])));
			}
			if (Tail > 330)
			{
				std::fprintf(stderr, "  %s: tail %d\n", I.Name, Tail);
			}
			TFPS_EXPECT(Tail <= 330);
		}
		// Deterministic.
		TFPS_EXPECT(Render(Id, 0) == P);
	}
}

TFPS_TEST(AudioVariantsDifferAndWrap)
{
	for (int C = 0; C < kCueCount; ++C)
	{
		const Cue Id = static_cast<Cue>(C);
		const int V = Info(Id).Variants;
		if (V > 1)
		{
			TFPS_EXPECT(Render(Id, 0) != Render(Id, 1));
		}
		TFPS_EXPECT(Render(Id, static_cast<uint32_t>(V)) == Render(Id, 0));
	}
}

TFPS_TEST(AudioLoopIsSeamless)
{
	const std::vector<int16_t> P = Render(Cue::DroneHum);
	TFPS_REQUIRE(Info(Cue::DroneHum).bLoop);
	// The jump from the last sample back to the first is no bigger than the largest step inside the loop.
	int MaxStep = 0;
	for (size_t I = 1; I < P.size(); ++I)
	{
		MaxStep = std::max(MaxStep, std::abs(P[I] - P[I - 1]));
	}
	TFPS_EXPECT(std::abs(P.front() - P.back()) <= MaxStep);
}

TFPS_TEST(AudioCuesHaveTheExpectedCharacter)
{
	// Explosions and landings are bass-heavy; glass and interface ticks are bright.
	const double Explosion = LowFraction(Render(Cue::Explosion));
	const double Land = LowFraction(Render(Cue::Land));
	const double Glass = LowFraction(Render(Cue::GlassBreak));
	const double Tick = LowFraction(Render(Cue::UiMove));
	std::printf("  low-band energy: explosion %.3f land %.3f glass %.3f tick %.3f marksman %.3f pistol %.3f\n", Explosion, Land, Glass, Tick,
		LowFraction(Render(Cue::MarksmanShot)), LowFraction(Render(Cue::PistolShot)));
	TFPS_EXPECT(Explosion > 0.5);
	TFPS_EXPECT(Land > 0.5);
	TFPS_EXPECT(Glass < 0.05);
	TFPS_EXPECT(Tick < 0.05);
	// The marksman rifle is heavier than the pistol.
	TFPS_EXPECT(LowFraction(Render(Cue::MarksmanShot)) > LowFraction(Render(Cue::PistolShot)));
	// Gunshots carry a reverb tail longer than a footstep.
	TFPS_EXPECT(Render(Cue::RifleShot).size() > 3 * Render(Cue::StepConcrete).size());
}

TFPS_TEST(AudioWavHeaderAndMappings)
{
	const std::vector<int16_t> P = {0, 1000, -1000, 32767};
	const std::vector<uint8_t> W = EncodeWav(P);
	TFPS_REQUIRE(W.size() == 44 + 8);
	TFPS_EXPECT(std::memcmp(W.data(), "RIFF", 4) == 0 && std::memcmp(W.data() + 8, "WAVEfmt ", 8) == 0);
	TFPS_EXPECT(W[4] == 44 && W[22] == 1 && W[34] == 16); // RIFF size 36 + 8, mono, 16-bit
	TFPS_EXPECT(W[24] == (kSampleRate & 0xFF) && W[25] == ((kSampleRate >> 8) & 0xFF));
	TFPS_EXPECT(std::memcmp(W.data() + 36, "data", 4) == 0 && W[40] == 8);
	TFPS_EXPECT(W[46] == 0xE8 && W[47] == 0x03); // 1000 little-endian
	TFPS_EXPECT(ShotCue(4) == Cue::PistolShot && ShotCue(99) == Cue::RifleShot);
	TFPS_EXPECT(StepCue(2) == Cue::StepWood && StepCue(6) == Cue::StepMetal && StepCue(0) == Cue::StepConcrete);
	TFPS_EXPECT(ImpactCue(0xFE) == Cue::ImpactFlesh && ImpactCue(5) == Cue::ImpactGlass);
}
