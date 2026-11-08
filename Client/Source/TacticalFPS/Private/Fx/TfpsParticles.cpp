// Copyright TacticalFPS project.
#include "Fx/TfpsParticles.h"

#include "NetCore/TfpsMap.h"

#include <algorithm>
#include <cmath>

namespace tfps
{
namespace fx
{
namespace
{
constexpr float kGravity = 9.81f;
/** Seconds a smoke cloud takes to reach full size. */
constexpr float kCloudGrowTime = 2.5f;

Vec3 Add(const Vec3& A, const Vec3& B) { return Vec3(A.X + B.X, A.Y + B.Y, A.Z + B.Z); }
Vec3 Mul(const Vec3& A, float S) { return Vec3(A.X * S, A.Y * S, A.Z * S); }
float Dot(const Vec3& A, const Vec3& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
float Len(const Vec3& A) { return std::sqrt(Dot(A, A)); }
Vec3 Norm(const Vec3& A)
{
	const float L = Len(A);
	return L > 1e-6f ? Mul(A, 1.0f / L) : Vec3(0.0f, 0.0f, 1.0f);
}
} // namespace

float Particle::Size() const
{
	const float T = Life > 0.0f ? std::min(1.0f, Age / Life) : 1.0f;
	return Size0 + (Size1 - Size0) * T;
}

float Particle::Alpha() const
{
	if (Life <= 0.0f)
	{
		return 0.0f;
	}
	const float In = std::max(0.03f, Life * 0.1f);
	const float Out = Life * 0.35f;
	float A = 1.0f;
	if (Age < In)
	{
		A = Age / In;
	}
	const float Left = Life - Age;
	if (Left < Out)
	{
		A = std::min(A, std::max(0.0f, Left / Out));
	}
	return A;
}

int CloudPuffTarget(float Radius)
{
	return std::min(120, std::max(12, static_cast<int>(Radius * Radius * 3.0f)));
}

System::System(uint32_t Seed, size_t Capacity)
	: Cap(Capacity)
	, Rng(Seed * 747796405u + 2891336453u)
{
	if (Rng == 0)
	{
		Rng = 1;
	}
	Parts.reserve(Cap);
}

float System::Uni()
{
	Rng ^= Rng << 13;
	Rng ^= Rng >> 17;
	Rng ^= Rng << 5;
	return static_cast<float>(Rng >> 8) * (1.0f / 16777216.0f);
}

Vec3 System::RandomDir()
{
	for (;;)
	{
		const Vec3 V(Bi(), Bi(), Bi());
		const float L2 = Dot(V, V);
		if (L2 > 1e-4f && L2 <= 1.0f)
		{
			return Mul(V, 1.0f / std::sqrt(L2));
		}
	}
}

Vec3 System::Cone(const Vec3& Axis, float Spread)
{
	const Vec3 A = Norm(Axis);
	Vec3 D = Norm(Add(A, Mul(RandomDir(), Spread * 1.5f)));
	if (Dot(D, A) < 0.0f)
	{
		D = Mul(D, -1.0f); // keep it on the axis side (out of the surface)
	}
	return D;
}

Particle* System::Spawn(Look Kind, const Vec3& Pos, const Vec3& Vel, float Life, float Size0, float Size1)
{
	if (Parts.size() >= Cap)
	{
		++DroppedCount;
		return nullptr;
	}
	Parts.emplace_back();
	Particle& P = Parts.back();
	P.Kind = Kind;
	P.Pos = Pos;
	P.Vel = Vel;
	P.Life = Life;
	P.Size0 = Size0;
	P.Size1 = Size1;
	P.Shade = Range(0.75f, 1.0f);
	return &P;
}

void System::Burst(Look Kind, int Count, const Vec3& Pos, const Vec3& Axis, float Spread, float SpeedMin, float SpeedMax, float LifeMin,
	float LifeMax, float Size, float Grow, float Drag, float Gravity, float Bounce, bool bCollide)
{
	for (int I = 0; I < Count; ++I)
	{
		const float S = Size * Range(0.7f, 1.3f);
		Particle* P = Spawn(Kind, Pos, Mul(Cone(Axis, Spread), Range(SpeedMin, SpeedMax)), Range(LifeMin, LifeMax), S, S * Grow);
		if (P == nullptr)
		{
			return;
		}
		P->Drag = Drag;
		P->Gravity = Gravity;
		P->Bounce = Bounce;
		P->bCollide = bCollide;
	}
}

void System::Impact(const Vec3& Pos, const Vec3& Normal, uint8_t Material)
{
	// Start just outside the surface so colliding bits do not begin inside it.
	const Vec3 N = Norm(Normal);
	const Vec3 At = Add(Pos, Mul(N, 0.02f));
	switch (Material)
	{
	case 0xFE: // player
		Burst(Look::Blood, 10, At, N, 0.6f, 1.0f, 3.0f, 0.3f, 0.6f, 0.03f, 3.0f, 2.0f, 0.6f, 0.0f, false);
		break;
	case 4: // metal
	case 6: // reinforced
		Burst(Look::Spark, 14, At, N, 0.7f, 4.0f, 10.0f, 0.15f, 0.4f, 0.012f, 0.5f, 0.5f, 1.0f, 0.5f, true);
		Burst(Look::Smoke, 2, At, N, 0.4f, 0.3f, 1.0f, 0.5f, 0.9f, 0.05f, 4.0f, 3.0f, -0.05f, 0.0f, false);
		break;
	case 5: // glass
		Burst(Look::Glass, 12, At, N, 0.8f, 1.5f, 4.0f, 1.0f, 2.0f, 0.02f, 1.0f, 0.3f, 1.0f, 0.2f, true);
		Burst(Look::Dust, 2, At, N, 0.5f, 0.5f, 1.5f, 0.4f, 0.7f, 0.04f, 3.0f, 3.0f, 0.1f, 0.0f, false);
		break;
	case 2: // wood
		Burst(Look::Splinter, 8, At, N, 0.6f, 2.0f, 6.0f, 1.0f, 1.6f, 0.025f, 1.0f, 0.3f, 1.0f, 0.3f, true);
		Burst(Look::Dust, 4, At, N, 0.5f, 0.5f, 2.0f, 0.5f, 0.9f, 0.05f, 3.5f, 3.0f, 0.1f, 0.0f, false);
		break;
	default: // concrete, brick, drywall, dirt
		Burst(Look::Dust, Material == 7 ? 14 : 10, At, N, 0.5f, 1.0f, 4.0f, 0.6f, 1.2f, 0.08f, 4.0f, 3.0f, 0.1f, 0.0f, false);
		Burst(Look::Debris, 6, At, N, 0.6f, 2.0f, 5.0f, 0.8f, 1.4f, 0.02f, 1.0f, 0.3f, 1.0f, 0.3f, true);
		break;
	}
}

void System::Muzzle(const Vec3& Pos, const Vec3& Dir)
{
	Burst(Look::Fire, 2, Pos, Dir, 0.15f, 0.5f, 2.0f, 0.04f, 0.06f, 0.12f, 1.3f, 0.0f, 0.0f, 0.0f, false);
	Burst(Look::Smoke, 3, Pos, Dir, 0.25f, 0.5f, 1.5f, 0.4f, 0.8f, 0.05f, 5.0f, 3.0f, -0.1f, 0.0f, false);
}

void System::Explosion(const Vec3& Pos, float Radius)
{
	const float R = std::max(0.5f, Radius);
	const Vec3 Up(0.0f, 0.0f, 1.0f);
	const Vec3 At = Add(Pos, Vec3(0.0f, 0.0f, 0.05f));
	Burst(Look::Fire, 24, At, Up, 1.0f, R * 1.5f, R * 3.0f, 0.2f, 0.45f, R * 0.25f, 1.5f, 4.0f, -0.2f, 0.0f, false);
	Burst(Look::Smoke, 30, At, Up, 1.0f, R * 0.5f, R * 1.5f, 2.0f, 4.0f, R * 0.3f, 2.5f, 2.0f, -0.15f, 0.0f, false);
	Burst(Look::Dust, 20, At, Up, 1.0f, R * 1.0f, R * 2.0f, 1.0f, 2.0f, R * 0.2f, 2.0f, 3.0f, 0.2f, 0.0f, false);
	Burst(Look::Debris, 30, At, Up, 0.9f, 4.0f, 12.0f, 2.0f, 3.5f, 0.05f, 1.0f, 0.2f, 1.0f, 0.35f, true);
	Burst(Look::Spark, 20, At, Up, 1.0f, 6.0f, 14.0f, 0.3f, 0.8f, 0.015f, 0.5f, 0.5f, 1.0f, 0.4f, true);
}

void System::Flash(const Vec3& Pos)
{
	const Vec3 Up(0.0f, 0.0f, 1.0f);
	Burst(Look::Fire, 8, Pos, Up, 1.0f, 1.0f, 3.0f, 0.06f, 0.1f, 0.6f, 1.5f, 2.0f, 0.0f, 0.0f, false);
	Burst(Look::Smoke, 10, Pos, Up, 1.0f, 0.5f, 2.0f, 1.5f, 3.0f, 0.3f, 3.0f, 2.0f, -0.1f, 0.0f, false);
}

void System::Breakage(const Vec3& Center, const Vec3& Size, uint8_t Material)
{
	const float Volume = std::max(0.0f, Size.X * Size.Y * Size.Z);
	const int N = std::min(24, std::max(4, static_cast<int>(Volume * 400.0f)));
	const Look Chunk = Material == 5 ? Look::Glass : (Material == 2 ? Look::Splinter : Look::Debris);
	const float ChunkSize = Material == 5 ? 0.03f : 0.06f;
	for (int I = 0; I < N; ++I)
	{
		// Anywhere in the cell, flying out from its centre.
		const Vec3 Offset(Bi() * Size.X * 0.5f, Bi() * Size.Y * 0.5f, Bi() * Size.Z * 0.5f);
		const Vec3 Vel = Add(Mul(Norm(Offset), Range(0.5f, 3.0f)), Vec3(0.0f, 0.0f, Range(0.0f, 1.5f)));
		const float S = ChunkSize * Range(0.6f, 1.4f);
		Particle* P = Spawn(Chunk, Add(Center, Offset), Vel, Range(2.0f, 4.0f), S, S);
		if (P == nullptr)
		{
			return;
		}
		P->Drag = 0.2f;
		P->Bounce = 0.25f;
		P->bCollide = true;
	}
	if (Material != 5)
	{
		Burst(Look::Dust, N / 2, Center, Vec3(0.0f, 0.0f, 1.0f), 1.0f, 0.3f, 1.2f, 1.0f,
			2.0f, 0.25f, 2.5f, 2.0f, 0.05f, 0.0f, false);
	}
}

void System::Cloud(uint32_t Id, const Vec3& Pos, float Radius, float Duration)
{
	for (CloudEmitter& C : Clouds)
	{
		if (C.Id == Id)
		{
			C.Pos = Pos;
			return;
		}
	}
	CloudEmitter C;
	C.Id = Id;
	C.Pos = Pos;
	C.Radius = std::max(0.5f, Radius);
	C.Duration = Duration;
	Clouds.push_back(C);
}

void System::StopCloud(uint32_t Id)
{
	for (CloudEmitter& C : Clouds)
	{
		if (C.Id == Id)
		{
			C.bStopping = true;
		}
	}
}

void System::SpawnCloudPuff(const CloudEmitter& C, int Slot)
{
	// Evenly spread slots through a dome over the grenade (golden-angle
	// spiral, depth from a second low-discrepancy sequence), so the cloud has
	// no chance gaps; a little jitter keeps it from looking regular. The dome
	// grows outward while the cloud builds up.
	const int N = CloudPuffTarget(C.Radius);
	const float K = static_cast<float>(Slot) + 0.5f;
	const float Up = 1.0f - K / static_cast<float>(N);            // 1 = top .. 0 = floor
	const float Ring = std::sqrt(std::max(0.0f, 1.0f - Up * Up));
	const float Phi = K * 2.39996323f;
	Vec3 D(Ring * std::cos(Phi), Ring * std::sin(Phi), Up * 0.8f - 0.05f);
	const float Depth = std::cbrt(0.15f + 0.85f * std::fmod(K * 0.618034f, 1.0f));
	const float Grow = std::min(1.0f, 0.6f + 0.4f * C.Age / kCloudGrowTime);
	D = Add(D, Mul(RandomDir(), 0.08f));
	const Vec3 At = Add(C.Pos, Mul(D, C.Radius * 0.8f * Grow * Depth));
	const float Size = C.Radius * 0.9f;
	// Staggered lives, so the puffs of a cloud do not all fade at once.
	const float Life = C.Age < kCloudGrowTime ? Range(3.0f, 7.0f) : Range(5.0f, 7.0f);
	Particle* P = Spawn(Look::Smoke, At, Mul(RandomDir(), 0.1f), Life, Size * 0.8f, Size);
	if (P == nullptr)
	{
		return;
	}
	P->Drag = 1.0f;
	P->Gravity = 0.0f;
	P->Cloud = C.Id;
	P->Slot = static_cast<int16_t>(Slot);
}

bool System::Inside(const Vec3& P) const
{
	if (Map == nullptr)
	{
		return P.Z < 0.0f;
	}
	for (const Solid& S : Map->Solids)
	{
		const Aabb& B = S.Bounds;
		if (P.X > B.Min.X && P.X < B.Max.X && P.Y > B.Min.Y && P.Y < B.Max.Y && P.Z > B.Min.Z && P.Z < B.Max.Z)
		{
			return true;
		}
	}
	return false;
}

void System::Step(Particle& P, float Dt)
{
	if (P.bResting)
	{
		return;
	}
	P.Vel = Mul(P.Vel, std::max(0.0f, 1.0f - P.Drag * Dt));
	P.Vel.Z -= kGravity * P.Gravity * Dt;
	if (!P.bCollide)
	{
		P.Pos = Add(P.Pos, Mul(P.Vel, Dt));
		return;
	}
	// Move one axis at a time: the axis that ends up inside a solid is the one to bounce on.
	for (int Axis = 0; Axis < 3; ++Axis)
	{
		Vec3 Next = P.Pos;
		Next.Set(Axis, P.Pos.Get(Axis) + P.Vel.Get(Axis) * Dt);
		if (Inside(Next))
		{
			P.Vel.Set(Axis, -P.Vel.Get(Axis) * P.Bounce);
			if (Axis == 2)
			{
				// Landed (or hit a ceiling): ground friction, and rest once slow.
				P.Vel.X *= 0.6f;
				P.Vel.Y *= 0.6f;
				if (Len(P.Vel) < 0.3f)
				{
					P.Vel = Vec3();
					P.bResting = true;
					return;
				}
			}
		}
		else
		{
			P.Pos = Next;
		}
	}
}

void System::Update(float Dt)
{
	if (Dt <= 0.0f)
	{
		return;
	}
	Dt = std::min(Dt, 0.1f);
	// Age and move; dead particles are swapped out (order does not matter for drawing).
	for (size_t I = 0; I < Parts.size();)
	{
		Particle& P = Parts[I];
		P.Age += Dt;
		if (P.Age >= P.Life)
		{
			P = Parts.back();
			Parts.pop_back();
			continue;
		}
		Step(P, Dt);
		++I;
	}
	// Smoke clouds keep their puff count up until they stop.
	for (size_t CI = 0; CI < Clouds.size();)
	{
		CloudEmitter& C = Clouds[CI];
		C.Age += Dt;
		if (C.Age >= C.Duration)
		{
			C.bStopping = true;
		}
		const int Target = CloudPuffTarget(C.Radius);
		std::vector<uint8_t> Filled(static_cast<size_t>(Target), 0);
		int Alive = 0;
		for (const Particle& P : Parts)
		{
			if (P.Cloud == C.Id)
			{
				++Alive;
				// A slot needs a new puff before its current one starts to fade.
				if (P.Slot >= 0 && P.Slot < Target && P.Age < P.Life * 0.6f)
				{
					Filled[static_cast<size_t>(P.Slot)] = 1;
				}
			}
		}
		if (C.bStopping)
		{
			if (Alive == 0)
			{
				Clouds[CI] = Clouds.back();
				Clouds.pop_back();
				continue;
			}
		}
		else
		{
			// Fill quickly while growing, then refill slots as their puffs expire.
			const float Rate = static_cast<float>(Target) / (C.Age < kCloudGrowTime ? kCloudGrowTime * 0.6f : 1.0f);
			C.SpawnDebt = std::min(C.SpawnDebt + Rate * Dt, 8.0f);
			for (int Slot = 0; Slot < Target && C.SpawnDebt >= 1.0f; ++Slot)
			{
				if (!Filled[static_cast<size_t>(Slot)])
				{
					SpawnCloudPuff(C, Slot);
					C.SpawnDebt -= 1.0f;
				}
			}
		}
		++CI;
	}
}

} // namespace fx
} // namespace tfps
