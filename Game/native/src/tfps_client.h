// TacticalFPS Godot extension: the engine-independent C++ client core
// (netcode, prediction, map, weapons, gadgets, procedural sounds) exposed to
// GDScript. All positions are returned in Godot space (Y up, metres):
// simulation (X, Y, Z) with Z up maps to Godot (X, Z, Y).
#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "NetCore/TfpsClientCore.h"
#include "NetCore/TfpsGadget.h"
#include "NetCore/TfpsMap.h"
#include "NetCore/TfpsWeapon.h"

#include <memory>

class FUdpTransport;

namespace godot
{

class TfpsClient : public RefCounted
{
	GDCLASS(TfpsClient, RefCounted)

public:
	TfpsClient();
	~TfpsClient() override;

	// Definitions (JSON text, so it works from inside an exported package).
	bool load_map_json(const String& text);
	bool load_weapons_json(const String& text);
	bool load_gadgets_json(const String& text);
	String get_load_error() const { return LoadError; }
	String get_map_name() const;

	// Connection.
	bool connect_to(const String& address, const String& player_name, int64_t join_token);
	void disconnect_from();
	void update();
	String get_phase() const;
	bool is_connected_to_server() const;
	String get_disconnect_reason() const;

	// Input: buttons bit set, aim in simulation degrees (yaw 0 = +X toward +Y), slot.
	void set_input(int buttons, float yaw_deg, float pitch_deg, int slot);

	// Local player.
	int get_player_id() const;
	Vector3 get_feet_position() const; // interpolated between fixed ticks
	Vector3 get_eye_position(float yaw_deg) const;
	Vector3 get_velocity() const;
	Dictionary get_local_state() const;
	Dictionary get_weapon() const;
	Array get_gadget_stock() const;

	// World.
	Array get_players() const;
	Array get_gadget_entities() const;
	Array get_solids() const;
	bool is_cell_destroyed(int64_t cell) const;
	Dictionary get_cell(int64_t cell) const;
	Array get_objectives() const;
	Array get_spawns() const;
	Dictionary get_match() const;
	Dictionary get_stats() const;
	Dictionary get_gadget_def(int id) const;
	Dictionary get_weapon_def(int id) const;
	/** First solid hit by a ray (Godot space, static map). Empty when nothing. */
	Dictionary raycast(const Vector3& origin, const Vector3& direction, float max_distance) const;

	// Changes since the last call.
	Array poll_events();
	Array poll_shots();
	PackedInt32Array poll_destroyed_cells();
	PackedInt32Array poll_added_solids();
	PackedInt32Array poll_reinforced_solids();
	bool poll_map_reset();

	// Procedural sounds (tfps::audio): 16-bit mono PCM at 48 kHz.
	static int sound_count();
	static Dictionary sound_info(int cue);
	static PackedByteArray render_sound(int cue, int variant);
	static int shot_cue(int weapon_id);
	static int step_cue(int material);
	static int impact_cue(int material);

	static Vector3 to_godot(const tfps::Vec3& V) { return Vector3(V.X, V.Z, V.Y); }
	static tfps::Vec3 to_sim(const Vector3& V) { return tfps::Vec3(V.x, V.z, V.y); }

protected:
	static void _bind_methods();

private:
	const tfps::CollisionWorld& WorldRef() const;
	Dictionary SolidDict(size_t Index) const;

	tfps::MapDef Map;
	tfps::WeaponSet Weapons;
	tfps::GadgetSet Gadgets;
	tfps::CollisionWorld OfflineWorld;
	bool bHasMap = false;
	String LoadError;
	std::unique_ptr<FUdpTransport> Transport;
	std::unique_ptr<tfps::ClientCore> Core;
	tfps::MoveInput Input;
};

} // namespace godot
