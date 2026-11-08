// TacticalFPS client core behind a JSON request interface for Godot.
// See tfps_native.cpp for the operations. Positions in replies are in Godot
// space (Y up, metres): simulation (X, Y, Z) with Z up becomes (X, Z, Y).
#pragma once

#include "NetCore/TfpsClientCore.h"
#include "NetCore/TfpsGadget.h"
#include "NetCore/TfpsJson.h"
#include "NetCore/TfpsMap.h"
#include "NetCore/TfpsWeapon.h"

#include <memory>
#include <string>

class FUdpTransport;

class TfpsNative
{
public:
	TfpsNative();
	~TfpsNative();
	/** Handles one JSON request ({"op": ...}) and returns the JSON reply. */
	std::string Request(const std::string& Json);

private:
	std::string Load(const tfps::JsonValue& R);
	std::string World() const;
	std::string Cells(const tfps::JsonValue& R) const;
	std::string Connect(const tfps::JsonValue& R);
	std::string Frame(const tfps::JsonValue& R);
	std::string Raycast(const tfps::JsonValue& R) const;
	std::string Sounds() const;
	std::string Sound(const tfps::JsonValue& R) const;
	void Disconnect();
	const tfps::CollisionWorld& WorldRef() const;

	tfps::MapDef Map;
	tfps::WeaponSet Weapons;
	tfps::GadgetSet Gadgets;
	tfps::CollisionWorld OfflineWorld;
	bool bHasMap = false;
	std::unique_ptr<FUdpTransport> Transport;
	std::unique_ptr<tfps::ClientCore> Core;
	tfps::MoveInput Input;
};
