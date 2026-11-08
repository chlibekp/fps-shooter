// Particle effects (Fx/TfpsParticles).
#include "TestFramework.h"
#include "Fx/TfpsParticles.h"
#include "NetCore/TfpsMap.h"

#include <cmath>
#include <cstdio>

using namespace tfps;
using namespace tfps::fx;

namespace
{
	MapDef Room()
	{
		// Floor (top at z = 0) and a wall at x = 1 .. 1.2.
		MapDef M;
		Solid Floor;
		Floor.Bounds = {Vec3(-20.0f, -20.0f, -1.0f), Vec3(20.0f, 20.0f, 0.0f)};
		Solid Wall;
		Wall.Bounds = {Vec3(1.0f, -20.0f, -1.0f), Vec3(1.2f, 20.0f, 5.0f)};
		M.Solids = {Floor, Wall};
		return M;
	}

	void Run(System& S, float Seconds)
	{
		for (int I = 0; I < static_cast<int>(Seconds * 60.0f); ++I)
		{
			S.Update(1.0f / 60.0f);
		}
	}

	int CountLook(const System& S, Look L)
	{
		int N = 0;
		for (const Particle& P : S.Particles())
		{
			N += P.Kind == L ? 1 : 0;
		}
		return N;
	}
}

TFPS_TEST(FxImpactsMatchTheSurface)
{
	System S(7);
	S.Impact(Vec3(0.0f, 0.0f, 1.0f), Vec3(-1.0f, 0.0f, 0.0f), 4); // metal
	TFPS_EXPECT(CountLook(S, Look::Spark) > 0 && CountLook(S, Look::Blood) == 0);
	System G(7);
	G.Impact(Vec3(0.0f, 0.0f, 1.0f), Vec3(-1.0f, 0.0f, 0.0f), 5); // glass
	TFPS_EXPECT(CountLook(G, Look::Glass) > 0 && CountLook(G, Look::Spark) == 0);
	System F(7);
	F.Impact(Vec3(0.0f, 0.0f, 1.0f), Vec3(-1.0f, 0.0f, 0.0f), 0xFE); // player
	TFPS_EXPECT(CountLook(F, Look::Blood) > 0 && CountLook(F, Look::Debris) == 0);
	System W(7);
	W.Impact(Vec3(0.0f, 0.0f, 1.0f), Vec3(-1.0f, 0.0f, 0.0f), 2); // wood
	TFPS_EXPECT(CountLook(W, Look::Splinter) > 0);
	// Bits fly out of the surface, not into it.
	for (const Particle& P : S.Particles())
	{
		TFPS_EXPECT(P.Vel.X <= 0.0f);
	}
}

TFPS_TEST(FxParticlesBounceOffSolidsAndComeToRest)
{
	const MapDef M = Room();
	System S(3);
	S.SetMap(&M);
	S.Explosion(Vec3(0.5f, 0.0f, 0.1f), 3.0f); // next to the wall, on the floor
	S.Impact(Vec3(0.99f, 0.0f, 1.5f), Vec3(-1.0f, 0.0f, 0.0f), 0);
	int Resting = 0;
	for (int Frame = 0; Frame < 180; ++Frame)
	{
		S.Update(1.0f / 60.0f);
		for (const Particle& P : S.Particles())
		{
			if (P.bCollide)
			{
				// Never through the floor or the wall.
				TFPS_REQUIRE(P.Pos.Z >= 0.0f);
				TFPS_REQUIRE(P.Pos.X <= 1.0f);
			}
		}
	}
	for (const Particle& P : S.Particles())
	{
		Resting += P.bResting ? 1 : 0;
	}
	TFPS_EXPECT(Resting > 5);
}

TFPS_TEST(FxIsDeterministicAndEffectsFadeAndEnd)
{
	auto Script = [](System& S)
	{
		S.Muzzle(Vec3(0.0f, 0.0f, 1.5f), Vec3(1.0f, 0.0f, 0.0f));
		S.Impact(Vec3(5.0f, 0.0f, 1.0f), Vec3(-1.0f, 0.0f, 0.0f), 0);
		S.Breakage(Vec3(2.0f, 0.0f, 1.0f), Vec3(0.3f, 0.3f, 0.3f), 3);
		S.Flash(Vec3(0.0f, 2.0f, 0.2f));
		Run(S, 1.0f);
	};
	System A(11);
	System B(11);
	Script(A);
	Script(B);
	TFPS_REQUIRE(A.Particles().size() == B.Particles().size());
	bool bSame = true;
	for (size_t I = 0; I < A.Particles().size(); ++I)
	{
		bSame = bSame && A.Particles()[I].Pos == B.Particles()[I].Pos;
	}
	TFPS_EXPECT(bSame);
	for (const Particle& P : A.Particles())
	{
		TFPS_EXPECT(P.Alpha() >= 0.0f && P.Alpha() <= 1.0f && P.Size() > 0.0f);
	}
	// Everything has a finite life: no effect lingers.
	Run(A, 6.0f);
	TFPS_EXPECT(A.Particles().empty());
	Particle P;
	P.Life = 1.0f;
	P.Age = 0.999f;
	TFPS_EXPECT(P.Alpha() < 0.01f);
}

TFPS_TEST(FxCapacityIsAHardLimit)
{
	System S(5, 200);
	for (int I = 0; I < 10; ++I)
	{
		S.Explosion(Vec3(0.0f, 0.0f, 0.0f), 4.0f);
	}
	TFPS_EXPECT(S.Particles().size() <= 200);
	TFPS_EXPECT(S.Dropped() > 0);
}

TFPS_TEST(FxSmokeCloudFillsItsRadiusThenClears)
{
	const MapDef M = Room();
	System S(9);
	S.SetMap(&M);
	const float R = 4.0f;
	const Vec3 C(-6.0f, 0.0f, 0.0f);
	S.Cloud(42, C, R, 15.0f);
	uint32_t Seed = 1;
	auto Uni = [&Seed]() { Seed = Seed * 1664525u + 1013904223u; return static_cast<float>(Seed >> 8) / 16777216.0f; };
	float Elapsed = 0.0f;
	for (const float At : {4.0f, 8.0f, 12.0f})
	{
		Run(S, At - Elapsed);
		Elapsed = At;
		TFPS_EXPECT(S.CloudCount() == 1);
		TFPS_EXPECT(CountLook(S, Look::Smoke) <= 2 * CloudPuffTarget(R));
		// Sample the dome above the grenade (75% of the radius): every point
		// should sit inside a well-visible puff, so the cloud has no holes to see through.
		int Covered = 0;
		int Samples = 0;
		while (Samples < 600)
		{
			const Vec3 D(Uni() * 2.0f - 1.0f, Uni() * 2.0f - 1.0f, Uni());
			if (D.X * D.X + D.Y * D.Y + D.Z * D.Z > 1.0f)
			{
				continue;
			}
			const Vec3 Pt(C.X + D.X * R * 0.75f, C.Y + D.Y * R * 0.75f, C.Z + D.Z * R * 0.75f);
			++Samples;
			for (const Particle& P : S.Particles())
			{
				if (P.Cloud == 42 && P.Alpha() > 0.5f && Distance(P.Pos, Pt) < P.Size() * 0.5f)
				{
					++Covered;
					break;
				}
			}
		}
		const float Fraction = static_cast<float>(Covered) / static_cast<float>(Samples);
		std::printf("  smoke coverage at %.0f s: %.2f with %d puffs\n", At, Fraction, CountLook(S, Look::Smoke));
		TFPS_EXPECT(Fraction >= 0.95f);
	}
	// Stopped: no new puffs, and the old ones fade out.
	S.StopCloud(42);
	Run(S, 8.0f);
	TFPS_EXPECT(S.Particles().empty());
	TFPS_EXPECT(S.CloudCount() == 0);
}
