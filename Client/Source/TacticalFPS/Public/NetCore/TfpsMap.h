// Copyright TacticalFPS project. Engine-independent NetCore.
// Mirror of Shared/Simulation/rust/src/map.rs.
#pragma once

#include "NetCore/TfpsMath.h"

#include <cmath>
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
	/** Wood, drywall, glass and brick are destructible unless the map disables it. */
	bool MaterialDestructibleByDefault(EMaterial M);

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
		bool bDestructible = false;
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

	constexpr float kCellSize = 0.3f;
	constexpr float kMinSplit = 0.6f;
	constexpr uint32_t kMaxCellsPerAxis = 64;
	constexpr uint32_t kNoCell = 0xFFFFFFFFu;

	/** Cells along an axis of the given length (mirror of cells_along). */
	uint32_t CellsAlong(float Size);

	struct CellGrid
	{
		uint32_t N[3] = {1, 1, 1};
		uint32_t First = 0;
		uint32_t Count() const { return N[0] * N[1] * N[2]; }
	};

	struct WorldSolid
	{
		Aabb Bounds;
		EMaterial Material = EMaterial::Concrete;
		bool bHasGrid = false;
		CellGrid Grid;
	};

	/**
	 * Collision world (mirror of CollisionWorld in map.rs). Destructible solids
	 * are cell grids; destroyed cells stop colliding. Candidate queries visit
	 * boxes in ascending (solid, cell) order, identical to a full scan.
	 */
	struct CollisionWorld
	{
		std::vector<WorldSolid> Solids;
		std::vector<uint64_t> Destroyed;
		uint32_t CellCount = 0;
		uint32_t Revision = 0;

		static CollisionWorld FromMap(const MapDef& Map);
		void PushBox(const Aabb& B) { PushSolid(B, EMaterial::Concrete, false); }
		void PushSolid(const Aabb& B, EMaterial Material, bool bDestructible);

		bool IsDestroyed(uint32_t Cell) const
		{
			const size_t W = Cell / 64;
			return W < Destroyed.size() && (Destroyed[W] & (1ull << (Cell % 64))) != 0;
		}
		bool DestroyCell(uint32_t Cell);
		static Aabb CellBounds(const Aabb& Solid, const uint32_t N[3], uint32_t IX, uint32_t IY, uint32_t IZ);
		/** Solid index and bounds of a cell id; false if invalid. */
		bool CellInfo(uint32_t Cell, size_t& OutSolid, Aabb& OutBounds) const;

		/** Calls Fn(const Aabb&, uint32_t CellId) -> bool (false = stop) for candidates near Query. */
		template <typename F>
		void ForEachCandidate(const Aabb& Query, float Margin, F&& Fn) const
		{
			Aabb Q;
			Q.Min = Vec3(Query.Min.X - Margin, Query.Min.Y - Margin, Query.Min.Z - Margin);
			Q.Max = Vec3(Query.Max.X + Margin, Query.Max.Y + Margin, Query.Max.Z + Margin);
			for (const WorldSolid& S : Solids)
			{
				if (!S.Bounds.Overlaps(Q)) { continue; }
				if (!S.bHasGrid)
				{
					if (!Fn(S.Bounds, kNoCell)) { return; }
					continue;
				}
				uint32_t Lo[3];
				uint32_t Hi[3];
				for (int Axis = 0; Axis < 3; ++Axis)
				{
					const float Mn = S.Bounds.Min.Get(Axis);
					const float Size = S.Bounds.Max.Get(Axis) - Mn;
					const float NF = static_cast<float>(S.Grid.N[Axis]);
					const long long L = static_cast<long long>(std::floor((Q.Min.Get(Axis) - Mn) / Size * NF)) - 1;
					const long long H = static_cast<long long>(std::floor((Q.Max.Get(Axis) - Mn) / Size * NF)) + 1;
					const long long Last = static_cast<long long>(S.Grid.N[Axis]) - 1;
					Lo[Axis] = static_cast<uint32_t>(L < 0 ? 0 : L);
					Hi[Axis] = static_cast<uint32_t>((H > Last ? Last : H) < 0 ? 0 : (H > Last ? Last : H));
				}
				for (uint32_t IZ = Lo[2]; IZ <= Hi[2]; ++IZ)
				{
					for (uint32_t IY = Lo[1]; IY <= Hi[1]; ++IY)
					{
						for (uint32_t IX = Lo[0]; IX <= Hi[0]; ++IX)
						{
							const uint32_t Id = S.Grid.First + IX + S.Grid.N[0] * (IY + S.Grid.N[1] * IZ);
							if (IsDestroyed(Id)) { continue; }
							if (!Fn(CellBounds(S.Bounds, S.Grid.N, IX, IY, IZ), Id)) { return; }
						}
					}
				}
			}
		}

		bool OverlapsAny(const Aabb& B) const
		{
			bool bHit = false;
			ForEachCandidate(B, 0.0f, [&](const Aabb& O, uint32_t) {
				if (O.Overlaps(B)) { bHit = true; return false; }
				return true;
			});
			return bHit;
		}
	};
}
