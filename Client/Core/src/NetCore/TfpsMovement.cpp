// Copyright TacticalFPS project. Engine-independent NetCore.
// Line-by-line mirror of Shared/Simulation/rust/src/movement.rs (v2).
#include "NetCore/TfpsMovement.h"
#include "NetCore/TfpsFloatDeterminism.h"

#include <cmath>

namespace tfps
{
	namespace
	{
		constexpr float MoveSkin = 0.0001f;
		constexpr float MoveMaxSubstep = 0.1f;
		constexpr int MoveMaxSubsteps = 32;
		constexpr float MoveCandidateMargin = 0.5f;

		Aabb MoveHeadBox(const Vec3& Eye)
		{
			Aabb B;
			B.Min = Vec3(Eye.X - MoveTuning::HeadHalf, Eye.Y - MoveTuning::HeadHalf, Eye.Z - MoveTuning::HeadHalf);
			B.Max = Vec3(Eye.X + MoveTuning::HeadHalf, Eye.Y + MoveTuning::HeadHalf, Eye.Z + MoveTuning::HeadHalf);
			return B;
		}

		void MoveApproach(float& VX, float& VY, float TX, float TY, float MaxDelta)
		{
			const float DX = TX - VX;
			const float DY = TY - VY;
			const float D2 = DX * DX + DY * DY;
			if (D2 <= MaxDelta * MaxDelta)
			{
				VX = TX;
				VY = TY;
			}
			else
			{
				const float K = MaxDelta / std::sqrt(D2);
				VX += DX * K;
				VY += DY * K;
			}
		}

		Aabb VaultProbe(const MoveState& State, float S, float C, float Ahead, float Z0, float Z1)
		{
			const float X = State.Pos.X + C * Ahead;
			const float Y = State.Pos.Y + S * Ahead;
			const float H = MoveTuning::HalfWidth * 0.6f;
			Aabb B;
			B.Min = Vec3(X - H, Y - H, State.Pos.Z + Z0);
			B.Max = Vec3(X + H, Y + H, State.Pos.Z + Z1);
			return B;
		}

		/** Mirror of try_vault in movement.rs (same queries, same order). */
		bool MoveTryVault(MoveState& State, float S, float C, const CollisionWorld& World)
		{
			if (!World.OverlapsAny(VaultProbe(State, S, C, MoveTuning::VaultReach, 0.3f, 0.4f))) { return false; }
			float Top = -1.0f;
			const int Steps = static_cast<int>((MoveTuning::VaultMax - MoveTuning::VaultMin) / MoveTuning::VaultStep);
			for (int I = 0; I <= Steps; ++I)
			{
				const float H = MoveTuning::VaultMin + static_cast<float>(I) * MoveTuning::VaultStep;
				if (!World.OverlapsAny(VaultProbe(State, S, C, MoveTuning::VaultReach, H, H + MoveTuning::VaultStep)))
				{
					Top = H;
					break;
				}
			}
			if (Top < 0.0f) { return false; }
			const Vec3 Ahead(State.Pos.X + C * (MoveTuning::VaultReach + 0.3f), State.Pos.Y + S * (MoveTuning::VaultReach + 0.3f),
				State.Pos.Z + Top + MoveTuning::VaultStep);
			if (World.OverlapsAny(PlayerBox(Ahead, MoveTuning::VaultHeight))) { return false; }
			State.Vel.Z = std::sqrt(2.0f * MoveTuning::Gravity * (Top + MoveTuning::VaultClear));
			State.Vel.X = C * MoveTuning::VaultPush;
			State.Vel.Y = S * MoveTuning::VaultPush;
			State.bCrouched = true;
			State.bVaulting = true;
			State.bOnGround = false;
			State.JumpTimer = MoveTuning::JumpCooldown;
			return true;
		}

		void MoveUpdateStance(MoveState& State, uint16_t B, float Dt, const CollisionWorld& World)
		{
			if (State.StanceTimer > 0.0f)
			{
				State.StanceTimer -= Dt;
				if (State.StanceTimer < 0.0f) { State.StanceTimer = 0.0f; }
				return;
			}
			const bool bWantProne = (B & Buttons::Prone) != 0;
			const bool bWantCrouch = (B & Buttons::Crouch) != 0;
			if (bWantProne)
			{
				if (!State.bProne && State.bOnGround)
				{
					State.bProne = true;
					State.bCrouched = false;
					State.StanceTimer = MoveTuning::ProneTransition;
				}
			}
			else if (State.bProne)
			{
				const float H = StanceHeight(bWantCrouch, false);
				if (!World.OverlapsAny(PlayerBox(State.Pos, H)))
				{
					State.bProne = false;
					State.bCrouched = bWantCrouch;
					State.StanceTimer = MoveTuning::ProneTransition;
				}
			}
			else if (bWantCrouch)
			{
				State.bCrouched = true;
			}
			else if (State.bCrouched && State.bOnGround && !State.bVaulting && !World.OverlapsAny(PlayerBox(State.Pos, MoveTuning::StandHeight)))
			{
				State.bCrouched = false;
			}
		}

		void MoveUpdateLean(MoveState& State, const MoveInput& Input, bool bSprinting, float Dt, const CollisionWorld& World)
		{
			const uint16_t B = Input.Buttons;
			float Target = 0.0f;
			if (!bSprinting && !State.bProne && State.StanceTimer <= 0.0f)
			{
				if ((B & Buttons::LeanRight) != 0) { Target += 1.0f; }
				if ((B & Buttons::LeanLeft) != 0) { Target -= 1.0f; }
			}
			State.Lean = ApproachScalar(State.Lean, Target, MoveTuning::LeanRate * Dt);
			int I = 0;
			while (I < 10 && State.Lean != 0.0f && World.OverlapsAny(MoveHeadBox(State.EyePosition(Input.YawQ))))
			{
				State.Lean = ApproachScalar(State.Lean, 0.0f, 0.1f);
				++I;
			}
		}

		bool MoveResolveAxis(MoveState& State, int Axis, float Step, bool bAllowStep, const CollisionWorld& World)
		{
			bool bBlocked = false;
			const float H = State.Height();
			Aabb PB = PlayerBox(State.Pos, H);
			const Aabb Query = PB;
			World.ForEachCandidate(Query, MoveCandidateMargin, [&](const Aabb& WB, uint32_t) {
				if (!PB.Overlaps(WB))
				{
					return true;
				}
				if (Axis == 2)
				{
					if (Step > 0.0f)
					{
						State.Pos.Z = WB.Min.Z - H - MoveSkin;
					}
					else
					{
						State.Pos.Z = WB.Max.Z;
					}
				}
				else
				{
					if (bAllowStep)
					{
						const float Rise = WB.Max.Z - State.Pos.Z;
						if (Rise > 0.0f && Rise <= MoveTuning::StepHeight)
						{
							Vec3 Cand = State.Pos;
							Cand.Z = WB.Max.Z;
							if (!World.OverlapsAny(PlayerBox(Cand, H)))
							{
								State.Pos = Cand;
								PB = PlayerBox(State.Pos, H);
								return true;
							}
						}
					}
					if (Step > 0.0f)
					{
						State.Pos.Set(Axis, WB.Min.Get(Axis) - MoveTuning::HalfWidth - MoveSkin);
					}
					else
					{
						State.Pos.Set(Axis, WB.Max.Get(Axis) + MoveTuning::HalfWidth + MoveSkin);
					}
				}
				bBlocked = true;
				PB = PlayerBox(State.Pos, H);
				return true;
			});
			return bBlocked;
		}

		bool MoveAlongAxis(MoveState& State, int Axis, float Dist, bool bAllowStep, const CollisionWorld& World)
		{
			if (Dist == 0.0f)
			{
				return false;
			}
			// Rust: (dist.abs() / MAX_SUBSTEP) as i32 + 1, capped at MAX_SUBSTEPS.
			const float Q = std::fabs(Dist) / MoveMaxSubstep;
			const int N = (Q >= 31.0f) ? MoveMaxSubsteps : static_cast<int>(Q) + 1;
			const float Step = Dist / static_cast<float>(N);
			for (int I = 0; I < N; ++I)
			{
				State.Pos.Set(Axis, State.Pos.Get(Axis) + Step);
				if (MoveResolveAxis(State, Axis, Step, bAllowStep, World))
				{
					return true;
				}
			}
			return false;
		}

		void MoveGroundSnap(MoveState& State, const CollisionWorld& World)
		{
			const float H = State.Height();
			const Aabb PB = PlayerBox(State.Pos, H);
			Aabb Probe;
			Probe.Min = Vec3(PB.Min.X, PB.Min.Y, State.Pos.Z - MoveTuning::StepHeight);
			Probe.Max = Vec3(PB.Max.X, PB.Max.Y, State.Pos.Z);
			bool bFound = false;
			float Best = 0.0f;
			World.ForEachCandidate(Probe, 0.0f, [&](const Aabb& WB, uint32_t) {
				if (WB.Overlaps(Probe) && WB.Max.Z <= State.Pos.Z && (!bFound || WB.Max.Z > Best))
				{
					Best = WB.Max.Z;
					bFound = true;
				}
				return true;
			});
			if (!bFound)
			{
				return;
			}
			Vec3 Cand = State.Pos;
			Cand.Z = Best;
			if (!World.OverlapsAny(PlayerBox(Cand, H)))
			{
				State.Pos.Z = Best;
				State.Vel.Z = 0.0f;
				State.bOnGround = true;
			}
		}
	}

	float StanceHeight(bool bCrouched, bool bProne)
	{
		return bProne ? MoveTuning::ProneHeight : (bCrouched ? MoveTuning::CrouchHeight : MoveTuning::StandHeight);
	}

	Vec3 MoveState::EyePosition(uint16_t YawQ) const
	{
		float S = 0.0f;
		float C = 0.0f;
		YawSinCos(YawQ, S, C);
		const float Off = Lean * MoveTuning::LeanOffset;
		const float Drop = (Lean < 0.0f ? -Lean : Lean) * MoveTuning::LeanDrop;
		return Vec3(Pos.X - S * Off, Pos.Y + C * Off, Pos.Z + EyeHeight() - Drop);
	}

	Aabb PlayerBox(const Vec3& Pos, float Height)
	{
		Aabb B;
		B.Min = Vec3(Pos.X - MoveTuning::HalfWidth, Pos.Y - MoveTuning::HalfWidth, Pos.Z);
		B.Max = Vec3(Pos.X + MoveTuning::HalfWidth, Pos.Y + MoveTuning::HalfWidth, Pos.Z + Height);
		return B;
	}

	float ApproachScalar(float V, float Target, float Step)
	{
		if (V < Target)
		{
			const float N = V + Step;
			return N > Target ? Target : N;
		}
		const float N = V - Step;
		return N < Target ? Target : N;
	}

	bool SprintIntent(const MoveState& State, const MoveInput& Input)
	{
		const uint16_t B = Input.Buttons;
		return (B & Buttons::Sprint) != 0 && (B & Buttons::Walk) == 0 && (B & Buttons::Forward) != 0 && (B & Buttons::Back) == 0
			&& (B & Buttons::Fire) == 0 && !State.bCrouched && !State.bProne && State.Aim <= 0.0f;
	}

	float TickDt(uint16_t TickRate)
	{
		return 1.0f / static_cast<float>(TickRate);
	}

	bool Simulate(MoveState& State, const MoveInput& Input, float Dt, const CollisionWorld& World)
	{
		const uint16_t B = Input.Buttons;

		// 1. Stance.
		MoveUpdateStance(State, B, Dt, World);

		// 2. Wish direction.
		float S = 0.0f;
		float C = 0.0f;
		YawSinCos(Input.YawQ, S, C);
		float Fwd = 0.0f;
		float Side = 0.0f;
		if ((B & Buttons::Forward) != 0) { Fwd += 1.0f; }
		if ((B & Buttons::Back) != 0) { Fwd -= 1.0f; }
		if ((B & Buttons::Right) != 0) { Side += 1.0f; }
		if ((B & Buttons::Left) != 0) { Side -= 1.0f; }
		float WX = C * Fwd - S * Side;
		float WY = S * Fwd + C * Side;
		const float Len2 = WX * WX + WY * WY;
		const bool bHasInput = Len2 > 0.0f;
		if (bHasInput)
		{
			const float Inv = 1.0f / std::sqrt(Len2);
			WX *= Inv;
			WY *= Inv;
		}

		const bool bSprinting = SprintIntent(State, Input);
		float Speed;
		if (State.StanceTimer > 0.0f) { Speed = 0.0f; }
		else if (State.bProne) { Speed = MoveTuning::ProneSpeed; }
		else if (State.bCrouched) { Speed = MoveTuning::CrouchSpeed; }
		else if ((B & Buttons::Walk) != 0) { Speed = MoveTuning::WalkSpeed; }
		else if (bSprinting) { Speed = MoveTuning::SprintSpeed; }
		else { Speed = MoveTuning::RunSpeed; }
		Speed *= 1.0f - (1.0f - MoveTuning::AdsSpeedMult) * State.Aim;
		Speed *= ArmorSpeed(State.Armor);
		const float TX = WX * Speed;
		const float TY = WY * Speed;

		// 3. Horizontal acceleration and jumping.
		if (State.bOnGround)
		{
			const float Accel = bHasInput ? MoveTuning::GroundAccel : MoveTuning::GroundDecel;
			MoveApproach(State.Vel.X, State.Vel.Y, TX, TY, Accel * Dt);
			if ((B & Buttons::Jump) != 0 && (B & Buttons::Forward) != 0 && State.JumpTimer <= 0.0f && !State.bProne
				&& State.StanceTimer <= 0.0f && MoveTryVault(State, S, C, World))
			{
				// Vaulting (see MoveTryVault).
			}
			else if ((B & Buttons::Jump) != 0 && State.JumpTimer <= 0.0f && !State.bCrouched && !State.bProne && State.StanceTimer <= 0.0f)
			{
				State.Vel.Z = MoveTuning::JumpSpeed;
				State.bOnGround = false;
				State.JumpTimer = MoveTuning::JumpCooldown;
			}
		}
		else if (State.bVaulting)
		{
			// Carried over the obstacle (mirror of movement.rs).
			State.Vel.X = C * MoveTuning::VaultPush;
			State.Vel.Y = S * MoveTuning::VaultPush;
		}
		else if (bHasInput)
		{
			MoveApproach(State.Vel.X, State.Vel.Y, TX, TY, MoveTuning::AirAccel * Dt);
		}

		// 4. Gravity.
		State.Vel.Z -= MoveTuning::Gravity * Dt;
		if (State.Vel.Z < -MoveTuning::MaxFallSpeed)
		{
			State.Vel.Z = -MoveTuning::MaxFallSpeed;
		}

		// 5. Collide and slide.
		const bool bWasOnGround = State.bOnGround;
		if (MoveAlongAxis(State, 0, State.Vel.X * Dt, bWasOnGround, World)) { State.Vel.X = 0.0f; }
		if (MoveAlongAxis(State, 1, State.Vel.Y * Dt, bWasOnGround, World)) { State.Vel.Y = 0.0f; }
		State.bOnGround = false;
		const float DZ = State.Vel.Z * Dt;
		if (MoveAlongAxis(State, 2, DZ, false, World))
		{
			if (DZ < 0.0f)
			{
				State.bOnGround = true;
			}
			State.Vel.Z = 0.0f;
		}
		// A vault ends on landing, once a crouched body fits.
		if (State.bVaulting && State.bOnGround && !World.OverlapsAny(PlayerBox(State.Pos, MoveTuning::CrouchHeight)))
		{
			State.bVaulting = false;
		}

		// 6. Ground snap.
		if (bWasOnGround && !State.bOnGround && State.Vel.Z <= 0.0f)
		{
			MoveGroundSnap(State, World);
		}

		// 7. Lean.
		MoveUpdateLean(State, Input, bSprinting, Dt, World);

		// 8. Timers.
		if (State.JumpTimer > 0.0f)
		{
			State.JumpTimer -= Dt;
			if (State.JumpTimer < 0.0f)
			{
				State.JumpTimer = 0.0f;
			}
		}
		return bSprinting;
	}
}

#include "NetCore/TfpsFloatDeterminismEnd.h"
