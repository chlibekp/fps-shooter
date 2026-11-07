// Copyright TacticalFPS project. Engine-independent NetCore.
//
// Bit-exact C++ mirror of Shared/Simulation/rust/src/movement.rs. Any change
// must be made in both, and the conformance vectors regenerated
// (cargo run -p tfps-sim --bin gen_vectors). Verified by
// Client/NetCoreTests (movement conformance test).
#pragma once

#include "NetCore/TfpsMap.h"
#include "NetCore/TfpsMath.h"

#include <cstdint>

namespace tfps
{
	namespace Buttons
	{
		constexpr uint16_t Forward = 1u << 0;
		constexpr uint16_t Back = 1u << 1;
		constexpr uint16_t Left = 1u << 2;
		constexpr uint16_t Right = 1u << 3;
		constexpr uint16_t Jump = 1u << 4;
		constexpr uint16_t Crouch = 1u << 5;
		constexpr uint16_t Sprint = 1u << 6;
		constexpr uint16_t Walk = 1u << 7;
		constexpr uint16_t LeanLeft = 1u << 8;
		constexpr uint16_t LeanRight = 1u << 9;
		constexpr uint16_t Aim = 1u << 10;
		constexpr uint16_t Fire = 1u << 11;
		constexpr uint16_t Prone = 1u << 12;
		constexpr uint16_t Reload = 1u << 13;
		constexpr uint16_t Use = 1u << 14;
		constexpr uint16_t All = 0x7FFF;
	}

	namespace MoveTuning
	{
		constexpr float HalfWidth = 0.3f;
		constexpr float StandHeight = 1.8f;
		constexpr float CrouchHeight = 1.2f;
		constexpr float StandEye = 1.65f;
		constexpr float CrouchEye = 1.05f;
		constexpr float RunSpeed = 3.4f;
		constexpr float SprintSpeed = 5.2f;
		constexpr float WalkSpeed = 1.7f;
		constexpr float CrouchSpeed = 1.6f;
		constexpr float GroundAccel = 28.0f;
		constexpr float GroundDecel = 34.0f;
		constexpr float AirAccel = 2.0f;
		constexpr float Gravity = 9.81f;
		constexpr float MaxFallSpeed = 40.0f;
		constexpr float JumpSpeed = 3.1f;
		constexpr float JumpCooldown = 0.9f;
		constexpr float StepHeight = 0.35f;
	}

	struct MoveInput
	{
		uint16_t Buttons = 0;
		uint16_t YawQ = 0;
		int16_t PitchQ = 0;
		bool operator==(const MoveInput& O) const { return Buttons == O.Buttons && YawQ == O.YawQ && PitchQ == O.PitchQ; }
	};

	struct MoveState
	{
		Vec3 Pos;
		Vec3 Vel;
		bool bOnGround = false;
		bool bCrouched = false;
		float JumpTimer = 0.0f;

		float Height() const { return bCrouched ? MoveTuning::CrouchHeight : MoveTuning::StandHeight; }
		float EyeHeight() const { return bCrouched ? MoveTuning::CrouchEye : MoveTuning::StandEye; }
		bool operator==(const MoveState& O) const
		{
			return Pos == O.Pos && Vel == O.Vel && bOnGround == O.bOnGround && bCrouched == O.bCrouched && JumpTimer == O.JumpTimer;
		}
	};

	Aabb PlayerBox(const Vec3& Pos, bool bCrouched);

	/** Advances one fixed simulation step. */
	void Simulate(MoveState& State, const MoveInput& Input, float Dt, const CollisionWorld& World);

	/** 1 / TickRate, computed identically to Rust's tick_dt. */
	float TickDt(uint16_t TickRate);
}
