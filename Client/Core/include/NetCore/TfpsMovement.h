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
		constexpr float ProneHeight = 0.6f;
		constexpr float StandEye = 1.65f;
		constexpr float CrouchEye = 1.05f;
		constexpr float ProneEye = 0.35f;
		constexpr float RunSpeed = 3.4f;
		constexpr float SprintSpeed = 5.2f;
		constexpr float WalkSpeed = 1.7f;
		constexpr float CrouchSpeed = 1.6f;
		constexpr float ProneSpeed = 0.8f;
		constexpr float AdsSpeedMult = 0.6f;
		constexpr float ProneTransition = 0.8f;
		constexpr float LeanOffset = 0.35f;
		constexpr float LeanDrop = 0.06f;
		constexpr float LeanRate = 5.0f;
		constexpr float HeadHalf = 0.14f;
		constexpr float GroundAccel = 28.0f;
		constexpr float GroundDecel = 34.0f;
		constexpr float AirAccel = 2.0f;
		constexpr float Gravity = 9.81f;
		constexpr float MaxFallSpeed = 40.0f;
		constexpr float JumpSpeed = 3.1f;
		// Vaulting (mirror of VAULT_* in movement.rs).
		constexpr float VaultMin = 0.45f;
		constexpr float VaultMax = 1.3f;
		constexpr float VaultReach = 0.45f;
		constexpr float VaultStep = 0.05f;
		constexpr float VaultPush = 2.8f;
		constexpr float VaultClear = 0.2f;
		constexpr float VaultHeight = 0.9f;
		constexpr float VaultEye = 0.75f;
		constexpr float JumpCooldown = 0.9f;
		constexpr float StepHeight = 0.35f;
	}

	struct MoveInput
	{
		uint16_t Buttons = 0;
		uint16_t YawQ = 0;
		int16_t PitchQ = 0;
		/** Requested weapon slot (0 = primary, 1 = secondary). */
		uint8_t Slot = 0;
		bool operator==(const MoveInput& O) const { return Buttons == O.Buttons && YawQ == O.YawQ && PitchQ == O.PitchQ && Slot == O.Slot; }
	};

	float StanceHeight(bool bCrouched, bool bProne);

	struct MoveState
	{
		Vec3 Pos;
		Vec3 Vel;
		bool bOnGround = false;
		bool bCrouched = false;
		bool bProne = false;
		float JumpTimer = 0.0f;
		/** Remaining prone transition time (movement and firing locked). */
		float StanceTimer = 0.0f;
		/** -1 (full left) .. 1 (full right). */
		float Lean = 0.0f;
		/** 0 (hip) .. 1 (fully aimed down sights). */
		float Aim = 0.0f;
		/** Armour class: 0 medium (default), 1 light, 2 heavy (speed factor, see ArmorSpeed). */
		uint8_t Armor = 0;
		/** Vaulting: tucked (VaultHeight) until landing where a crouched body fits. */
		bool bVaulting = false;

		float Height() const { return bVaulting ? MoveTuning::VaultHeight : StanceHeight(bCrouched, bProne); }
		float EyeHeight() const
		{
			return bVaulting ? MoveTuning::VaultEye : (bProne ? MoveTuning::ProneEye : (bCrouched ? MoveTuning::CrouchEye : MoveTuning::StandEye));
		}
		/** Eye position including lean, for a view yaw. */
		Vec3 EyePosition(uint16_t YawQ) const;
		bool operator==(const MoveState& O) const
		{
			return Pos == O.Pos && Vel == O.Vel && bOnGround == O.bOnGround && bCrouched == O.bCrouched && bProne == O.bProne
				&& JumpTimer == O.JumpTimer && StanceTimer == O.StanceTimer && Lean == O.Lean && Aim == O.Aim && Armor == O.Armor
				&& bVaulting == O.bVaulting;
		}
	};

	Aabb PlayerBox(const Vec3& Pos, float Height);

	/** Movement speed factor of an armour class (mirror of armor_speed in movement.rs). */
	inline float ArmorSpeed(uint8_t Armor) { return Armor == 1 ? 1.1f : (Armor == 2 ? 0.9f : 1.0f); }

	/** Moves V toward Target by at most Step. */
	float ApproachScalar(float V, float Target, float Step);

	/** True if the input asks to sprint and nothing forbids it. */
	bool SprintIntent(const MoveState& State, const MoveInput& Input);

	/** Advances one fixed movement step. Returns true if the player sprinted. */
	bool Simulate(MoveState& State, const MoveInput& Input, float Dt, const CollisionWorld& World);

	/** 1 / TickRate, computed identically to Rust's tick_dt. */
	float TickDt(uint16_t TickRate);
}
