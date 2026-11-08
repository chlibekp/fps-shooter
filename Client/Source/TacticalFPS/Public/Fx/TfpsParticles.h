// Copyright TacticalFPS project.
//
// Engine-independent particle effects: bullet impacts per surface, sparks,
// glass shards, blood, muzzle smoke, explosions (fire, dust, debris), the
// breaking of destructible cells, and smoke-grenade clouds. Particles fall,
// slow down with drag, and bounce off the map's solids. Deterministic for a
// given seed and call sequence, so it is unit tested without the engine
// (Client/NetCoreTests/src/FxTests.cpp). The Unreal client draws Particles()
// with instanced meshes (ATfpsParticleFx).
#pragma once

#include "NetCore/TfpsMath.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tfps
{
struct MapDef;

namespace fx
{

/** How a particle is drawn (one instanced mesh / material per look). */
enum class Look : uint8_t
{
	Dust,     // grey-brown puffs: concrete, plaster, dirt
	Smoke,    // grey puffs: muzzle, explosion, smoke grenades
	Fire,     // bright, short
	Spark,    // tiny, bright, fast (metal impacts, explosions)
	Glass,    // small light-blue shards
	Splinter, // wood chips
	Debris,   // chunks of the broken material
	Blood,
	Count
};
constexpr int kLookCount = static_cast<int>(Look::Count);

struct Particle
{
	Vec3 Pos;
	Vec3 Vel;
	float Age = 0.0f;
	float Life = 1.0f;
	/** Diameter (m) at birth and at death (linear in between). */
	float Size0 = 0.1f;
	float Size1 = 0.1f;
	/** Fraction of velocity lost per second. */
	float Drag = 0.0f;
	/** Multiplier on gravity (negative rises, like hot smoke). */
	float Gravity = 1.0f;
	/** Speed kept along the hit axis when bouncing (0 = sticks). */
	float Bounce = 0.0f;
	/** Brightness variation 0..1, so a cloud is not one flat colour. */
	float Shade = 1.0f;
	Look Kind = Look::Dust;
	bool bCollide = false;
	/** Came to rest on a surface: no more movement or collision. */
	bool bResting = false;
	/** Smoke-cloud emitter that owns it (0 = none) and its place in the cloud. */
	uint32_t Cloud = 0;
	int16_t Slot = -1;

	float Size() const;
	/** Opacity 0..1: quick fade-in, fade-out over the last third of life. */
	float Alpha() const;
};

class System
{
public:
	explicit System(uint32_t Seed = 1, size_t Capacity = 3000);

	/** Solids particles collide with (may be null: then only the floor at z = 0 stops them). */
	void SetMap(const MapDef* InMap) { Map = InMap; }

	/** Bullet impact. Material: tfps::EMaterial value, or 0xFE for a player. Normal points out of the surface. */
	void Impact(const Vec3& Pos, const Vec3& Normal, uint8_t Material);
	/** A short smoke puff at the muzzle (Dir: shot direction). */
	void Muzzle(const Vec3& Pos, const Vec3& Dir);
	/** Frag / breach charge. */
	void Explosion(const Vec3& Pos, float Radius);
	/** Flashbang: a bright burst and some smoke, no debris. */
	void Flash(const Vec3& Pos);
	/** A destructible cell (centre and size in m) broke. */
	void Breakage(const Vec3& Center, const Vec3& Size, uint8_t Material);
	/**
	 * Smoke grenade cloud `Id` at Pos: grows to Radius over a few seconds and
	 * keeps refreshing its puffs until StopCloud (or Duration runs out).
	 * Calling it again for a known id only moves the cloud.
	 */
	void Cloud(uint32_t Id, const Vec3& Pos, float Radius, float Duration);
	void StopCloud(uint32_t Id);

	void Update(float Dt);

	const std::vector<Particle>& Particles() const { return Parts; }
	size_t Capacity() const { return Cap; }
	/** Particles refused because the system was full (since construction). */
	uint64_t Dropped() const { return DroppedCount; }
	size_t CloudCount() const { return Clouds.size(); }

private:
	struct CloudEmitter
	{
		uint32_t Id = 0;
		Vec3 Pos;
		float Radius = 4.0f;
		float Age = 0.0f;
		float Duration = 15.0f;
		bool bStopping = false;
		float SpawnDebt = 0.0f;
	};

	float Uni();
	float Bi() { return Uni() * 2.0f - 1.0f; }
	float Range(float A, float B) { return A + (B - A) * Uni(); }
	Vec3 RandomDir();
	/** Random direction within a cone (Spread 0 = along Axis, 1 = hemisphere). */
	Vec3 Cone(const Vec3& Axis, float Spread);
	Particle* Spawn(Look Kind, const Vec3& Pos, const Vec3& Vel, float Life, float Size0, float Size1);
	void Burst(Look Kind, int Count, const Vec3& Pos, const Vec3& Axis, float Spread, float SpeedMin, float SpeedMax, float LifeMin,
		float LifeMax, float Size, float Grow, float Drag, float Gravity, float Bounce, bool bCollide);
	void SpawnCloudPuff(const CloudEmitter& C, int Slot);
	bool Inside(const Vec3& P) const;
	void Step(Particle& P, float Dt);

	const MapDef* Map = nullptr;
	std::vector<Particle> Parts;
	std::vector<CloudEmitter> Clouds;
	size_t Cap;
	uint32_t Rng;
	uint64_t DroppedCount = 0;
};

/** Places (slots) a cloud of this radius keeps filled with puffs; while a
 *  puff fades its replacement already shows, so up to twice as many puffs exist. */
int CloudPuffTarget(float Radius);

} // namespace fx
} // namespace tfps
