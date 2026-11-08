#include "tfps_client.h"

#include "UdpTransport.h"

#include "Audio/TfpsSynth.h"
#include "NetCore/TfpsMath.h"
#include "NetCore/TfpsMovement.h"
#include "NetCore/TfpsProtocol.h"

#include <godot_cpp/core/class_db.hpp>

#include <cstring>

using namespace godot;

namespace
{
std::string Utf8(const String& S)
{
	const CharString C = S.utf8();
	return std::string(C.get_data(), static_cast<size_t>(C.length()));
}

String FromUtf8(const std::string& S)
{
	return String::utf8(S.c_str(), static_cast<int64_t>(S.size()));
}

const char* EventName(tfps::ClientEvent::EType T)
{
	using E = tfps::ClientEvent::EType;
	switch (T)
	{
	case E::Connected: return "connected";
	case E::PlayerJoined: return "player_joined";
	case E::PlayerLeft: return "player_left";
	case E::PlayerConnState: return "player_conn_state";
	case E::Disconnected: return "disconnected";
	case E::HitConfirm: return "hit_confirm";
	case E::Damaged: return "damaged";
	case E::Kill: return "kill";
	case E::Explosion: return "explosion";
	case E::Flash: return "flash";
	case E::Spotted: return "spotted";
	case E::MatchState: return "match_state";
	case E::MapReset: return "map_reset";
	}
	return "unknown";
}

const char* CauseName(tfps::EDisconnectCause C)
{
	switch (C)
	{
	case tfps::EDisconnectCause::None: return "";
	case tfps::EDisconnectCause::Rejected: return "rejected";
	case tfps::EDisconnectCause::HandshakeTimeout: return "server did not answer";
	case tfps::EDisconnectCause::Timeout: return "connection timed out";
	case tfps::EDisconnectCause::MapMismatch: return "map does not match the server";
	case tfps::EDisconnectCause::WeaponsMismatch: return "weapons do not match the server";
	case tfps::EDisconnectCause::ServerDisconnect: return "server closed the connection";
	case tfps::EDisconnectCause::Local: return "disconnected";
	}
	return "";
}

Dictionary AabbDict(const tfps::Aabb& B)
{
	Dictionary D;
	D["min"] = TfpsClient::to_godot(B.Min);
	D["max"] = TfpsClient::to_godot(B.Max);
	return D;
}
} // namespace

TfpsClient::TfpsClient() = default;
TfpsClient::~TfpsClient()
{
	disconnect_from();
}

bool TfpsClient::load_map_json(const String& text)
{
	tfps::MapDef M;
	std::string Err;
	if (!tfps::ParseMapJson(Utf8(text), M, Err))
	{
		LoadError = FromUtf8("map: " + Err);
		return false;
	}
	Map = std::move(M);
	OfflineWorld = tfps::CollisionWorld::FromMap(Map);
	bHasMap = true;
	return true;
}

bool TfpsClient::load_weapons_json(const String& text)
{
	std::string Err;
	tfps::WeaponSet W;
	if (!tfps::ParseWeaponsJson(Utf8(text), W, Err))
	{
		LoadError = FromUtf8("weapons: " + Err);
		return false;
	}
	Weapons = std::move(W);
	return true;
}

bool TfpsClient::load_gadgets_json(const String& text)
{
	std::string Err;
	tfps::GadgetSet G;
	if (!tfps::ParseGadgetsJson(Utf8(text), G, Err))
	{
		LoadError = FromUtf8("gadgets: " + Err);
		return false;
	}
	Gadgets = std::move(G);
	return true;
}

String TfpsClient::get_map_name() const
{
	return FromUtf8(Map.Name);
}

bool TfpsClient::connect_to(const String& address, const String& player_name, int64_t join_token)
{
	disconnect_from();
	if (!bHasMap || Weapons.Weapons.empty())
	{
		LoadError = "load the map and weapons before connecting";
		return false;
	}
	Transport = std::make_unique<FUdpTransport>();
	if (!Transport->Open(Utf8(address)))
	{
		LoadError = "cannot open a socket to " + address;
		Transport.reset();
		return false;
	}
	tfps::ClientConfig Config;
	Config.Name = Utf8(player_name).substr(0, 24);
	Config.JoinToken = static_cast<uint64_t>(join_token);
	Core = std::make_unique<tfps::ClientCore>(Config, Map, Weapons, *Transport, NowSeconds());
	return true;
}

void TfpsClient::disconnect_from()
{
	if (Core)
	{
		Core->Disconnect();
		Core->Update(NowSeconds());
	}
	Core.reset();
	Transport.reset();
}

void TfpsClient::update()
{
	if (Core)
	{
		Core->SetInput(Input);
		Core->Update(NowSeconds());
	}
}

String TfpsClient::get_phase() const
{
	if (!Core)
	{
		return "offline";
	}
	switch (Core->Phase())
	{
	case tfps::EClientPhase::Requesting:
	case tfps::EClientPhase::Responding: return "connecting";
	case tfps::EClientPhase::Connected: return "connected";
	case tfps::EClientPhase::Disconnected: return "disconnected";
	}
	return "offline";
}

bool TfpsClient::is_connected_to_server() const
{
	return Core && Core->IsConnected();
}

String TfpsClient::get_disconnect_reason() const
{
	if (!Core)
	{
		return "";
	}
	String R = CauseName(Core->DisconnectCause());
	if (Core->DisconnectCause() == tfps::EDisconnectCause::Rejected)
	{
		R += String(": ") + tfps::RejectReasonName(Core->LastRejectReason());
	}
	return R;
}

void TfpsClient::set_input(int buttons, float yaw_deg, float pitch_deg, int slot)
{
	Input.Buttons = static_cast<uint16_t>(buttons & tfps::Buttons::All);
	Input.YawQ = tfps::QuantizeYawDeg(yaw_deg);
	Input.PitchQ = tfps::QuantizePitchDeg(pitch_deg);
	Input.Slot = static_cast<uint8_t>(slot);
}

int TfpsClient::get_player_id() const
{
	return Core ? Core->PlayerId() : 0;
}

Vector3 TfpsClient::get_feet_position() const
{
	if (!Core)
	{
		return Vector3();
	}
	const tfps::Vec3 A = Core->PreviousPredicted().Pos;
	const tfps::Vec3 B = Core->Predicted().Pos;
	const float T = Core->TickAlpha();
	return to_godot(tfps::Vec3(A.X + (B.X - A.X) * T, A.Y + (B.Y - A.Y) * T, A.Z + (B.Z - A.Z) * T));
}

Vector3 TfpsClient::get_eye_position(float yaw_deg) const
{
	if (!Core)
	{
		return Vector3();
	}
	const tfps::MoveState& M = Core->Predicted();
	const tfps::Vec3 Eye = M.EyePosition(tfps::QuantizeYawDeg(yaw_deg));
	// Smooth between ticks like the feet.
	const Vector3 Offset = to_godot(Eye) - to_godot(M.Pos);
	return get_feet_position() + Offset;
}

Vector3 TfpsClient::get_velocity() const
{
	return Core ? to_godot(Core->Predicted().Vel) : Vector3();
}

Dictionary TfpsClient::get_local_state() const
{
	Dictionary D;
	if (!Core)
	{
		return D;
	}
	const tfps::MoveState& M = Core->Predicted();
	D["on_ground"] = M.bOnGround;
	D["crouched"] = M.bCrouched;
	D["prone"] = M.bProne;
	D["lean"] = M.Lean;
	D["aim"] = M.Aim;
	D["height"] = M.Height();
	D["health"] = Core->Health();
	D["alive"] = Core->IsAlive();
	D["respawn_in"] = Core->RespawnIn();
	D["side"] = Core->Side();
	D["squad"] = Core->Squad();
	D["action"] = Core->Action();
	D["reinforcements"] = Core->Reinforcements();
	D["reinforce_progress"] = Core->ReinforceProgress();
	return D;
}

Dictionary TfpsClient::get_weapon() const
{
	Dictionary D;
	if (!Core)
	{
		return D;
	}
	const tfps::WeaponState& W = Core->Weapon();
	D["active"] = W.Active;
	D["recoil_pitch"] = W.RecoilPitch;
	D["recoil_yaw"] = W.RecoilYaw;
	D["shot_counter"] = static_cast<int64_t>(W.ShotCounter);
	D["equip_timer"] = W.EquipTimer;
	D["reloading"] = W.IsReloading();
	if (W.Active < tfps::kWeaponSlots)
	{
		const tfps::SlotAmmo& A = W.Slots[W.Active];
		D["ammo"] = A.Ammo + (A.bChambered ? 1 : 0);
		D["reserve"] = A.Reserve;
		if (const tfps::WeaponDef* Def = Core->LoadoutWeapon(W.Active))
		{
			D["id"] = Def->Id;
			D["name"] = FromUtf8(Def->Name);
			D["class"] = FromUtf8(Def->Class);
			D["mag_size"] = Def->MagSize;
			const float Total = W.bReloadEmpty ? Def->ReloadEmptyTime : Def->ReloadTime;
			D["reload_progress"] = W.IsReloading() && Total > 0.0f ? 1.0f - W.ReloadTimer / Total : 0.0f;
		}
	}
	Array Slots;
	for (int S = 0; S < tfps::kWeaponSlots; ++S)
	{
		const tfps::WeaponDef* Def = Core->LoadoutWeapon(S);
		Slots.append(Def ? FromUtf8(Def->Name) : String());
	}
	D["slots"] = Slots;
	return D;
}

Array TfpsClient::get_gadget_stock() const
{
	Array A;
	if (!Core)
	{
		return A;
	}
	for (int S = 0; S < tfps::kGadgetSlots; ++S)
	{
		const tfps::GadgetStock G = Core->LocalGadget(S);
		Dictionary D;
		D["id"] = G.Id;
		D["count"] = G.Count;
		const tfps::GadgetDef* Def = Gadgets.Get(G.Id);
		D["name"] = Def ? FromUtf8(Def->Name) : String();
		D["kind"] = Def ? static_cast<int>(Def->Kind) : -1;
		A.append(D);
	}
	return A;
}

Array TfpsClient::get_players() const
{
	Array A;
	if (!Core)
	{
		return A;
	}
	for (const auto& KV : Core->Players())
	{
		const tfps::RemotePlayer& P = KV.second;
		if (!P.bPresent || P.Samples.empty())
		{
			continue;
		}
		const tfps::EntityState& Latest = P.Samples.back().second;
		Dictionary D;
		D["id"] = P.Id;
		D["name"] = FromUtf8(P.Name);
		D["team"] = P.Team;
		D["position"] = to_godot(P.RenderPos);
		D["yaw"] = P.RenderYawDeg;
		D["pitch"] = P.RenderPitchDeg;
		D["flags"] = P.Flags;
		D["health"] = P.Health;
		D["lean"] = Latest.Lean();
		D["aim"] = Latest.Aim();
		D["weapon"] = Latest.Weapon;
		D["interrupted"] = P.ConnState != tfps::EConnState::Connected;
		A.append(D);
	}
	return A;
}

Array TfpsClient::get_gadget_entities() const
{
	Array A;
	if (!Core)
	{
		return A;
	}
	for (const tfps::GadgetEntityState& G : Core->Gadgets())
	{
		Dictionary D;
		D["id"] = G.Id;
		D["gadget"] = G.Gadget;
		D["phase"] = G.Phase;
		D["owner"] = G.Owner;
		D["position"] = to_godot(G.Position());
		const tfps::GadgetDef* Def = Gadgets.Get(G.Gadget);
		D["kind"] = Def ? static_cast<int>(Def->Kind) : -1;
		D["radius"] = Def ? Def->Radius : 0.0f;
		D["duration"] = Def ? Def->Duration : 0.0f;
		A.append(D);
	}
	return A;
}

const tfps::CollisionWorld& TfpsClient::WorldRef() const
{
	return Core ? Core->World() : OfflineWorld;
}

Dictionary TfpsClient::SolidDict(size_t Index) const
{
	const tfps::CollisionWorld& W = WorldRef();
	const tfps::WorldSolid& S = W.Solids[Index];
	Dictionary D = AabbDict(S.Bounds);
	D["index"] = static_cast<int64_t>(Index);
	D["material"] = static_cast<int>(S.Material);
	D["destructible"] = S.bHasGrid;
	// Tag of the map solid it came from (map solids come first, in order).
	D["tag"] = Index < Map.Solids.size() ? FromUtf8(Map.Solids[Index].Tag) : String("runtime");
	if (S.bHasGrid)
	{
		// Cell counts along the simulation axes, reported in Godot axis order.
		D["cells"] = Vector3i(static_cast<int32_t>(S.Grid.N[0]), static_cast<int32_t>(S.Grid.N[2]), static_cast<int32_t>(S.Grid.N[1]));
		D["first_cell"] = static_cast<int64_t>(S.Grid.First);
		D["cell_count"] = static_cast<int64_t>(S.Grid.Count());
	}
	return D;
}

Array TfpsClient::get_solids() const
{
	Array A;
	const tfps::CollisionWorld& W = WorldRef();
	for (size_t I = 0; I < W.Solids.size(); ++I)
	{
		A.append(SolidDict(I));
	}
	return A;
}

bool TfpsClient::is_cell_destroyed(int64_t cell) const
{
	return WorldRef().IsDestroyed(static_cast<uint32_t>(cell));
}

Dictionary TfpsClient::get_cell(int64_t cell) const
{
	size_t Solid = 0;
	tfps::Aabb B;
	if (!WorldRef().CellInfo(static_cast<uint32_t>(cell), Solid, B))
	{
		return Dictionary();
	}
	Dictionary D = AabbDict(B);
	D["solid"] = static_cast<int64_t>(Solid);
	D["material"] = static_cast<int>(WorldRef().Solids[Solid].Material);
	return D;
}

Array TfpsClient::get_objectives() const
{
	Array A;
	for (const tfps::MapDef::Objective& O : Map.Objectives)
	{
		Dictionary D = AabbDict(O.Bounds);
		D["name"] = FromUtf8(O.Name);
		A.append(D);
	}
	return A;
}

Array TfpsClient::get_spawns() const
{
	Array A;
	for (const tfps::SpawnPoint& S : Map.Spawns)
	{
		Dictionary D;
		D["position"] = to_godot(S.Pos);
		D["yaw"] = S.YawDeg;
		D["team"] = S.Team;
		A.append(D);
	}
	return A;
}

Dictionary TfpsClient::get_match() const
{
	Dictionary D;
	if (!Core || !Core->HasMatch())
	{
		return D;
	}
	const tfps::MatchInfo& M = Core->Match();
	D["mode"] = M.Mode;
	D["phase"] = M.Phase;
	D["round"] = M.Round;
	D["score"] = Vector2i(M.Score[0], M.Score[1]);
	D["attacking_squad"] = M.AttackingSquad;
	D["time_left"] = Core->MatchTimeLeft(NowSeconds());
	D["rounds_to_win"] = M.RoundsToWin;
	D["device_site"] = M.DeviceSite;
	D["device_position"] = to_godot(tfps::Vec3(M.DevicePosMm[0] * 0.001f, M.DevicePosMm[1] * 0.001f, M.DevicePosMm[2] * 0.001f));
	D["last_winner"] = M.LastWinner;
	D["last_reason"] = M.LastReason;
	return D;
}

Dictionary TfpsClient::get_stats() const
{
	Dictionary D;
	if (!Core)
	{
		return D;
	}
	const tfps::ClientNetStats& S = Core->Stats();
	D["rtt_ms"] = S.RttMs;
	D["snapshots"] = static_cast<int64_t>(S.SnapshotsReceived);
	D["delta_snapshots"] = static_cast<int64_t>(S.DeltaSnapshots);
	D["corrections"] = static_cast<int64_t>(S.Corrections);
	D["last_correction_m"] = S.LastCorrectionM;
	D["bytes_in"] = static_cast<int64_t>(S.BytesReceived);
	D["bytes_out"] = static_cast<int64_t>(S.BytesSent);
	D["tick_rate"] = Core->TickRate();
	D["input_depth"] = S.ServerInputDepth;
	return D;
}

Dictionary TfpsClient::get_gadget_def(int id) const
{
	Dictionary D;
	if (const tfps::GadgetDef* G = Gadgets.Get(static_cast<uint8_t>(id)))
	{
		D["name"] = FromUtf8(G->Name);
		D["kind"] = static_cast<int>(G->Kind);
		D["radius"] = G->Radius;
		D["duration"] = G->Duration;
		D["fuse"] = G->Fuse;
	}
	return D;
}

Dictionary TfpsClient::get_weapon_def(int id) const
{
	Dictionary D;
	if (const tfps::WeaponDef* W = Weapons.Get(static_cast<uint8_t>(id)))
	{
		D["name"] = FromUtf8(W->Name);
		D["class"] = FromUtf8(W->Class);
		D["rpm"] = W->Rpm;
		D["mag_size"] = W->MagSize;
	}
	return D;
}

Dictionary TfpsClient::raycast(const Vector3& origin, const Vector3& direction, float max_distance) const
{
	Dictionary D;
	if (!bHasMap)
	{
		return D;
	}
	const Vector3 Dir = direction.normalized();
	float T = 0.0f;
	tfps::EMaterial Mat = tfps::EMaterial::Concrete;
	if (tfps::RaycastSolids(Map, to_sim(origin), to_sim(Dir), max_distance, T, Mat))
	{
		D["distance"] = T;
		D["position"] = origin + Dir * T;
		D["material"] = static_cast<int>(Mat);
	}
	return D;
}

Array TfpsClient::poll_events()
{
	Array A;
	if (!Core)
	{
		return A;
	}
	tfps::ClientEvent E;
	while (Core->PollEvent(E))
	{
		Dictionary D;
		D["type"] = EventName(E.Type);
		D["player_id"] = E.PlayerId;
		D["other_id"] = E.OtherId;
		D["team"] = E.Team;
		D["name"] = FromUtf8(E.Name);
		D["amount"] = E.Amount;
		D["health"] = E.Health;
		D["weapon_id"] = E.WeaponId;
		D["headshot"] = E.bHeadshot;
		D["killed"] = E.bKilled;
		D["position"] = to_godot(E.From);
		D["radius"] = E.Radius;
		D["intensity"] = E.Intensity;
		D["duration"] = E.Duration;
		D["connected"] = E.ConnState == tfps::EConnState::Connected;
		if (E.Type == tfps::ClientEvent::EType::PlayerLeft)
		{
			D["reason"] = tfps::DisconnectReasonName(E.LeftReason);
		}
		else if (E.Type == tfps::ClientEvent::EType::Disconnected)
		{
			D["reason"] = get_disconnect_reason();
		}
		A.append(D);
	}
	return A;
}

Array TfpsClient::poll_shots()
{
	Array A;
	if (!Core)
	{
		return A;
	}
	tfps::FxShot S;
	while (Core->PollShot(S))
	{
		Dictionary D;
		D["shooter"] = S.Shooter;
		D["weapon_id"] = S.WeaponId;
		D["local"] = S.bLocal;
		if (S.bLocal)
		{
			// Local shots: the end point is found here against the static map (visual only).
			const tfps::Vec3 Dir = tfps::DirectionFromAngles(S.YawDeg, S.PitchDeg);
			float T = 150.0f;
			tfps::EMaterial Mat = tfps::EMaterial::Concrete;
			int Surface = 0xFF;
			if (tfps::RaycastSolids(Map, S.Origin, Dir, 150.0f, T, Mat))
			{
				Surface = static_cast<int>(Mat);
			}
			D["origin"] = to_godot(S.Origin);
			D["end"] = to_godot(tfps::Vec3(S.Origin.X + Dir.X * T, S.Origin.Y + Dir.Y * T, S.Origin.Z + Dir.Z * T));
			D["surface"] = Surface;
		}
		else
		{
			D["origin"] = to_godot(S.Origin);
			D["end"] = to_godot(S.End);
			D["surface"] = static_cast<int>(S.SurfaceKind);
		}
		A.append(D);
	}
	return A;
}

PackedInt32Array TfpsClient::poll_destroyed_cells()
{
	PackedInt32Array A;
	uint32_t Cell = 0;
	while (Core && Core->PollDestroyedCell(Cell))
	{
		A.append(static_cast<int32_t>(Cell));
	}
	return A;
}

PackedInt32Array TfpsClient::poll_added_solids()
{
	PackedInt32Array A;
	uint32_t I = 0;
	while (Core && Core->PollAddedSolid(I))
	{
		A.append(static_cast<int32_t>(I));
	}
	return A;
}

PackedInt32Array TfpsClient::poll_reinforced_solids()
{
	PackedInt32Array A;
	uint32_t I = 0;
	while (Core && Core->PollReinforcedSolid(I))
	{
		A.append(static_cast<int32_t>(I));
	}
	return A;
}

bool TfpsClient::poll_map_reset()
{
	return Core && Core->PollMapReset();
}

int TfpsClient::sound_count()
{
	return tfps::audio::kCueCount;
}

Dictionary TfpsClient::sound_info(int cue)
{
	Dictionary D;
	if (cue < 0 || cue >= tfps::audio::kCueCount)
	{
		return D;
	}
	const tfps::audio::CueInfo& I = tfps::audio::Info(static_cast<tfps::audio::Cue>(cue));
	D["name"] = I.Name;
	D["loop"] = I.bLoop;
	D["spatial"] = I.bSpatial;
	D["range"] = I.Range;
	D["variants"] = I.Variants;
	return D;
}

PackedByteArray TfpsClient::render_sound(int cue, int variant)
{
	PackedByteArray Bytes;
	if (cue < 0 || cue >= tfps::audio::kCueCount)
	{
		return Bytes;
	}
	const std::vector<int16_t> Pcm = tfps::audio::Render(static_cast<tfps::audio::Cue>(cue), static_cast<uint32_t>(variant));
	Bytes.resize(static_cast<int64_t>(Pcm.size() * 2));
	std::memcpy(Bytes.ptrw(), Pcm.data(), Pcm.size() * 2);
	return Bytes;
}

int TfpsClient::shot_cue(int weapon_id)
{
	return static_cast<int>(tfps::audio::ShotCue(static_cast<uint8_t>(weapon_id)));
}

int TfpsClient::step_cue(int material)
{
	return static_cast<int>(tfps::audio::StepCue(static_cast<uint8_t>(material)));
}

int TfpsClient::impact_cue(int material)
{
	return static_cast<int>(tfps::audio::ImpactCue(static_cast<uint8_t>(material)));
}

void TfpsClient::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("load_map_json", "text"), &TfpsClient::load_map_json);
	ClassDB::bind_method(D_METHOD("load_weapons_json", "text"), &TfpsClient::load_weapons_json);
	ClassDB::bind_method(D_METHOD("load_gadgets_json", "text"), &TfpsClient::load_gadgets_json);
	ClassDB::bind_method(D_METHOD("get_load_error"), &TfpsClient::get_load_error);
	ClassDB::bind_method(D_METHOD("get_map_name"), &TfpsClient::get_map_name);
	ClassDB::bind_method(D_METHOD("connect_to", "address", "player_name", "join_token"), &TfpsClient::connect_to);
	ClassDB::bind_method(D_METHOD("disconnect_from"), &TfpsClient::disconnect_from);
	ClassDB::bind_method(D_METHOD("update"), &TfpsClient::update);
	ClassDB::bind_method(D_METHOD("get_phase"), &TfpsClient::get_phase);
	ClassDB::bind_method(D_METHOD("is_connected_to_server"), &TfpsClient::is_connected_to_server);
	ClassDB::bind_method(D_METHOD("get_disconnect_reason"), &TfpsClient::get_disconnect_reason);
	ClassDB::bind_method(D_METHOD("set_input", "buttons", "yaw_deg", "pitch_deg", "slot"), &TfpsClient::set_input);
	ClassDB::bind_method(D_METHOD("get_player_id"), &TfpsClient::get_player_id);
	ClassDB::bind_method(D_METHOD("get_feet_position"), &TfpsClient::get_feet_position);
	ClassDB::bind_method(D_METHOD("get_eye_position", "yaw_deg"), &TfpsClient::get_eye_position);
	ClassDB::bind_method(D_METHOD("get_velocity"), &TfpsClient::get_velocity);
	ClassDB::bind_method(D_METHOD("get_local_state"), &TfpsClient::get_local_state);
	ClassDB::bind_method(D_METHOD("get_weapon"), &TfpsClient::get_weapon);
	ClassDB::bind_method(D_METHOD("get_gadget_stock"), &TfpsClient::get_gadget_stock);
	ClassDB::bind_method(D_METHOD("get_players"), &TfpsClient::get_players);
	ClassDB::bind_method(D_METHOD("get_gadget_entities"), &TfpsClient::get_gadget_entities);
	ClassDB::bind_method(D_METHOD("get_solids"), &TfpsClient::get_solids);
	ClassDB::bind_method(D_METHOD("is_cell_destroyed", "cell"), &TfpsClient::is_cell_destroyed);
	ClassDB::bind_method(D_METHOD("get_cell", "cell"), &TfpsClient::get_cell);
	ClassDB::bind_method(D_METHOD("get_objectives"), &TfpsClient::get_objectives);
	ClassDB::bind_method(D_METHOD("get_spawns"), &TfpsClient::get_spawns);
	ClassDB::bind_method(D_METHOD("get_match"), &TfpsClient::get_match);
	ClassDB::bind_method(D_METHOD("get_stats"), &TfpsClient::get_stats);
	ClassDB::bind_method(D_METHOD("get_gadget_def", "id"), &TfpsClient::get_gadget_def);
	ClassDB::bind_method(D_METHOD("get_weapon_def", "id"), &TfpsClient::get_weapon_def);
	ClassDB::bind_method(D_METHOD("raycast", "origin", "direction", "max_distance"), &TfpsClient::raycast);
	ClassDB::bind_method(D_METHOD("poll_events"), &TfpsClient::poll_events);
	ClassDB::bind_method(D_METHOD("poll_shots"), &TfpsClient::poll_shots);
	ClassDB::bind_method(D_METHOD("poll_destroyed_cells"), &TfpsClient::poll_destroyed_cells);
	ClassDB::bind_method(D_METHOD("poll_added_solids"), &TfpsClient::poll_added_solids);
	ClassDB::bind_method(D_METHOD("poll_reinforced_solids"), &TfpsClient::poll_reinforced_solids);
	ClassDB::bind_method(D_METHOD("poll_map_reset"), &TfpsClient::poll_map_reset);
	ClassDB::bind_static_method("TfpsClient", D_METHOD("sound_count"), &TfpsClient::sound_count);
	ClassDB::bind_static_method("TfpsClient", D_METHOD("sound_info", "cue"), &TfpsClient::sound_info);
	ClassDB::bind_static_method("TfpsClient", D_METHOD("render_sound", "cue", "variant"), &TfpsClient::render_sound);
	ClassDB::bind_static_method("TfpsClient", D_METHOD("shot_cue", "weapon_id"), &TfpsClient::shot_cue);
	ClassDB::bind_static_method("TfpsClient", D_METHOD("step_cue", "material"), &TfpsClient::step_cue);
	ClassDB::bind_static_method("TfpsClient", D_METHOD("impact_cue", "material"), &TfpsClient::impact_cue);
}
