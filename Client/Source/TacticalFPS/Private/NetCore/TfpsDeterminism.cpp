// Copyright TacticalFPS project. Engine-independent NetCore.
#include "NetCore/TfpsDeterminism.h"
#include "NetCore/TfpsMovement.h"

namespace tfps
{
	uint64_t DeterminismSelfTestHash()
	{
		CollisionWorld World;
		auto Box = [](float X0, float Y0, float Z0, float X1, float Y1, float Z1)
		{
			Aabb B;
			B.Min = Vec3(X0, Y0, Z0);
			B.Max = Vec3(X1, Y1, Z1);
			return B;
		};
		World.PushBox(Box(-20.0f, -20.0f, -1.0f, 20.0f, 20.0f, 0.0f));
		World.PushBox(Box(3.0f, -2.0f, 0.0f, 4.0f, 2.0f, 0.3f));
		World.PushBox(Box(6.0f, -6.0f, 0.0f, 6.2f, 6.0f, 3.0f));
		World.PushBox(Box(-2.0f, 3.0f, 1.4f, 2.0f, 6.0f, 1.6f));
		const float Dt = TickDt(64);
		MoveState S;
		S.Pos = Vec3(0.0f, 0.0f, 0.5f);
		Fnv1a64 H;
		for (uint32_t I = 0; I < 640; ++I)
		{
			const uint32_t Phase = I / 40;
			uint16_t B = 0;
			switch (Phase % 4)
			{
			case 0: B = Buttons::Forward; break;
			case 1: B = Buttons::Forward | Buttons::Sprint | Buttons::Right; break;
			case 2: B = Buttons::Left | Buttons::Crouch; break;
			default: B = Buttons::Back | Buttons::Jump; break;
			}
			if (I % 97 == 0) { B = static_cast<uint16_t>(B | Buttons::Jump); }
			MoveInput Input;
			Input.Buttons = B;
			Input.YawQ = static_cast<uint16_t>((I * 371u) & 0xFFFFu);
			Simulate(S, Input, Dt, World);
			const float Vals[7] = {S.Pos.X, S.Pos.Y, S.Pos.Z, S.Vel.X, S.Vel.Y, S.Vel.Z, S.JumpTimer};
			for (float V : Vals) { H.WriteF32(V); }
			const uint8_t Flags[2] = {static_cast<uint8_t>(S.bOnGround ? 1 : 0), static_cast<uint8_t>(S.bCrouched ? 1 : 0)};
			H.Write(Flags, 2);
		}
		return H.Finish();
	}
}
