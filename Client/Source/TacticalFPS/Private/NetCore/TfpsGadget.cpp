// Copyright TacticalFPS project. Engine-independent NetCore.
#include "NetCore/TfpsGadget.h"
#include "NetCore/TfpsJson.h"
#include "NetCore/TfpsMath.h"

#include <cmath>

namespace tfps
{
	const GadgetDef* GadgetSet::Get(uint8_t Id) const
	{
		for (const GadgetDef& G : Gadgets)
		{
			if (G.Id == Id) { return &G; }
		}
		return nullptr;
	}

	uint64_t GadgetSet::Hash() const
	{
		Fnv1a64 H;
		H.WriteU32(static_cast<uint32_t>(Gadgets.size()));
		for (const GadgetDef& G : Gadgets)
		{
			const uint8_t B[2] = {G.Id, static_cast<uint8_t>(G.Kind)};
			H.Write(B, 2);
			for (float V : {G.ThrowSpeed, G.Fuse, G.Radius, G.Damage, G.CellDamage, G.Duration, G.Reach, G.ArmTime, G.Speed, G.JumpSpeed, G.SpotRange}) { H.WriteF32(V); }
		}
		for (const auto& Team : TeamLoadouts)
		{
			for (const GadgetLoadoutEntry& E : Team)
			{
				const uint8_t B[2] = {E.Id, E.Count};
				H.Write(B, 2);
			}
		}
		return H.Finish();
	}

	bool ParseGadgetsJson(const std::string& Text, GadgetSet& Out, std::string& OutError)
	{
		JsonValue Root;
		if (!ParseJson(Text, Root, OutError)) { return false; }
		if (JsonNumber(Root, "version", 0) != 1.0) { OutError = "unsupported gadgets file version"; return false; }
		const JsonValue* List = Root.Find("gadgets");
		const JsonValue* Teams = Root.Find("team_loadouts");
		if (List == nullptr || List->Kind != JsonValue::EKind::Array || Teams == nullptr || Teams->Items.size() != 2)
		{
			OutError = "gadgets file needs 'gadgets' and two 'team_loadouts'";
			return false;
		}
		GadgetSet Set;
		auto F = [](const JsonValue& O, const char* K) { return static_cast<float>(JsonNumber(O, K, 0.0)); };
		for (const JsonValue& J : List->Items)
		{
			GadgetDef G;
			G.Id = static_cast<uint8_t>(JsonNumber(J, "id", 0));
			if (const JsonValue* N = J.Find("name")) { G.Name = N->String; }
			const JsonValue* K = J.Find("kind");
			const std::string Kind = K != nullptr ? K->String : "";
			if (Kind == "frag") { G.Kind = EGadgetKind::Frag; }
			else if (Kind == "flash") { G.Kind = EGadgetKind::Flash; }
			else if (Kind == "smoke") { G.Kind = EGadgetKind::Smoke; }
			else if (Kind == "breach") { G.Kind = EGadgetKind::Breach; }
			else if (Kind == "drone") { G.Kind = EGadgetKind::Drone; }
			else { OutError = "unknown gadget kind '" + Kind + "'"; return false; }
			G.ThrowSpeed = F(J, "throw_speed"); G.Fuse = F(J, "fuse"); G.Radius = F(J, "radius"); G.Damage = F(J, "damage");
			G.CellDamage = F(J, "cell_damage"); G.Duration = F(J, "duration"); G.Reach = F(J, "reach"); G.ArmTime = F(J, "arm_time");
			G.Speed = F(J, "speed"); G.JumpSpeed = F(J, "jump_speed"); G.SpotRange = F(J, "spot_range");
			bool bFinite = true;
			for (float V : {G.ThrowSpeed, G.Fuse, G.Radius, G.Damage, G.CellDamage, G.Duration, G.Reach, G.ArmTime, G.Speed, G.JumpSpeed, G.SpotRange})
			{
				bFinite = bFinite && std::isfinite(V) && V >= 0.0f;
			}
			const bool bOk = G.Id != 0 && G.Id < 0x80 && bFinite && G.Radius > 0.0f
				&& (G.Kind == EGadgetKind::Breach ? G.Reach > 0.0f
					: (G.Kind == EGadgetKind::Drone ? (G.ThrowSpeed > 0.0f && G.Speed > 0.0f) : G.ThrowSpeed > 0.0f));
			if (!bOk) { OutError = "gadget '" + G.Name + "' has invalid values"; return false; }
			Set.Gadgets.push_back(G);
		}
		for (int T = 0; T < 2; ++T)
		{
			const JsonValue& Team = Teams->Items[static_cast<size_t>(T)];
			if (Team.Items.size() != kGadgetSlots) { OutError = "each team loadout needs two gadget slots"; return false; }
			for (int S = 0; S < kGadgetSlots; ++S)
			{
				const JsonValue& E = Team.Items[static_cast<size_t>(S)];
				Set.TeamLoadouts[T][S].Id = static_cast<uint8_t>(JsonNumber(E, "id", 0));
				Set.TeamLoadouts[T][S].Count = static_cast<uint8_t>(JsonNumber(E, "count", 0));
				if (Set.TeamLoadouts[T][S].Id != 0 && Set.Get(Set.TeamLoadouts[T][S].Id) == nullptr)
				{
					OutError = "team loadout uses an unknown gadget";
					return false;
				}
			}
		}
		Out = std::move(Set);
		return true;
	}
}
