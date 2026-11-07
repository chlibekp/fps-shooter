// Copyright TacticalFPS project. Engine-independent NetCore.
#include "NetCore/TfpsMath.h"
#include "NetCore/TfpsFloatDeterminism.h"

#include <cmath>
#include <cstring>

namespace tfps
{
	float Distance(const Vec3& A, const Vec3& B)
	{
		const float DX = A.X - B.X;
		const float DY = A.Y - B.Y;
		const float DZ = A.Z - B.Z;
		return std::sqrt(DX * DX + DY * DY + DZ * DZ);
	}

	void YawSinCos(uint16_t YawQ, float& OutSin, float& OutCos)
	{
		const uint32_t Quadrant = (static_cast<uint32_t>(YawQ) >> 14) & 3u;
		const float Rem = static_cast<float>(YawQ & 0x3FFFu);
		const float X = Rem * 9.5873799e-5f;
		const float X2 = X * X;
		const float S = X
			* (1.0f
				+ X2 * (-0.16666667f
					+ X2 * (0.008333334f
						+ X2 * (-0.0001984127f + X2 * (2.7557319e-6f + X2 * -2.5052108e-8f)))));
		const float C = 1.0f
			+ X2 * (-0.5f
				+ X2 * (0.041666668f
					+ X2 * (-0.0013888889f
						+ X2 * (2.4801587e-5f + X2 * (-2.7557319e-7f + X2 * 2.0876757e-9f)))));
		switch (Quadrant)
		{
		case 0: OutSin = S; OutCos = C; break;
		case 1: OutSin = C; OutCos = -S; break;
		case 2: OutSin = -S; OutCos = -C; break;
		default: OutSin = -C; OutCos = S; break;
		}
	}

	uint16_t QuantizeYawDeg(float YawDeg)
	{
		const float Turns = YawDeg / 360.0f;
		const float Frac = Turns - std::floor(Turns);
		const uint32_t Q = static_cast<uint32_t>(Frac * 65536.0f + 0.5f);
		return static_cast<uint16_t>(Q % 65536u);
	}

	float DequantizeYawDeg(uint16_t YawQ)
	{
		return static_cast<float>(YawQ) * (360.0f / 65536.0f);
	}

	int16_t QuantizePitchDeg(float PitchDeg)
	{
		float P = PitchDeg;
		if (P > 90.0f) { P = 90.0f; }
		if (P < -90.0f) { P = -90.0f; }
		return static_cast<int16_t>(std::lround(P * (32767.0f / 90.0f)));
	}

	float DequantizePitchDeg(int16_t PitchQ)
	{
		const int16_t Clamped = PitchQ < -32767 ? static_cast<int16_t>(-32767) : PitchQ;
		return static_cast<float>(Clamped) * (90.0f / 32767.0f);
	}

	void Fnv1a64::Write(const uint8_t* Data, size_t Len)
	{
		for (size_t I = 0; I < Len; ++I)
		{
			State ^= Data[I];
			State *= 0x00000100000001b3ull;
		}
	}

	void Fnv1a64::WriteU32(uint32_t V)
	{
		const uint8_t B[4] = {static_cast<uint8_t>(V), static_cast<uint8_t>(V >> 8), static_cast<uint8_t>(V >> 16), static_cast<uint8_t>(V >> 24)};
		Write(B, 4);
	}

	void Fnv1a64::WriteF32(float V)
	{
		WriteU32(FloatToBits(V));
	}

	uint32_t FloatToBits(float V)
	{
		uint32_t Bits;
		std::memcpy(&Bits, &V, sizeof(Bits));
		return Bits;
	}

	float BitsToFloat(uint32_t Bits)
	{
		float V;
		std::memcpy(&V, &Bits, sizeof(V));
		return V;
	}
}

#include "NetCore/TfpsFloatDeterminismEnd.h"
