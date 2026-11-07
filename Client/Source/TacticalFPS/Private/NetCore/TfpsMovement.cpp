// Copyright TacticalFPS project. Engine-independent NetCore.
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

		bool MoveResolveAxis(MoveState& State, int Axis, float Step, bool bAllowStep, const CollisionWorld& World)
		{
			bool bBlocked = false;
			Aabb PB = PlayerBox(State.Pos, State.bCrouched);
			for (const Aabb& WB : World.Boxes)
			{
				if (!PB.Overlaps(WB))
				{
					continue;
				}
				if (Axis == 2)
				{
					if (Step > 0.0f)
					{
						State.Pos.Z = WB.Min.Z - State.Height() - MoveSkin;
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
							if (!World.OverlapsAny(PlayerBox(Cand, State.bCrouched)))
							{
								State.Pos = Cand;
								PB = PlayerBox(State.Pos, State.bCrouched);
								continue;
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
				PB = PlayerBox(State.Pos, State.bCrouched);
			}
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
			const Aabb PB = PlayerBox(State.Pos, State.bCrouched);
			Aabb Probe;
			Probe.Min = Vec3(PB.Min.X, PB.Min.Y, State.Pos.Z - MoveTuning::StepHeight);
			Probe.Max = Vec3(PB.Max.X, PB.Max.Y, State.Pos.Z);
			bool bFound = false;
			float Best = 0.0f;
			for (const Aabb& WB : World.Boxes)
			{
				if (WB.Overlaps(Probe) && WB.Max.Z <= State.Pos.Z && (!bFound || WB.Max.Z > Best))
				{
					Best = WB.Max.Z;
					bFound = true;
				}
			}
			if (!bFound)
			{
				return;
			}
			Vec3 Cand = State.Pos;
			Cand.Z = Best;
			if (!World.OverlapsAny(PlayerBox(Cand, State.bCrouched)))
			{
				State.Pos.Z = Best;
				State.Vel.Z = 0.0f;
				State.bOnGround = true;
			}
		}
	}

	Aabb PlayerBox(const Vec3& Pos, bool bCrouched)
	{
		const float H = bCrouched ? MoveTuning::CrouchHeight : MoveTuning::StandHeight;
		Aabb B;
		B.Min = Vec3(Pos.X - MoveTuning::HalfWidth, Pos.Y - MoveTuning::HalfWidth, Pos.Z);
		B.Max = Vec3(Pos.X + MoveTuning::HalfWidth, Pos.Y + MoveTuning::HalfWidth, Pos.Z + H);
		return B;
	}

	float TickDt(uint16_t TickRate)
	{
		return 1.0f / static_cast<float>(TickRate);
	}

	void Simulate(MoveState& State, const MoveInput& Input, float Dt, const CollisionWorld& World)
	{
		const uint16_t B = Input.Buttons;

		// 1. Stance.
		if ((B & Buttons::Crouch) != 0)
		{
			State.bCrouched = true;
		}
		else if (State.bCrouched && !World.OverlapsAny(PlayerBox(State.Pos, false)))
		{
			State.bCrouched = false;
		}

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

		const bool bSprinting = (B & Buttons::Sprint) != 0 && (B & Buttons::Walk) == 0 && Fwd > 0.0f && !State.bCrouched;
		float Speed;
		if (State.bCrouched) { Speed = MoveTuning::CrouchSpeed; }
		else if ((B & Buttons::Walk) != 0) { Speed = MoveTuning::WalkSpeed; }
		else if (bSprinting) { Speed = MoveTuning::SprintSpeed; }
		else { Speed = MoveTuning::RunSpeed; }
		const float TX = WX * Speed;
		const float TY = WY * Speed;

		// 3. Horizontal acceleration and jumping.
		if (State.bOnGround)
		{
			const float Accel = bHasInput ? MoveTuning::GroundAccel : MoveTuning::GroundDecel;
			MoveApproach(State.Vel.X, State.Vel.Y, TX, TY, Accel * Dt);
			if ((B & Buttons::Jump) != 0 && State.JumpTimer <= 0.0f && !State.bCrouched)
			{
				State.Vel.Z = MoveTuning::JumpSpeed;
				State.bOnGround = false;
				State.JumpTimer = MoveTuning::JumpCooldown;
			}
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

		// 6. Ground snap.
		if (bWasOnGround && !State.bOnGround && State.Vel.Z <= 0.0f)
		{
			MoveGroundSnap(State, World);
		}

		// 7. Timers.
		if (State.JumpTimer > 0.0f)
		{
			State.JumpTimer -= Dt;
			if (State.JumpTimer < 0.0f)
			{
				State.JumpTimer = 0.0f;
			}
		}
	}
}

#include "NetCore/TfpsFloatDeterminismEnd.h"
