// Copyright TacticalFPS project. Engine-independent NetCore.
// Gadget definitions (Shared/Definitions/gadgets.json). Gadgets are simulated
// only by the server; the client uses the definitions for names, icons and
// effect sizes (smoke radius, flash duration).
#pragma once

#include "NetCore/TfpsWeapon.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tfps
{
	enum class EGadgetKind : uint8_t { Frag = 0, Flash = 1, Smoke = 2, Breach = 3, Drone = 4, Camera = 5, MotionSensor = 6, Jammer = 7 };

	inline bool IsPlacedGadget(EGadgetKind K) { return K == EGadgetKind::Breach || K == EGadgetKind::Camera || K == EGadgetKind::MotionSensor || K == EGadgetKind::Jammer; }
	inline bool IsGrenade(EGadgetKind K) { return K == EGadgetKind::Frag || K == EGadgetKind::Flash || K == EGadgetKind::Smoke; }
	inline bool IsRemoteViewable(EGadgetKind K) { return K == EGadgetKind::Drone || K == EGadgetKind::Camera; }

	struct GadgetDef
	{
		uint8_t Id = 0;
		std::string Name;
		EGadgetKind Kind = EGadgetKind::Frag;
		float ThrowSpeed = 0, Fuse = 0, Radius = 0, Damage = 0, CellDamage = 0, Duration = 0, Reach = 0, ArmTime = 0;
		float Speed = 0, JumpSpeed = 0, SpotRange = 0; // drones
	};

	struct GadgetLoadoutEntry
	{
		uint8_t Id = 0;
		uint8_t Count = 0;
	};

	struct GadgetSet
	{
		std::vector<GadgetDef> Gadgets;
		GadgetLoadoutEntry TeamLoadouts[2][kGadgetSlots];

		const GadgetDef* Get(uint8_t Id) const;
		/** Same value as the Rust GadgetSet::hash (checked by the golden vectors). */
		uint64_t Hash() const;
	};

	bool ParseGadgetsJson(const std::string& Text, GadgetSet& Out, std::string& OutError);
}
