// Copyright TacticalFPS project. Engine-independent NetCore.
// Mirror of Shared/Simulation/rust/src/map.rs.
#pragma once

#include "NetCore/TfpsMath.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tfps
{
	enum class EMaterial : uint8_t
	{
		Concrete = 0,
		Brick = 1,
		Wood = 2,
		Drywall = 3,
		Metal = 4,
		Glass = 5,
		Reinforced = 6,
		Dirt = 7,
	};

	/** Parses the JSON material name. Returns false if unknown. */
	bool ParseMaterial(const std::string& Name, EMaterial& Out);
	const char* MaterialName(EMaterial M);

	struct Aabb
	{
		Vec3 Min;
		Vec3 Max;
		/** Strict overlap; touching faces do not overlap. */
		bool Overlaps(const Aabb& O) const
		{
			return Min.X < O.Max.X && Max.X > O.Min.X
				&& Min.Y < O.Max.Y && Max.Y > O.Min.Y
				&& Min.Z < O.Max.Z && Max.Z > O.Min.Z;
		}
	};

	struct Solid
	{
		Aabb Bounds;
		EMaterial Material = EMaterial::Concrete;
		std::string Tag;
	};

	struct SpawnPoint
	{
		Vec3 Pos;
		float YawDeg = 0.0f;
		uint8_t Team = 0;
	};

	struct MapDef
	{
		std::string Name;
		std::vector<Solid> Solids;
		std::vector<SpawnPoint> Spawns;
		float KillZ = -50.0f;

		/** Same value as MapDef::geometry_hash() in Rust. */
		uint64_t GeometryHash() const;
		/** Returns an empty string if valid, otherwise the problem. */
		std::string Validate() const;
	};

	/**
	 * Parses the map JSON format (Shared/Definitions/maps/README.md) with a
	 * small self-contained parser so the standalone build has no dependencies.
	 * Returns false and sets OutError on failure.
	 */
	bool ParseMapJson(const std::string& Text, MapDef& OutMap, std::string& OutError);

	/**
	 * Nearest solid hit by a ray (unit direction) within MaxT. Visual use only
	 * (tracer end points); the server decides real hits.
	 */
	bool RaycastSolids(const MapDef& Map, const Vec3& Origin, const Vec3& Dir, float MaxT, float& OutT, EMaterial& OutMaterial);

	/** Unit direction from yaw (0 = +X, toward +Y) and pitch (+ = up) in degrees. */
	Vec3 DirectionFromAngles(float YawDeg, float PitchDeg);

	struct CollisionWorld
	{
		std::vector<Aabb> Boxes;

		static CollisionWorld FromMap(const MapDef& Map);
		bool OverlapsAny(const Aabb& B) const
		{
			for (const Aabb& O : Boxes)
			{
				if (O.Overlaps(B)) { return true; }
			}
			return false;
		}
	};
}
