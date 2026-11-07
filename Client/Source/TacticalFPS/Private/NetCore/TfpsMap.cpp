// Copyright TacticalFPS project. Engine-independent NetCore.
#include "NetCore/TfpsMap.h"

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

		// ---- Minimal JSON reader (objects, arrays, strings, numbers, bools, null) ----
		struct FJsonValue
		{
			enum class EKind { Null, Bool, Number, String, Array, Object } Kind = EKind::Null;
			double Number = 0.0;
			bool Bool = false;
			std::string String;
			std::vector<FJsonValue> Items;
			std::vector<std::pair<std::string, FJsonValue>> Members;

			const FJsonValue* Find(const char* Key) const
			{
				for (const auto& M : Members)
				{
					if (M.first == Key) { return &M.second; }
				}
				return nullptr;
			}
		};

		class FJsonParser
		{
		public:
			explicit FJsonParser(const std::string& InText) : Text(InText) {}

			bool Parse(FJsonValue& Out, std::string& Err)
			{
				if (!ParseValue(Out, 0)) { Err = Error.empty() ? "json syntax error" : Error; return false; }
				SkipWs();
				if (Pos != Text.size()) { Err = "trailing characters"; return false; }
				return true;
			}

		private:
			const std::string& Text;
			size_t Pos = 0;
			std::string Error;

			void SkipWs()
			{
				while (Pos < Text.size() && (Text[Pos] == ' ' || Text[Pos] == '\n' || Text[Pos] == '\r' || Text[Pos] == '\t')) { ++Pos; }
			}

			bool Fail(const char* Msg) { Error = std::string(Msg) + " at offset " + std::to_string(Pos); return false; }

			bool ParseValue(FJsonValue& Out, int Depth)
			{
				if (Depth > 32) { return Fail("nesting too deep"); }
				SkipWs();
				if (Pos >= Text.size()) { return Fail("unexpected end"); }
				const char C = Text[Pos];
				if (C == '{') { return ParseObject(Out, Depth); }
				if (C == '[') { return ParseArray(Out, Depth); }
				if (C == '"') { Out.Kind = FJsonValue::EKind::String; return ParseString(Out.String); }
				if (Text.compare(Pos, 4, "true") == 0) { Out.Kind = FJsonValue::EKind::Bool; Out.Bool = true; Pos += 4; return true; }
				if (Text.compare(Pos, 5, "false") == 0) { Out.Kind = FJsonValue::EKind::Bool; Out.Bool = false; Pos += 5; return true; }
				if (Text.compare(Pos, 4, "null") == 0) { Out.Kind = FJsonValue::EKind::Null; Pos += 4; return true; }
				return ParseNumber(Out);
			}

			bool ParseNumber(FJsonValue& Out)
			{
				const size_t Start = Pos;
				while (Pos < Text.size() && (std::strchr("+-0123456789.eE", Text[Pos]) != nullptr)) { ++Pos; }
				if (Pos == Start) { return Fail("expected value"); }
				const std::string Num = Text.substr(Start, Pos - Start);
				char* End = nullptr;
				Out.Number = std::strtod(Num.c_str(), &End);
				if (End == nullptr || *End != '\0') { return Fail("bad number"); }
				Out.Kind = FJsonValue::EKind::Number;
				return true;
			}

			bool ParseString(std::string& Out)
			{
				++Pos; // opening quote
				Out.clear();
				while (Pos < Text.size())
				{
					const char C = Text[Pos++];
					if (C == '"') { return true; }
					if (C == '\\')
					{
						if (Pos >= Text.size()) { break; }
						const char E = Text[Pos++];
						switch (E)
						{
						case '"': Out += '"'; break;
						case '\\': Out += '\\'; break;
						case '/': Out += '/'; break;
						case 'n': Out += '\n'; break;
						case 't': Out += '\t'; break;
						case 'r': Out += '\r'; break;
						case 'b': Out += '\b'; break;
						case 'f': Out += '\f'; break;
						case 'u':
						{
							if (Pos + 4 > Text.size()) { return Fail("bad escape"); }
							const unsigned long Code = std::strtoul(Text.substr(Pos, 4).c_str(), nullptr, 16);
							Pos += 4;
							// Map names/tags are ASCII; encode BMP code points as UTF-8.
							if (Code < 0x80) { Out += static_cast<char>(Code); }
							else if (Code < 0x800) { Out += static_cast<char>(0xC0 | (Code >> 6)); Out += static_cast<char>(0x80 | (Code & 0x3F)); }
							else { Out += static_cast<char>(0xE0 | (Code >> 12)); Out += static_cast<char>(0x80 | ((Code >> 6) & 0x3F)); Out += static_cast<char>(0x80 | (Code & 0x3F)); }
							break;
						}
						default: return Fail("bad escape");
						}
					}
					else
					{
						Out += C;
					}
				}
				return Fail("unterminated string");
			}

			bool ParseArray(FJsonValue& Out, int Depth)
			{
				Out.Kind = FJsonValue::EKind::Array;
				++Pos;
				SkipWs();
				if (Pos < Text.size() && Text[Pos] == ']') { ++Pos; return true; }
				while (true)
				{
					Out.Items.emplace_back();
					if (!ParseValue(Out.Items.back(), Depth + 1)) { return false; }
					SkipWs();
					if (Pos >= Text.size()) { return Fail("unterminated array"); }
					if (Text[Pos] == ',') { ++Pos; continue; }
					if (Text[Pos] == ']') { ++Pos; return true; }
					return Fail("expected , or ]");
				}
			}

			bool ParseObject(FJsonValue& Out, int Depth)
			{
				Out.Kind = FJsonValue::EKind::Object;
				++Pos;
				SkipWs();
				if (Pos < Text.size() && Text[Pos] == '}') { ++Pos; return true; }
				while (true)
				{
					SkipWs();
					if (Pos >= Text.size() || Text[Pos] != '"') { return Fail("expected key"); }
					std::string Key;
					if (!ParseString(Key)) { return false; }
					SkipWs();
					if (Pos >= Text.size() || Text[Pos] != ':') { return Fail("expected :"); }
					++Pos;
					Out.Members.emplace_back(Key, FJsonValue());
					if (!ParseValue(Out.Members.back().second, Depth + 1)) { return false; }
					SkipWs();
					if (Pos >= Text.size()) { return Fail("unterminated object"); }
					if (Text[Pos] == ',') { ++Pos; continue; }
					if (Text[Pos] == '}') { ++Pos; return true; }
					return Fail("expected , or }");
				}
			}
		};

		bool ReadVec3(const FJsonValue* V, Vec3& Out)
		{
			if (V == nullptr || V->Kind != FJsonValue::EKind::Array || V->Items.size() != 3) { return false; }
			for (int I = 0; I < 3; ++I)
			{
				if (V->Items[I].Kind != FJsonValue::EKind::Number) { return false; }
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
			const uint8_t M = static_cast<uint8_t>(S.Material);
			H.Write(&M, 1);
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
		FJsonValue Root;
		FJsonParser Parser(Text);
		if (!Parser.Parse(Root, OutError)) { return false; }
		if (Root.Kind != FJsonValue::EKind::Object) { OutError = "root must be an object"; return false; }
		MapDef Map;
		const FJsonValue* Name = Root.Find("name");
		if (Name == nullptr || Name->Kind != FJsonValue::EKind::String) { OutError = "missing name"; return false; }
		Map.Name = Name->String;
		if (const FJsonValue* KZ = Root.Find("kill_z"))
		{
			if (KZ->Kind != FJsonValue::EKind::Number) { OutError = "kill_z must be a number"; return false; }
			Map.KillZ = static_cast<float>(KZ->Number);
		}
		const FJsonValue* Solids = Root.Find("solids");
		if (Solids == nullptr || Solids->Kind != FJsonValue::EKind::Array) { OutError = "missing solids"; return false; }
		for (const FJsonValue& SV : Solids->Items)
		{
			Solid S;
			if (!ReadVec3(SV.Find("min"), S.Bounds.Min) || !ReadVec3(SV.Find("max"), S.Bounds.Max)) { OutError = "solid needs min/max"; return false; }
			const FJsonValue* Mat = SV.Find("material");
			if (Mat == nullptr || Mat->Kind != FJsonValue::EKind::String || !ParseMaterial(Mat->String, S.Material)) { OutError = "solid has unknown material"; return false; }
			if (const FJsonValue* Tag = SV.Find("tag"))
			{
				if (Tag->Kind == FJsonValue::EKind::String) { S.Tag = Tag->String; }
			}
			Map.Solids.push_back(S);
		}
		const FJsonValue* Spawns = Root.Find("spawns");
		if (Spawns == nullptr || Spawns->Kind != FJsonValue::EKind::Array) { OutError = "missing spawns"; return false; }
		for (const FJsonValue& SV : Spawns->Items)
		{
			SpawnPoint P;
			if (!ReadVec3(SV.Find("pos"), P.Pos)) { OutError = "spawn needs pos"; return false; }
			if (const FJsonValue* Y = SV.Find("yaw")) { P.YawDeg = static_cast<float>(Y->Number); }
			if (const FJsonValue* T = SV.Find("team")) { P.Team = static_cast<uint8_t>(T->Number); }
			Map.Spawns.push_back(P);
		}
		OutError = Map.Validate();
		if (!OutError.empty()) { return false; }
		OutMap = std::move(Map);
		return true;
	}

	CollisionWorld CollisionWorld::FromMap(const MapDef& Map)
	{
		CollisionWorld W;
		W.Boxes.reserve(Map.Solids.size());
		for (const Solid& S : Map.Solids) { W.Boxes.push_back(S.Bounds); }
		return W;
	}
}
