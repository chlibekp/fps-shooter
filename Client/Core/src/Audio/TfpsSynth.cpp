// Copyright TacticalFPS project.
#include "Audio/TfpsSynth.h"

#include <algorithm>
#include <cmath>

namespace tfps
{
namespace audio
{
namespace
{
constexpr float kPi = 3.14159265358979f;
constexpr float kSr = static_cast<float>(kSampleRate);

using Buf = std::vector<float>;

// ---- primitives -------------------------------------------------------------

struct Rng
{
	uint32_t S;
	explicit Rng(uint32_t Seed)
		: S(Seed * 2654435761u + 0x9E3779B9u)
	{
		if (S == 0)
		{
			S = 1;
		}
		Next();
	}
	uint32_t Next()
	{
		S ^= S << 13;
		S ^= S >> 17;
		S ^= S << 5;
		return S;
	}
	/** [0, 1) */
	float Uni() { return static_cast<float>(Next() >> 8) * (1.0f / 16777216.0f); }
	/** [-1, 1) */
	float Bi() { return Uni() * 2.0f - 1.0f; }
	float Range(float A, float B) { return A + (B - A) * Uni(); }
	/** 1 +- Amount */
	float Vary(float Amount) { return 1.0f + Bi() * Amount; }
};

enum class FType
{
	Low,
	High,
	Band,
};

struct Biquad
{
	float B0 = 1.0f, B1 = 0.0f, B2 = 0.0f, A1 = 0.0f, A2 = 0.0f;
	float Z1 = 0.0f, Z2 = 0.0f;
	float Process(float X)
	{
		const float Y = B0 * X + Z1;
		Z1 = B1 * X - A1 * Y + Z2;
		Z2 = B2 * X - A2 * Y;
		return Y;
	}
};

/** RBJ cookbook filters (band-pass with 0 dB peak gain). */
Biquad Filter(FType Type, float Freq, float Q)
{
	const float F = std::min(std::max(Freq, 10.0f), kSr * 0.45f);
	const float W0 = 2.0f * kPi * F / kSr;
	const float C = std::cos(W0);
	const float Alpha = std::sin(W0) / (2.0f * Q);
	const float A0 = 1.0f + Alpha;
	Biquad B;
	switch (Type)
	{
	case FType::Low:
		B.B0 = (1.0f - C) * 0.5f;
		B.B1 = 1.0f - C;
		B.B2 = (1.0f - C) * 0.5f;
		break;
	case FType::High:
		B.B0 = (1.0f + C) * 0.5f;
		B.B1 = -(1.0f + C);
		B.B2 = (1.0f + C) * 0.5f;
		break;
	case FType::Band:
		B.B0 = Alpha;
		B.B1 = 0.0f;
		B.B2 = -Alpha;
		break;
	}
	B.A1 = -2.0f * C;
	B.A2 = 1.0f - Alpha;
	B.B0 /= A0;
	B.B1 /= A0;
	B.B2 /= A0;
	B.A1 /= A0;
	B.A2 /= A0;
	return B;
}

Biquad LP(float F, float Q = 0.707f) { return Filter(FType::Low, F, Q); }
Biquad HP(float F, float Q = 0.707f) { return Filter(FType::High, F, Q); }
Biquad BP(float F, float Q = 1.0f) { return Filter(FType::Band, F, Q); }

size_t Samples(float Seconds)
{
	return static_cast<size_t>(std::max(0.0f, Seconds) * kSr);
}

Buf Make(float Seconds)
{
	return Buf(Samples(Seconds), 0.0f);
}

/** Envelope: linear attack, exponential decay (time constant Tau), 2 ms release at the end of Dur. */
float Env(float T, float Dur, float Attack, float Tau)
{
	float E = std::exp(-T / Tau);
	if (Attack > 0.0f && T < Attack)
	{
		E *= T / Attack;
	}
	const float Rel = 0.002f;
	if (Dur - T < Rel)
	{
		E *= std::max(0.0f, (Dur - T) / Rel);
	}
	return E;
}

/** Filtered noise burst. */
void Noise(Buf& B, Rng& R, float Start, float Dur, Biquad F, float Gain, float Attack, float Tau)
{
	const size_t S0 = Samples(Start);
	const size_t N = Samples(Dur);
	for (size_t I = 0; I < N && S0 + I < B.size(); ++I)
	{
		const float T = static_cast<float>(I) / kSr;
		B[S0 + I] += F.Process(R.Bi()) * Gain * Env(T, Dur, Attack, Tau);
	}
}

/**
 * Sine (plus optional odd harmonic for a softer square-ish tone) whose pitch
 * glides exponentially from F0 to F1 with time constant Glide.
 */
void Tone(Buf& B, float Start, float Dur, float F0, float F1, float Glide, float Gain, float Attack, float Tau, float Third = 0.0f)
{
	const size_t S0 = Samples(Start);
	const size_t N = Samples(Dur);
	float Phase = 0.0f;
	for (size_t I = 0; I < N && S0 + I < B.size(); ++I)
	{
		const float T = static_cast<float>(I) / kSr;
		const float F = Glide > 0.0f ? F1 + (F0 - F1) * std::exp(-T / Glide) : F0;
		Phase += 2.0f * kPi * F / kSr;
		if (Phase > 2.0f * kPi)
		{
			Phase -= 2.0f * kPi;
		}
		const float S = std::sin(Phase) + Third * std::sin(3.0f * Phase);
		B[S0 + I] += S * Gain * Env(T, Dur, Attack, Tau);
	}
}

/** Struck object: decaying sine modes (higher modes decay faster). */
void Modes(Buf& B, float Start, const float* Freqs, const float* Gains, int Count, float Tau, float Gain, float Dur)
{
	for (int M = 0; M < Count; ++M)
	{
		const float ModeTau = Tau * std::pow(1000.0f / Freqs[M], 0.3f);
		Tone(B, Start, Dur, Freqs[M], Freqs[M], 0.0f, Gain * Gains[M], 0.0005f, ModeTau);
	}
}

/** Single short "ping" (tinkle / debris grain). */
void Ping(Buf& B, float Start, float Freq, float Tau, float Gain)
{
	Tone(B, Start, std::min(Tau * 6.0f, 0.3f), Freq, Freq, 0.0f, Gain, 0.0003f, Tau);
}

void SoftClip(Buf& B, float Drive)
{
	const float Norm = 1.0f / std::tanh(Drive);
	float Peak = 0.0f;
	for (float V : B)
	{
		Peak = std::max(Peak, std::fabs(V));
	}
	if (Peak <= 0.0f)
	{
		return;
	}
	for (float& V : B)
	{
		V = std::tanh(Drive * V / Peak) * Norm * Peak;
	}
}

/** Schroeder reverb: four combs and two all-passes. Decay is the -60 dB time. */
void Reverb(Buf& B, float Mix, float Decay)
{
	const float CombMs[4] = {29.7f, 37.1f, 41.1f, 43.7f};
	Buf Wet(B.size(), 0.0f);
	for (float Ms : CombMs)
	{
		const size_t D = Samples(Ms / 1000.0f);
		const float G = std::pow(10.0f, -3.0f * (Ms / 1000.0f) / Decay);
		Buf Line(D, 0.0f);
		size_t P = 0;
		float Damp = 0.0f;
		for (size_t I = 0; I < B.size(); ++I)
		{
			const float Out = Line[P];
			Damp = Out * 0.6f + Damp * 0.4f; // darker tail
			Line[P] = B[I] + Damp * G;
			P = (P + 1) % D;
			Wet[I] += Out * 0.25f;
		}
	}
	const float ApMs[2] = {5.0f, 1.7f};
	for (float Ms : ApMs)
	{
		const size_t D = Samples(Ms / 1000.0f);
		Buf Line(D, 0.0f);
		size_t P = 0;
		for (float& V : Wet)
		{
			const float Delayed = Line[P];
			const float In = V + Delayed * 0.7f;
			Line[P] = In;
			V = Delayed - In * 0.7f;
			P = (P + 1) % D;
		}
	}
	for (size_t I = 0; I < B.size(); ++I)
	{
		B[I] += Wet[I] * Mix;
	}
}

std::vector<int16_t> Finish(Buf& B, float Peak, bool bLoop)
{
	if (!bLoop)
	{
		const size_t Fade = std::min(B.size(), Samples(0.005f));
		for (size_t I = 0; I < Fade; ++I)
		{
			B[B.size() - 1 - I] *= static_cast<float>(I) / static_cast<float>(Fade);
		}
	}
	float Max = 0.0f;
	for (float V : B)
	{
		Max = std::max(Max, std::fabs(V));
	}
	const float Scale = Max > 0.0f ? Peak / Max : 0.0f;
	std::vector<int16_t> Out(B.size());
	for (size_t I = 0; I < B.size(); ++I)
	{
		const float V = std::min(1.0f, std::max(-1.0f, B[I] * Scale));
		Out[I] = static_cast<int16_t>(std::lround(V * 32767.0f));
	}
	return Out;
}

// ---- recipes ----------------------------------------------------------------

struct Gun
{
	float Body;    // main filtered-noise band (Hz)
	float Thump;   // low-frequency punch (Hz)
	float BodyTau; // body decay (s)
	float Tail;    // reverb decay (s)
	float Crack;   // supersonic crack level
	float Mech;    // action click level
};

Buf Gunshot(Rng& R, const Gun& G)
{
	Buf B = Make(G.Tail + 0.2f);
	const float K = R.Vary(0.04f);
	Noise(B, R, 0.0f, 0.006f, HP(2600.0f * K, 0.7f), G.Crack, 0.0f, 0.0015f);
	Noise(B, R, 0.0f, 0.3f, LP(G.Body * K, 0.8f), 1.0f, 0.0004f, G.BodyTau);
	Noise(B, R, 0.0f, 0.15f, BP(G.Body * 1.8f * K, 1.2f), 0.5f, 0.0004f, G.BodyTau * 0.5f);
	Tone(B, 0.0f, 0.3f, G.Thump * 2.0f * K, G.Thump * K, 0.02f, 0.9f, 0.0005f, 0.05f);
	if (G.Mech > 0.0f)
	{
		Noise(B, R, 0.035f * R.Vary(0.1f), 0.01f, BP(3500.0f * K, 3.0f), G.Mech, 0.0f, 0.002f);
	}
	SoftClip(B, 2.0f);
	Reverb(B, 0.35f, G.Tail);
	return B;
}

Buf Step(Rng& R, Cue C)
{
	Buf B = Make(0.25f);
	const float K = R.Vary(0.08f);
	const float Toe = R.Range(0.035f, 0.06f);
	for (int Part = 0; Part < 2; ++Part)
	{
		const float T = Part == 0 ? 0.0f : Toe;
		const float G = Part == 0 ? 1.0f : R.Range(0.45f, 0.7f);
		switch (C)
		{
		case Cue::StepWood:
			Tone(B, T, 0.08f, 185.0f * K, 150.0f * K, 0.02f, 0.35f * G, 0.001f, 0.012f);
			Noise(B, R, T, 0.08f, BP(620.0f * K, 1.2f), 0.9f * G, 0.0005f, 0.015f);
			Noise(B, R, T, 0.03f, HP(2500.0f * K), 0.15f * G, 0.0f, 0.004f);
			break;
		case Cue::StepMetal:
		{
			const float F[4] = {420.0f * K, 1130.0f * K, 2380.0f * K, 3700.0f * K};
			const float A[4] = {1.0f, 0.6f, 0.4f, 0.25f};
			Modes(B, T, F, A, 4, 0.035f, 0.5f * G, 0.15f);
			Noise(B, R, T, 0.02f, BP(2000.0f * K, 1.0f), 0.6f * G, 0.0f, 0.005f);
			break;
		}
		case Cue::StepDirt:
			Noise(B, R, T, 0.12f, LP(520.0f * K), 1.0f * G, 0.002f, 0.03f);
			for (int I = 0; I < 6; ++I)
			{
				Noise(B, R, T + R.Range(0.0f, 0.05f), 0.006f, BP(R.Range(2500.0f, 5000.0f), 2.0f), 0.15f * G, 0.0f, 0.0015f);
			}
			break;
		default: // concrete
			Noise(B, R, T, 0.08f, BP(900.0f * K, 0.9f), 1.0f * G, 0.0005f, 0.012f);
			Noise(B, R, T, 0.03f, HP(3000.0f * K), 0.25f * G, 0.0f, 0.006f);
			break;
		}
	}
	return B;
}

Buf Impact(Rng& R, Cue C)
{
	const float K = R.Vary(0.06f);
	switch (C)
	{
	case Cue::ImpactWood:
	{
		Buf B = Make(0.25f);
		Tone(B, 0.0f, 0.2f, 340.0f * K, 300.0f * K, 0.02f, 0.7f, 0.0005f, 0.025f);
		Noise(B, R, 0.0f, 0.1f, BP(900.0f * K, 1.2f), 0.8f, 0.0f, 0.012f);
		return B;
	}
	case Cue::ImpactMetal:
	{
		Buf B = Make(0.6f);
		const float F[4] = {1700.0f * K, 2930.0f * K * R.Vary(0.03f), 4150.0f * K * R.Vary(0.03f), 5600.0f * K};
		const float A[4] = {1.0f, 0.7f, 0.5f, 0.3f};
		Modes(B, 0.0f, F, A, 4, 0.06f, 0.5f, 0.55f);
		Noise(B, R, 0.0f, 0.04f, BP(3500.0f * K, 1.0f), 0.3f, 0.0f, 0.01f);
		Noise(B, R, 0.0f, 0.01f, HP(3000.0f), 0.8f, 0.0f, 0.002f);
		return B;
	}
	case Cue::ImpactGlass:
	{
		Buf B = Make(0.4f);
		Noise(B, R, 0.0f, 0.02f, HP(4000.0f * K), 0.8f, 0.0f, 0.003f);
		const float F[3] = {3100.0f * K, 4700.0f * K, 6900.0f * K};
		const float A[3] = {1.0f, 0.7f, 0.5f};
		Modes(B, 0.0f, F, A, 3, 0.025f, 0.4f, 0.2f);
		for (int I = 0; I < 6; ++I)
		{
			Ping(B, R.Range(0.01f, 0.2f), R.Range(3000.0f, 8000.0f), R.Range(0.004f, 0.015f), R.Range(0.05f, 0.2f));
		}
		return B;
	}
	case Cue::ImpactFlesh:
	{
		Buf B = Make(0.2f);
		Noise(B, R, 0.0f, 0.12f, LP(700.0f * K), 1.0f, 0.001f, 0.02f);
		Tone(B, 0.0f, 0.15f, 130.0f * K, 90.0f * K, 0.03f, 0.6f, 0.001f, 0.03f);
		return B;
	}
	default: // concrete
	{
		Buf B = Make(0.3f);
		Noise(B, R, 0.0f, 0.02f, HP(1500.0f * K), 1.0f, 0.0f, 0.004f);
		Noise(B, R, 0.0f, 0.06f, BP(1100.0f * K, 0.8f), 0.4f, 0.0f, 0.012f);
		for (int I = 0; I < 12; ++I)
		{
			Noise(B, R, R.Range(0.03f, 0.2f), 0.005f, BP(R.Range(2000.0f, 6000.0f), 3.0f), R.Range(0.05f, 0.25f), 0.0f, 0.001f);
		}
		return B;
	}
	}
}

Buf Synthesize(Cue C, Rng& R)
{
	switch (C)
	{
	case Cue::RifleShot: return Gunshot(R, {1800.0f, 85.0f, 0.045f, 0.9f, 0.9f, 0.15f});
	case Cue::SmgShot: return Gunshot(R, {2600.0f, 110.0f, 0.03f, 0.6f, 0.7f, 0.12f});
	case Cue::MarksmanShot: return Gunshot(R, {1300.0f, 60.0f, 0.065f, 1.4f, 1.1f, 0.25f});
	case Cue::PistolShot: return Gunshot(R, {2900.0f, 120.0f, 0.028f, 0.6f, 0.6f, 0.1f});
	case Cue::ShotgunShot: return Gunshot(R, {1000.0f, 55.0f, 0.08f, 1.3f, 0.8f, 0.0f});
	case Cue::DryFire:
	{
		Buf B = Make(0.08f);
		Noise(B, R, 0.0f, 0.004f, BP(4200.0f, 4.0f), 1.0f, 0.0f, 0.0008f);
		Noise(B, R, 0.011f, 0.004f, BP(2800.0f, 4.0f), 0.6f, 0.0f, 0.0008f);
		return B;
	}
	case Cue::MagOut:
	{
		Buf B = Make(0.2f);
		const float K = R.Vary(0.05f);
		Noise(B, R, 0.0f, 0.06f, BP(2500.0f * K, 2.0f), 0.3f, 0.02f, 1.0f);
		Noise(B, R, 0.06f, 0.01f, BP(3800.0f * K, 3.0f), 1.0f, 0.0f, 0.0015f);
		return B;
	}
	case Cue::MagIn:
	{
		Buf B = Make(0.2f);
		const float K = R.Vary(0.05f);
		Noise(B, R, 0.0f, 0.01f, BP(3000.0f * K, 3.0f), 0.8f, 0.0f, 0.002f);
		Tone(B, 0.005f, 0.08f, 220.0f * K, 180.0f * K, 0.02f, 0.6f, 0.001f, 0.02f);
		Noise(B, R, 0.05f, 0.01f, BP(4500.0f * K, 3.0f), 1.0f, 0.0f, 0.0015f);
		return B;
	}
	case Cue::BoltRack:
	{
		Buf B = Make(0.25f);
		Noise(B, R, 0.0f, 0.01f, BP(3000.0f, 3.0f), 0.8f, 0.0f, 0.0015f);
		Noise(B, R, 0.01f, 0.07f, BP(1800.0f, 2.0f), 0.3f, 0.02f, 1.0f);
		Noise(B, R, 0.09f, 0.01f, BP(3200.0f, 3.0f), 1.0f, 0.0f, 0.0015f);
		Tone(B, 0.095f, 0.08f, 160.0f, 140.0f, 0.02f, 0.5f, 0.001f, 0.02f);
		return B;
	}
	case Cue::StepConcrete:
	case Cue::StepWood:
	case Cue::StepMetal:
	case Cue::StepDirt:
		return Step(R, C);
	case Cue::Land:
	{
		Buf B = Make(0.4f);
		const float K = R.Vary(0.06f);
		Tone(B, 0.0f, 0.3f, 90.0f * K, 60.0f * K, 0.04f, 1.0f, 0.002f, 0.06f);
		Noise(B, R, 0.0f, 0.25f, LP(400.0f * K), 0.8f, 0.002f, 0.05f);
		Noise(B, R, 0.005f, 0.04f, BP(1500.0f * K, 1.0f), 0.3f, 0.0f, 0.01f);
		return B;
	}
	case Cue::ImpactConcrete:
	case Cue::ImpactWood:
	case Cue::ImpactMetal:
	case Cue::ImpactGlass:
	case Cue::ImpactFlesh:
		return Impact(R, C);
	case Cue::GlassBreak:
	{
		Buf B = Make(1.2f);
		Noise(B, R, 0.0f, 0.05f, HP(2500.0f), 1.0f, 0.0f, 0.01f);
		Noise(B, R, 0.0f, 0.4f, BP(3500.0f, 0.7f), 0.5f, 0.0f, 0.08f);
		for (int I = 0; I < 70; ++I)
		{
			const float U = R.Uni();
			const float T = 0.02f + 0.9f * U * U; // dense at first
			Ping(B, T, R.Range(2500.0f, 9000.0f), R.Range(0.004f, 0.018f), 0.3f * (1.0f - T) * R.Range(0.3f, 1.0f));
		}
		Reverb(B, 0.15f, 0.5f);
		return B;
	}
	case Cue::WoodBreak:
	{
		Buf B = Make(0.8f);
		Noise(B, R, 0.0f, 0.05f, BP(1500.0f, 1.0f), 1.0f, 0.0f, 0.01f);
		for (int I = 0; I < 30; ++I)
		{
			Noise(B, R, R.Range(0.0f, 0.4f), 0.03f, BP(R.Range(800.0f, 2500.0f), 2.0f), 0.4f * R.Uni(), 0.0f, 0.006f);
		}
		Tone(B, 0.0f, 0.4f, 110.0f, 70.0f, 0.05f, 0.7f, 0.002f, 0.08f);
		Reverb(B, 0.2f, 0.6f);
		return B;
	}
	case Cue::WallBreak:
	{
		Buf B = Make(1.3f);
		Tone(B, 0.0f, 0.6f, 70.0f, 45.0f, 0.08f, 1.0f, 0.003f, 0.12f);
		Noise(B, R, 0.0f, 0.8f, LP(800.0f), 0.5f, 0.005f, 0.25f);
		for (int I = 0; I < 80; ++I)
		{
			const float T = 0.9f * R.Uni();
			Noise(B, R, T, 0.04f, BP(R.Range(300.0f, 2500.0f), 1.5f), 0.4f * (1.0f - T) * R.Uni(), 0.0f, 0.01f);
		}
		Reverb(B, 0.25f, 0.8f);
		return B;
	}
	case Cue::Explosion:
	{
		Buf B = Make(2.6f);
		const float K = R.Vary(0.05f);
		Noise(B, R, 0.0f, 0.03f, HP(1000.0f), 1.0f, 0.0f, 0.006f);
		Tone(B, 0.0f, 0.6f, 90.0f * K, 40.0f * K, 0.08f, 0.8f, 0.002f, 0.12f);
		Noise(B, R, 0.0f, 1.5f, LP(900.0f * K), 1.4f, 0.002f, 0.35f);
		Noise(B, R, 0.0f, 2.0f, LP(160.0f * K, 1.0f), 3.0f, 0.01f, 0.7f);
		for (int I = 0; I < 40; ++I)
		{
			const float T = R.Range(0.2f, 1.2f);
			Noise(B, R, T, 0.03f, BP(R.Range(800.0f, 4000.0f), 2.0f), 0.15f * R.Uni(), 0.0f, 0.004f);
		}
		SoftClip(B, 3.0f);
		Reverb(B, 0.4f, 2.0f);
		return B;
	}
	case Cue::Flashbang:
	{
		Buf B = Make(1.4f);
		Noise(B, R, 0.0f, 0.1f, HP(800.0f), 1.2f, 0.0f, 0.02f);
		Noise(B, R, 0.0f, 0.4f, BP(2500.0f, 0.6f), 0.8f, 0.0f, 0.07f);
		Tone(B, 0.0f, 0.5f, 140.0f, 60.0f, 0.05f, 0.8f, 0.001f, 0.1f);
		SoftClip(B, 4.0f);
		Reverb(B, 0.45f, 1.2f);
		return B;
	}
	case Cue::Tinnitus:
	{
		Buf B = Make(3.0f);
		Tone(B, 0.0f, 3.0f, 6400.0f, 6400.0f, 0.0f, 0.6f, 0.05f, 1.2f);
		Tone(B, 0.0f, 3.0f, 6420.0f, 6420.0f, 0.0f, 0.4f, 0.05f, 1.2f);
		return B;
	}
	case Cue::SmokeHiss:
	{
		Buf B = Make(2.5f);
		Biquad F = HP(1800.0f);
		for (size_t I = 0; I < B.size(); ++I)
		{
			const float T = static_cast<float>(I) / kSr;
			const float Wobble = 1.0f + 0.3f * std::sin(2.0f * kPi * 3.0f * T) + 0.15f * std::sin(2.0f * kPi * 7.3f * T);
			B[I] = F.Process(R.Bi()) * Wobble * Env(T, 2.5f, 0.1f, 1.2f);
		}
		return B;
	}
	case Cue::GrenadeBounce:
	{
		Buf B = Make(0.2f);
		const float K = R.Vary(0.08f);
		const float F[3] = {2200.0f * K, 3800.0f * K, 5300.0f * K};
		const float A[3] = {1.0f, 0.6f, 0.4f};
		Modes(B, 0.0f, F, A, 3, 0.04f, 0.6f, 0.18f);
		Noise(B, R, 0.0f, 0.06f, LP(300.0f), 0.5f, 0.0f, 0.015f);
		return B;
	}
	case Cue::DroneHum:
	{
		// Exactly periodic over one second (all frequencies are whole Hz), so it loops seamlessly.
		Buf B = Make(1.0f);
		for (size_t I = 0; I < B.size(); ++I)
		{
			const float T = static_cast<float>(I) / kSr;
			float S = 0.0f;
			for (int H = 1; H <= 12; ++H)
			{
				S += std::sin(2.0f * kPi * 150.0f * static_cast<float>(H) * T) / static_cast<float>(H);
			}
			S *= 0.5f;
			S += 0.35f * std::sin(2.0f * kPi * 1200.0f * T) * (1.0f + 0.3f * std::sin(2.0f * kPi * 6.0f * T));
			S += 0.4f * std::sin(2.0f * kPi * 300.0f * T) * (1.0f + 0.2f * std::sin(2.0f * kPi * 30.0f * T));
			B[I] = S;
		}
		return B;
	}
	case Cue::DeviceBeep:
	{
		Buf B = Make(0.12f);
		Tone(B, 0.0f, 0.09f, 1850.0f, 1850.0f, 0.0f, 1.0f, 0.003f, 10.0f);
		return B;
	}
	case Cue::Reinforce:
	{
		Buf B = Make(0.8f);
		const float F[4] = {180.0f, 520.0f, 1310.0f, 2200.0f};
		const float A[4] = {1.0f, 0.7f, 0.45f, 0.3f};
		Modes(B, 0.0f, F, A, 4, 0.25f, 0.8f, 0.7f);
		Noise(B, R, 0.0f, 0.1f, LP(1500.0f), 0.7f, 0.0f, 0.03f);
		const float F2[4] = {189.0f, 546.0f, 1375.0f, 2310.0f};
		Modes(B, 0.12f, F2, A, 4, 0.2f, 0.5f, 0.6f);
		return B;
	}
	case Cue::BarricadePlace:
	{
		Buf B = Make(0.4f);
		Tone(B, 0.0f, 0.25f, 140.0f, 100.0f, 0.04f, 0.8f, 0.002f, 0.05f);
		Noise(B, R, 0.0f, 0.12f, BP(700.0f), 0.8f, 0.001f, 0.03f);
		Noise(B, R, 0.02f, 0.3f, HP(2000.0f), 0.2f, 0.02f, 0.1f);
		return B;
	}
	case Cue::HitMarker:
	{
		Buf B = Make(0.06f);
		Tone(B, 0.0f, 0.06f, 3300.0f, 3300.0f, 0.0f, 1.0f, 0.0005f, 0.015f);
		Noise(B, R, 0.0f, 0.01f, HP(5000.0f), 0.3f, 0.0f, 0.002f);
		return B;
	}
	case Cue::KillConfirm:
	{
		Buf B = Make(0.25f);
		Tone(B, 0.0f, 0.1f, 1400.0f, 1400.0f, 0.0f, 1.0f, 0.002f, 0.05f);
		Tone(B, 0.06f, 0.18f, 2100.0f, 2100.0f, 0.0f, 1.0f, 0.002f, 0.08f);
		return B;
	}
	case Cue::RoundStart:
	{
		Buf B = Make(1.1f);
		Tone(B, 0.0f, 0.15f, 660.0f, 660.0f, 0.0f, 1.0f, 0.004f, 0.2f, 0.11f);
		Tone(B, 0.25f, 0.15f, 660.0f, 660.0f, 0.0f, 1.0f, 0.004f, 0.2f, 0.11f);
		Tone(B, 0.5f, 0.55f, 990.0f, 990.0f, 0.0f, 1.0f, 0.004f, 0.3f, 0.11f);
		return B;
	}
	case Cue::RoundWin:
	{
		Buf B = Make(1.4f);
		const float Notes[4] = {523.25f, 659.25f, 783.99f, 1046.5f};
		for (int I = 0; I < 4; ++I)
		{
			Tone(B, 0.12f * static_cast<float>(I), 1.0f, Notes[I], Notes[I], 0.0f, 0.8f, 0.004f, 0.5f, 0.11f);
		}
		return B;
	}
	case Cue::RoundLose:
	{
		Buf B = Make(1.4f);
		const float Notes[3] = {440.0f, 349.23f, 293.66f};
		for (int I = 0; I < 3; ++I)
		{
			Tone(B, 0.18f * static_cast<float>(I), 1.0f, Notes[I], Notes[I], 0.0f, 0.8f, 0.004f, 0.6f, 0.11f);
		}
		return B;
	}
	case Cue::UiMove:
	{
		Buf B = Make(0.04f);
		Tone(B, 0.0f, 0.04f, 2400.0f, 2400.0f, 0.0f, 1.0f, 0.0005f, 0.008f);
		return B;
	}
	case Cue::UiAccept:
	{
		Buf B = Make(0.12f);
		Tone(B, 0.0f, 0.12f, 1200.0f, 1800.0f, 0.02f, 1.0f, 0.001f, 0.05f);
		return B;
	}
	case Cue::Count:
		break;
	}
	return Make(0.01f);
}

const CueInfo kInfo[kCueCount] = {
	// Name, loop, spatial, range m, variants, peak
	{"rifle_shot", false, true, 300.0f, 4, 0.95f},
	{"smg_shot", false, true, 250.0f, 4, 0.9f},
	{"marksman_shot", false, true, 400.0f, 3, 1.0f},
	{"pistol_shot", false, true, 200.0f, 4, 0.85f},
	{"shotgun_shot", false, true, 300.0f, 3, 1.0f},
	{"dry_fire", false, true, 15.0f, 1, 0.5f},
	{"mag_out", false, true, 20.0f, 2, 0.5f},
	{"mag_in", false, true, 20.0f, 2, 0.55f},
	{"bolt_rack", false, true, 20.0f, 1, 0.55f},
	{"step_concrete", false, true, 25.0f, 6, 0.45f},
	{"step_wood", false, true, 25.0f, 6, 0.45f},
	{"step_metal", false, true, 25.0f, 6, 0.5f},
	{"step_dirt", false, true, 25.0f, 6, 0.4f},
	{"land", false, true, 30.0f, 2, 0.6f},
	{"impact_concrete", false, true, 40.0f, 4, 0.6f},
	{"impact_wood", false, true, 40.0f, 4, 0.6f},
	{"impact_metal", false, true, 50.0f, 4, 0.55f},
	{"impact_glass", false, true, 40.0f, 4, 0.55f},
	{"impact_flesh", false, true, 30.0f, 4, 0.6f},
	{"glass_break", false, true, 60.0f, 3, 0.8f},
	{"wood_break", false, true, 60.0f, 3, 0.8f},
	{"wall_break", false, true, 80.0f, 3, 0.85f},
	{"explosion", false, true, 400.0f, 2, 1.0f},
	{"flashbang", false, true, 300.0f, 1, 1.0f},
	{"tinnitus", false, false, 0.0f, 1, 0.35f},
	{"smoke_hiss", false, true, 30.0f, 1, 0.5f},
	{"grenade_bounce", false, true, 30.0f, 3, 0.6f},
	{"drone_hum", true, true, 20.0f, 1, 0.5f},
	{"device_beep", false, true, 30.0f, 1, 0.5f},
	{"reinforce", false, true, 40.0f, 1, 0.75f},
	{"barricade_place", false, true, 30.0f, 1, 0.65f},
	{"hit_marker", false, false, 0.0f, 1, 0.5f},
	{"kill_confirm", false, false, 0.0f, 1, 0.5f},
	{"round_start", false, false, 0.0f, 1, 0.55f},
	{"round_win", false, false, 0.0f, 1, 0.55f},
	{"round_lose", false, false, 0.0f, 1, 0.55f},
	{"ui_move", false, false, 0.0f, 1, 0.25f},
	{"ui_accept", false, false, 0.0f, 1, 0.35f},
};

void PutU32(std::vector<uint8_t>& O, uint32_t V)
{
	for (int I = 0; I < 4; ++I)
	{
		O.push_back(static_cast<uint8_t>((V >> (8 * I)) & 0xFF));
	}
}

void PutU16(std::vector<uint8_t>& O, uint16_t V)
{
	O.push_back(static_cast<uint8_t>(V & 0xFF));
	O.push_back(static_cast<uint8_t>(V >> 8));
}

void PutTag(std::vector<uint8_t>& O, const char* Tag)
{
	for (int I = 0; I < 4; ++I)
	{
		O.push_back(static_cast<uint8_t>(Tag[I]));
	}
}
} // namespace

const CueInfo& Info(Cue C)
{
	const int I = static_cast<int>(C);
	return kInfo[I >= 0 && I < kCueCount ? I : 0];
}

std::vector<int16_t> Render(Cue C, uint32_t Variant)
{
	const CueInfo& I = Info(C);
	const uint32_t V = Variant % static_cast<uint32_t>(std::max(1, I.Variants));
	Rng R(static_cast<uint32_t>(C) * 7919u + V * 104729u + 1u);
	Buf B = Synthesize(C, R);
	return Finish(B, I.Peak, I.bLoop);
}

std::vector<uint8_t> EncodeWav(const std::vector<int16_t>& Pcm, int SampleRate)
{
	const uint32_t DataBytes = static_cast<uint32_t>(Pcm.size() * 2);
	std::vector<uint8_t> O;
	O.reserve(44 + DataBytes);
	PutTag(O, "RIFF");
	PutU32(O, 36 + DataBytes);
	PutTag(O, "WAVE");
	PutTag(O, "fmt ");
	PutU32(O, 16);
	PutU16(O, 1); // PCM
	PutU16(O, 1); // mono
	PutU32(O, static_cast<uint32_t>(SampleRate));
	PutU32(O, static_cast<uint32_t>(SampleRate) * 2);
	PutU16(O, 2);
	PutU16(O, 16);
	PutTag(O, "data");
	PutU32(O, DataBytes);
	for (int16_t S : Pcm)
	{
		PutU16(O, static_cast<uint16_t>(S));
	}
	return O;
}

Cue ShotCue(uint8_t WeaponId)
{
	switch (WeaponId)
	{
	case 2: return Cue::SmgShot;
	case 3: return Cue::MarksmanShot;
	case 4: return Cue::PistolShot;
	case 5: return Cue::ShotgunShot;
	default: return Cue::RifleShot;
	}
}

Cue StepCue(uint8_t Material)
{
	switch (Material)
	{
	case 2: return Cue::StepWood;  // wood
	case 4:                        // metal
	case 6: return Cue::StepMetal; // reinforced
	case 7: return Cue::StepDirt;  // dirt
	default: return Cue::StepConcrete;
	}
}

Cue ImpactCue(uint8_t Material)
{
	switch (Material)
	{
	case 0xFE: return Cue::ImpactFlesh;
	case 2: return Cue::ImpactWood;
	case 4:
	case 6: return Cue::ImpactMetal;
	case 5: return Cue::ImpactGlass;
	default: return Cue::ImpactConcrete; // concrete, brick, drywall, dirt
	}
}

} // namespace audio
} // namespace tfps
