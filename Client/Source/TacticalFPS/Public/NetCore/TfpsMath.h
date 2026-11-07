// Copyright TacticalFPS project. Engine-independent NetCore.
// Mirror of Shared/Simulation/rust/src/math.rs.
#pragma once

#include <cstddef>
#include <cstdint>

namespace tfps
{
	/** Meters, Z-up, X forward, Y right. */
	struct Vec3
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;

		Vec3() = default;
		Vec3(float InX, float InY, float InZ) : X(InX), Y(InY), Z(InZ) {}

		float Get(int Axis) const { return Axis == 0 ? X : (Axis == 1 ? Y : Z); }
		void Set(int Axis, float V)
		{
			if (Axis == 0) { X = V; }
			else if (Axis == 1) { Y = V; }
			else { Z = V; }
		}
		bool operator==(const Vec3& O) const { return X == O.X && Y == O.Y && Z == O.Z; }
		bool operator!=(const Vec3& O) const { return !(*this == O); }
	};

	float Distance(const Vec3& A, const Vec3& B);

	/** Deterministic sin/cos of a 16-bit quantized yaw (0..65535 = 0..360 deg). */
	void YawSinCos(uint16_t YawQ, float& OutSin, float& OutCos);

	uint16_t QuantizeYawDeg(float YawDeg);
	float DequantizeYawDeg(uint16_t YawQ);
	int16_t QuantizePitchDeg(float PitchDeg);
	float DequantizePitchDeg(int16_t PitchQ);

	/** 64-bit FNV-1a. */
	struct Fnv1a64
	{
		uint64_t State = 0xcbf29ce484222325ull;
		void Write(const uint8_t* Data, size_t Len);
		void WriteU32(uint32_t V);
		void WriteF32(float V);
		uint64_t Finish() const { return State; }
	};

	uint32_t FloatToBits(float V);
	float BitsToFloat(uint32_t Bits);
}
