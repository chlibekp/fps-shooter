// Copyright TacticalFPS project. Engine-independent NetCore.
#include "NetCore/TfpsMap.h"
#include "NetCore/TfpsJson.h"
#include "NetCore/TfpsFloatDeterminism.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace tfps
{
	namespace
	{
		struct FMaterialEntry { const char* Name; EMaterial Value; };
		const FMaterialEntry GMaterialTable[] = {
			{"concrete", EMaterial::Concrete}, {"brick", EMaterial::Brick}, {"wood", EMaterial::Wood},
			{"drywall", EMaterial::Drywall}, {"metal", EMaterial::Metal}, {"glass", EMaterial::Glass},
			{"reinforced", EMaterial::Reinforced}, {"dirt", EMaterial::Dirt},
		};

		bool ReadVec3(const JsonValue* V, Vec3& Out)
		{
			if (V == nullptr || V->Kind != JsonValue::EKind::Array || V->Items.size() != 3) { return false; }
			for (int I = 0; I < 3; ++I)
			{
				if (V->Items[I].Kind != JsonValue::EKind::Number) { return false; }
				// f64 -> f32, the same conversion serde_json performs on the server.
				Out.Set(I, static_cast<float>(V->Items[I].Number));
			}
			return true;
		}
	}

	bool ParseMaterial(const std::string& Name, EMaterial& Out)
	{
		for (const FMaterialEntry& E : GMaterialTable)
		{
			if (Name == E.Name) { Out = E.Value; return true; }
		}
		return false;
	}

	bool MaterialDestructibleByDefault(EMaterial M)
	{
		return M == EMaterial::Wood || M == EMaterial::Drywall || M == EMaterial::Glass || M == EMaterial::Brick;
	}

	const char* MaterialName(EMaterial M)
	{
		for (const FMaterialEntry& E : GMaterialTable)
		{
			if (E.Value == M) { return E.Name; }
		}
		return "unknown";
	}

	uint64_t MapDef::GeometryHash() const
	{
		Fnv1a64 H;
		H.WriteU32(static_cast<uint32_t>(Solids.size()));
		for (const Solid& S : Solids)
		{
			H.WriteF32(S.Bounds.Min.X); H.WriteF32(S.Bounds.Min.Y); H.WriteF32(S.Bounds.Min.Z);
			H.WriteF32(S.Bounds.Max.X); H.WriteF32(S.Bounds.Max.Y); H.WriteF32(S.Bounds.Max.Z);
			const uint8_t M[2] = {static_cast<uint8_t>(S.Material), static_cast<uint8_t>(S.bDestructible ? 1 : 0)};
			H.Write(M, 2);
		}
		H.WriteF32(KillZ);
		return H.Finish();
	}

	std::string MapDef::Validate() const
	{
		if (Name.empty() || Name.size() > 64) { return "name must be 1..=64 bytes"; }
		if (Spawns.empty()) { return "map needs at least one spawn"; }
		if (Solids.size() > 4096) { return "too many solids (max 4096)"; }
		for (size_t I = 0; I < Solids.size(); ++I)
		{
			const Aabb& B = Solids[I].Bounds;
			const float Vals[6] = {B.Min.X, B.Min.Y, B.Min.Z, B.Max.X, B.Max.Y, B.Max.Z};
			for (float V : Vals)
			{
				if (!std::isfinite(V)) { return "solid " + std::to_string(I) + " has invalid bounds"; }
			}
			if (B.Min.X >= B.Max.X || B.Min.Y >= B.Max.Y || B.Min.Z >= B.Max.Z)
			{
				return "solid " + std::to_string(I) + " has invalid bounds";
			}
		}
		return std::string();
	}

	bool ParseMapJson(const std::string& Text, MapDef& OutMap, std::string& OutError)
	{
		JsonValue Root;
		if (!ParseJson(Text, Root, OutError)) { return false; }
		if (Root.Kind != JsonValue::EKind::Object) { OutError = "root must be an object"; return false; }
		MapDef Map;
		const JsonValue* Name = Root.Find("name");
		if (Name == nullptr || Name->Kind != JsonValue::EKind::String) { OutError = "missing name"; return false; }
		Map.Name = Name->String;
		if (const JsonValue* KZ = Root.Find("kill_z"))
		{
			if (KZ->Kind != JsonValue::EKind::Number) { OutError = "kill_z must be a number"; return false; }
			Map.KillZ = static_cast<float>(KZ->Number);
		}
		const JsonValue* Solids = Root.Find("solids");
		if (Solids == nullptr || Solids->Kind != JsonValue::EKind::Array) { OutError = "missing solids"; return false; }
		for (const JsonValue& SV : Solids->Items)
		{
			Solid S;
			if (!ReadVec3(SV.Find("min"), S.Bounds.Min) || !ReadVec3(SV.Find("max"), S.Bounds.Max)) { OutError = "solid needs min/max"; return false; }
			const JsonValue* Mat = SV.Find("material");
			if (Mat == nullptr || Mat->Kind != JsonValue::EKind::String || !ParseMaterial(Mat->String, S.Material)) { OutError = "solid has unknown material"; return false; }
			if (const JsonValue* Tag = SV.Find("tag"))
			{
				if (Tag->Kind == JsonValue::EKind::String) { S.Tag = Tag->String; }
			}
			bool bAllow = true;
			if (const JsonValue* D = SV.Find("destructible"))
			{
				if (D->Kind == JsonValue::EKind::Bool) { bAllow = D->Bool; }
			}
			S.bDestructible = bAllow && MaterialDestructibleByDefault(S.Material);
			Map.Solids.push_back(S);
		}
		const JsonValue* Spawns = Root.Find("spawns");
		if (Spawns == nullptr || Spawns->Kind != JsonValue::EKind::Array) { OutError = "missing spawns"; return false; }
		for (const JsonValue& SV : Spawns->Items)
		{
			SpawnPoint P;
			if (!ReadVec3(SV.Find("pos"), P.Pos)) { OutError = "spawn needs pos"; return false; }
			if (const JsonValue* Y = SV.Find("yaw")) { P.YawDeg = static_cast<float>(Y->Number); }
			if (const JsonValue* T = SV.Find("team")) { P.Team = static_cast<uint8_t>(T->Number); }
			Map.Spawns.push_back(P);
		}
		OutError = Map.Validate();
		if (!OutError.empty()) { return false; }
		OutMap = std::move(Map);
		return true;
	}

	bool RaycastSolids(const MapDef& Map, const Vec3& Origin, const Vec3& Dir, float MaxT, float& OutT, EMaterial& OutMaterial)
	{
		bool bHit = false;
		float Best = MaxT;
		for (const Solid& S : Map.Solids)
		{
			float TMin = 0.0f;
			float TMax = Best;
			bool bMiss = false;
			for (int Axis = 0; Axis < 3 && !bMiss; ++Axis)
			{
				const float O = Origin.Get(Axis);
				const float D = Dir.Get(Axis);
				const float Mn = S.Bounds.Min.Get(Axis);
				const float Mx = S.Bounds.Max.Get(Axis);
				if (std::fabs(D) < 1e-9f)
				{
					bMiss = O < Mn || O > Mx;
				}
				else
				{
					float T1 = (Mn - O) / D;
					float T2 = (Mx - O) / D;
					if (T1 > T2) { const float Tmp = T1; T1 = T2; T2 = Tmp; }
					if (T1 > TMin) { TMin = T1; }
					if (T2 < TMax) { TMax = T2; }
					bMiss = TMin > TMax;
				}
			}
			if (!bMiss && TMin < Best)
			{
				Best = TMin;
				OutMaterial = S.Material;
				bHit = true;
			}
		}
		OutT = Best;
		return bHit;
	}

	Vec3 DirectionFromAngles(float YawDeg, float PitchDeg)
	{
		const float Y = YawDeg * 0.017453292f;
		const float P = PitchDeg * 0.017453292f;
		return Vec3(std::cos(P) * std::cos(Y), std::cos(P) * std::sin(Y), std::sin(P));
	}

	uint32_t CellsAlong(float Size)
	{
		if (Size < kMinSplit) { return 1; }
		uint32_t N = static_cast<uint32_t>(Size / kCellSize + 0.5f);
		if (N < 1) { N = 1; }
		if (N > kMaxCellsPerAxis) { N = kMaxCellsPerAxis; }
		return N;
	}

	CollisionWorld CollisionWorld::FromMap(const MapDef& Map)
	{
		CollisionWorld W;
		W.Solids.reserve(Map.Solids.size());
		for (const Solid& S : Map.Solids) { W.PushSolid(S.Bounds, S.Material, S.bDestructible); }
		return W;
	}

	void CollisionWorld::PushSolid(const Aabb& B, EMaterial Material, bool bDestructible)
	{
		WorldSolid S;
		S.Bounds = B;
		S.Material = Material;
		S.bHasGrid = bDestructible;
		if (bDestructible)
		{
			S.Grid.N[0] = CellsAlong(B.Max.X - B.Min.X);
			S.Grid.N[1] = CellsAlong(B.Max.Y - B.Min.Y);
			S.Grid.N[2] = CellsAlong(B.Max.Z - B.Min.Z);
			S.Grid.First = CellCount;
			CellCount += S.Grid.Count();
		}
		Destroyed.resize((CellCount + 63) / 64, 0);
		Solids.push_back(S);
	}

	bool CollisionWorld::DestroyCell(uint32_t Cell)
	{
		if (Cell >= CellCount || IsDestroyed(Cell)) { return false; }
		Destroyed[Cell / 64] |= 1ull << (Cell % 64);
		++Revision;
		return true;
	}

	Aabb CollisionWorld::CellBounds(const Aabb& Solid, const uint32_t N[3], uint32_t IX, uint32_t IY, uint32_t IZ)
	{
		auto F = [&](int Axis, uint32_t I) {
			const float Mn = Solid.Min.Get(Axis);
			const float Mx = Solid.Max.Get(Axis);
			return Mn + (Mx - Mn) * (static_cast<float>(I) / static_cast<float>(N[Axis]));
		};
		Aabb B;
		B.Min = Vec3(F(0, IX), F(1, IY), F(2, IZ));
		B.Max = Vec3(F(0, IX + 1), F(1, IY + 1), F(2, IZ + 1));
		return B;
	}

	bool CollisionWorld::CellInfo(uint32_t Cell, size_t& OutSolid, Aabb& OutBounds) const
	{
		for (size_t SI = 0; SI < Solids.size(); ++SI)
		{
			const WorldSolid& S = Solids[SI];
			if (!S.bHasGrid || Cell < S.Grid.First || Cell >= S.Grid.First + S.Grid.Count()) { continue; }
			const uint32_t Local = Cell - S.Grid.First;
			const uint32_t IX = Local % S.Grid.N[0];
			const uint32_t IY = (Local / S.Grid.N[0]) % S.Grid.N[1];
			const uint32_t IZ = Local / (S.Grid.N[0] * S.Grid.N[1]);
			OutSolid = SI;
			OutBounds = CellBounds(S.Bounds, S.Grid.N, IX, IY, IZ);
			return true;
		}
		return false;
	}
}

#include "NetCore/TfpsFloatDeterminismEnd.h"
