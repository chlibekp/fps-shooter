// Operations (request "op" field):
//   load    {map, weapons, gadgets: JSON text}        -> {ok, error?, map_name}
//   world                                             -> {solids, spawns, objectives, kill_y}
//   cells   {ids: [cell ids]}                          -> {cells: [{id, min, max, solid, material}]}
//   connect {address, name, join_token: hex string}   -> {ok, error?}
//   disconnect                                         -> {ok}
//   frame   {buttons, yaw, pitch, slot}                -> full client state for this frame (see Frame)
//   raycast {origin, direction, max}                   -> {hit, distance, position, material}
//   sounds                                             -> cue table and weapon / material mappings
//   sound   {cue, variant}                             -> {pcm: base64 16-bit mono 48 kHz}
#include "tfps_native.h"

#include "UdpTransport.h"

#include "Audio/TfpsSynth.h"
#include "NetCore/TfpsJson.h"
#include "NetCore/TfpsMath.h"
#include "NetCore/TfpsMovement.h"
#include "NetCore/TfpsProtocol.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
/** Small JSON writer: commas are inserted automatically. */
class JsonOut
{
public:
	std::string S;

	JsonOut& Obj() { Sep(); S += '{'; First = true; return *this; }
	JsonOut& EndObj() { S += '}'; First = false; return *this; }
	JsonOut& Arr() { Sep(); S += '['; First = true; return *this; }
	JsonOut& EndArr() { S += ']'; First = false; return *this; }
	JsonOut& Key(const char* K)
	{
		Sep();
		S += '"';
		S += K;
		S += "\":";
		First = true;
		return *this;
	}
	JsonOut& Num(double V)
	{
		Sep();
		if (!std::isfinite(V)) { V = 0.0; }
		char B[32];
		std::snprintf(B, sizeof(B), "%.5g", V);
		S += B;
		return *this;
	}
	JsonOut& Int(long long V)
	{
		Sep();
		S += std::to_string(V);
		return *this;
	}
	JsonOut& Bool(bool V)
	{
		Sep();
		S += V ? "true" : "false";
		return *this;
	}
	JsonOut& Str(const std::string& V)
	{
		Sep();
		S += '"';
		for (char C : V)
		{
			switch (C)
			{
			case '"': S += "\\\""; break;
			case '\\': S += "\\\\"; break;
			case '\n': S += "\\n"; break;
			case '\r': S += "\\r"; break;
			case '\t': S += "\\t"; break;
			default:
				if (static_cast<unsigned char>(C) < 0x20)
				{
					char B[8];
					std::snprintf(B, sizeof(B), "\\u%04x", C);
					S += B;
				}
				else
				{
					S += C;
				}
			}
		}
		S += '"';
		return *this;
	}
	/** Simulation vector -> Godot [x, y, z]. */
	JsonOut& Vec(const tfps::Vec3& V) { return Arr().Num(V.X).Num(V.Z).Num(V.Y).EndArr(); }

private:
	void Sep()
	{
		if (!First && !S.empty() && S.back() != '{' && S.back() != '[' && S.back() != ':')
		{
			S += ',';
		}
		First = false;
	}
	bool First = true;
};

std::string Error(const std::string& Message)
{
	JsonOut O;
	O.Obj().Key("ok").Bool(false).Key("error").Str(Message).EndObj();
	return O.S;
}

std::string Ok()
{
	return "{\"ok\":true}";
}

std::string Text(const tfps::JsonValue& R, const char* Key)
{
	const tfps::JsonValue* V = R.Find(Key);
	return V != nullptr && V->Kind == tfps::JsonValue::EKind::String ? V->String : std::string();
}

/** Godot [x, y, z] -> simulation vector. */
tfps::Vec3 SimVec(const tfps::JsonValue& R, const char* Key)
{
	const tfps::JsonValue* V = R.Find(Key);
	if (V == nullptr || V->Items.size() != 3)
	{
		return tfps::Vec3();
	}
	return tfps::Vec3(static_cast<float>(V->Items[0].Number), static_cast<float>(V->Items[2].Number), static_cast<float>(V->Items[1].Number));
}

void Aabb(JsonOut& O, const tfps::Aabb& B)
{
	O.Key("min").Vec(B.Min).Key("max").Vec(B.Max);
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
	case tfps::EDisconnectCause::HandshakeTimeout: return "the server did not answer";
	case tfps::EDisconnectCause::Timeout: return "connection timed out";
	case tfps::EDisconnectCause::MapMismatch: return "the map does not match the server's";
	case tfps::EDisconnectCause::WeaponsMismatch: return "the weapons do not match the server's";
	case tfps::EDisconnectCause::ServerDisconnect: return "the server closed the connection";
	case tfps::EDisconnectCause::Local: return "disconnected";
	}
	return "";
}

std::string Base64(const uint8_t* Data, size_t Len)
{
	static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string Out;
	Out.reserve((Len + 2) / 3 * 4);
	for (size_t I = 0; I < Len; I += 3)
	{
		const uint32_t B0 = Data[I];
		const uint32_t B1 = I + 1 < Len ? Data[I + 1] : 0;
		const uint32_t B2 = I + 2 < Len ? Data[I + 2] : 0;
		const uint32_t V = (B0 << 16) | (B1 << 8) | B2;
		Out += T[(V >> 18) & 63];
		Out += T[(V >> 12) & 63];
		Out += I + 1 < Len ? T[(V >> 6) & 63] : '=';
		Out += I + 2 < Len ? T[V & 63] : '=';
	}
	return Out;
}
} // namespace

TfpsNative::TfpsNative() = default;

TfpsNative::~TfpsNative()
{
	Disconnect();
}

std::string TfpsNative::Request(const std::string& Json)
{
	tfps::JsonValue R;
	std::string Err;
	if (!tfps::ParseJson(Json, R, Err))
	{
		return Error("bad request: " + Err);
	}
	const std::string Op = Text(R, "op");
	if (Op == "frame") { return Frame(R); }
	if (Op == "load") { return Load(R); }
	if (Op == "world") { return World(); }
	if (Op == "cells") { return Cells(R); }
	if (Op == "connect") { return Connect(R); }
	if (Op == "disconnect") { Disconnect(); return Ok(); }
	if (Op == "raycast") { return Raycast(R); }
	if (Op == "sounds") { return Sounds(); }
	if (Op == "sound") { return Sound(R); }
	return Error("unknown op '" + Op + "'");
}

std::string TfpsNative::Load(const tfps::JsonValue& R)
{
	std::string Err;
	tfps::MapDef M;
	if (!tfps::ParseMapJson(Text(R, "map"), M, Err))
	{
		return Error("map: " + Err);
	}
	tfps::WeaponSet W;
	if (!tfps::ParseWeaponsJson(Text(R, "weapons"), W, Err))
	{
		return Error("weapons: " + Err);
	}
	tfps::GadgetSet G;
	if (!tfps::ParseGadgetsJson(Text(R, "gadgets"), G, Err))
	{
		return Error("gadgets: " + Err);
	}
	Disconnect();
	Map = std::move(M);
	Weapons = std::move(W);
	Gadgets = std::move(G);
	OfflineWorld = tfps::CollisionWorld::FromMap(Map);
	bHasMap = true;
	JsonOut O;
	O.Obj().Key("ok").Bool(true).Key("map_name").Str(Map.Name).EndObj();
	return O.S;
}

const tfps::CollisionWorld& TfpsNative::WorldRef() const
{
	return Core ? Core->World() : OfflineWorld;
}

std::string TfpsNative::World() const
{
	JsonOut O;
	O.Obj().Key("solids").Arr();
	const tfps::CollisionWorld& W = WorldRef();
	for (size_t I = 0; I < W.Solids.size(); ++I)
	{
		const tfps::WorldSolid& S = W.Solids[I];
		O.Obj().Key("index").Int(static_cast<long long>(I));
		Aabb(O, S.Bounds);
		O.Key("material").Int(static_cast<int>(S.Material));
		O.Key("tag").Str(I < Map.Solids.size() ? Map.Solids[I].Tag : std::string("runtime"));
		O.Key("destructible").Bool(S.bHasGrid);
		if (S.bHasGrid)
		{
			// Grid counts in Godot axis order (x, y up, z).
			O.Key("cells").Arr().Int(S.Grid.N[0]).Int(S.Grid.N[2]).Int(S.Grid.N[1]).EndArr();
			O.Key("first_cell").Int(S.Grid.First);
			O.Key("cell_count").Int(S.Grid.Count());
		}
		O.EndObj();
	}
	O.EndArr().Key("spawns").Arr();
	for (const tfps::SpawnPoint& S : Map.Spawns)
	{
		O.Obj().Key("position").Vec(S.Pos).Key("yaw").Num(S.YawDeg).Key("team").Int(S.Team).EndObj();
	}
	O.EndArr().Key("objectives").Arr();
	for (const tfps::MapDef::Objective& Ob : Map.Objectives)
	{
		O.Obj().Key("name").Str(Ob.Name);
		Aabb(O, Ob.Bounds);
		O.EndObj();
	}
	O.EndArr().Key("kill_y").Num(Map.KillZ).EndObj();
	return O.S;
}

std::string TfpsNative::Cells(const tfps::JsonValue& R) const
{
	JsonOut O;
	O.Obj().Key("cells").Arr();
	if (const tfps::JsonValue* Ids = R.Find("ids"))
	{
		for (const tfps::JsonValue& Id : Ids->Items)
		{
			const uint32_t Cell = static_cast<uint32_t>(Id.Number);
			size_t Solid = 0;
			tfps::Aabb B;
			if (!WorldRef().CellInfo(Cell, Solid, B))
			{
				continue;
			}
			O.Obj().Key("id").Int(Cell);
			Aabb(O, B);
			O.Key("solid").Int(static_cast<long long>(Solid));
			O.Key("material").Int(static_cast<int>(WorldRef().Solids[Solid].Material));
			O.Key("destroyed").Bool(WorldRef().IsDestroyed(Cell));
			O.EndObj();
		}
	}
	O.EndArr().EndObj();
	return O.S;
}

std::string TfpsNative::Connect(const tfps::JsonValue& R)
{
	Disconnect();
	if (!bHasMap)
	{
		return Error("load a map before connecting");
	}
	const std::string Address = Text(R, "address");
	Transport = std::make_unique<FUdpTransport>();
	if (!Transport->Open(Address))
	{
		Transport.reset();
		return Error("cannot open a socket to " + Address);
	}
	tfps::ClientConfig Config;
	Config.Name = Text(R, "name").substr(0, 24);
	Config.JoinToken = std::strtoull(Text(R, "join_token").c_str(), nullptr, 16);
	const int ArmorReq = static_cast<int>(tfps::JsonNumber(R, "armor", 0));
	Config.Armor = static_cast<uint8_t>(ArmorReq < 0 ? 0 : (ArmorReq > 2 ? 2 : ArmorReq));
	Config.Attachments = static_cast<uint8_t>(static_cast<int>(tfps::JsonNumber(R, "attachments", 0)) & 0x0F);
	Core = std::make_unique<tfps::ClientCore>(Config, Map, Weapons, *Transport, NowSeconds());
	return Ok();
}

void TfpsNative::Disconnect()
{
	if (Core)
	{
		Core->Disconnect();
		Core->Update(NowSeconds());
	}
	Core.reset();
	Transport.reset();
}

std::string TfpsNative::Frame(const tfps::JsonValue& R)
{
	JsonOut O;
	O.Obj();
	if (!Core)
	{
		O.Key("phase").Str("offline").EndObj();
		return O.S;
	}
	Input.Buttons = static_cast<uint16_t>(static_cast<int>(tfps::JsonNumber(R, "buttons", 0)) & tfps::Buttons::All);
	Input.YawQ = tfps::QuantizeYawDeg(static_cast<float>(tfps::JsonNumber(R, "yaw", 0)));
	Input.PitchQ = tfps::QuantizePitchDeg(static_cast<float>(tfps::JsonNumber(R, "pitch", 0)));
	Input.Slot = static_cast<uint8_t>(tfps::JsonNumber(R, "slot", 0));
	Core->SetInput(Input);
	const double Now = NowSeconds();
	Core->Update(Now);

	const char* Phase = "offline";
	switch (Core->Phase())
	{
	case tfps::EClientPhase::Requesting:
	case tfps::EClientPhase::Responding: Phase = "connecting"; break;
	case tfps::EClientPhase::Connected: Phase = "connected"; break;
	case tfps::EClientPhase::Disconnected: Phase = "disconnected"; break;
	}
	O.Key("phase").Str(Phase);
	std::string Reason = CauseName(Core->DisconnectCause());
	if (Core->DisconnectCause() == tfps::EDisconnectCause::Rejected)
	{
		Reason += std::string(": ") + tfps::RejectReasonName(Core->LastRejectReason());
	}
	O.Key("reason").Str(Reason);
	O.Key("player_id").Int(Core->PlayerId());

	// Local player, interpolated between fixed ticks for smooth rendering.
	const tfps::MoveState& M = Core->Predicted();
	const tfps::MoveState& P = Core->PreviousPredicted();
	const float A = Core->TickAlpha();
	const tfps::Vec3 Feet(P.Pos.X + (M.Pos.X - P.Pos.X) * A, P.Pos.Y + (M.Pos.Y - P.Pos.Y) * A, P.Pos.Z + (M.Pos.Z - P.Pos.Z) * A);
	const tfps::Vec3 Eye = M.EyePosition(Input.YawQ);
	O.Key("feet").Vec(Feet);
	O.Key("eye").Vec(tfps::Vec3(Feet.X + Eye.X - M.Pos.X, Feet.Y + Eye.Y - M.Pos.Y, Feet.Z + Eye.Z - M.Pos.Z));
	O.Key("velocity").Vec(M.Vel);
	O.Key("on_ground").Bool(M.bOnGround).Key("crouched").Bool(M.bCrouched).Key("prone").Bool(M.bProne);
	O.Key("lean").Num(M.Lean).Key("aim").Num(M.Aim).Key("armor").Int(M.Armor);
	O.Key("health").Int(Core->Health()).Key("alive").Bool(Core->IsAlive()).Key("respawn_in").Num(Core->RespawnIn());
	O.Key("side").Int(Core->Side()).Key("squad").Int(Core->Squad()).Key("action").Int(Core->Action());
	O.Key("reinforcements").Int(Core->Reinforcements()).Key("reinforce_progress").Num(Core->ReinforceProgress());

	// Weapon.
	const tfps::WeaponState& W = Core->Weapon();
	O.Key("weapon").Obj().Key("active").Int(W.Active).Key("reloading").Bool(W.IsReloading());
	O.Key("recoil_pitch").Num(W.RecoilPitch).Key("recoil_yaw").Num(W.RecoilYaw).Key("shot_counter").Int(W.ShotCounter);
	O.Key("equip_timer").Num(W.EquipTimer);
	if (W.Active < tfps::kWeaponSlots)
	{
		const tfps::SlotAmmo& Am = W.Slots[W.Active];
		O.Key("ammo").Int(Am.Ammo + (Am.bChambered ? 1 : 0)).Key("reserve").Int(Am.Reserve);
		if (const tfps::WeaponDef* Def = Core->LoadoutWeapon(W.Active))
		{
			const float Total = W.bReloadEmpty ? Def->ReloadEmptyTime : Def->ReloadTime;
			O.Key("id").Int(Def->Id).Key("name").Str(Def->Name).Key("class").Str(Def->Class).Key("mag_size").Int(Def->MagSize);
			O.Key("reload_progress").Num(W.IsReloading() && Total > 0.0f ? 1.0f - W.ReloadTimer / Total : 0.0f);
		}
	}
	O.Key("slots").Arr();
	for (int S = 0; S < tfps::kWeaponSlots; ++S)
	{
		const tfps::WeaponDef* Def = Core->LoadoutWeapon(S);
		O.Obj().Key("id").Int(Def ? Def->Id : 0).Key("name").Str(Def ? Def->Name : std::string()).EndObj();
	}
	O.EndArr().EndObj();

	O.Key("gadgets").Arr();
	for (int S = 0; S < tfps::kGadgetSlots; ++S)
	{
		const tfps::GadgetStock G = Core->LocalGadget(S);
		const tfps::GadgetDef* Def = Gadgets.Get(G.Id);
		O.Obj().Key("id").Int(G.Id).Key("count").Int(G.Count).Key("name").Str(Def ? Def->Name : std::string());
		O.Key("kind").Int(Def ? static_cast<int>(Def->Kind) : -1).EndObj();
	}
	O.EndArr();

	// Other players.
	O.Key("players").Arr();
	for (const auto& KV : Core->Players())
	{
		const tfps::RemotePlayer& Pl = KV.second;
		if (!Pl.bPresent || Pl.Samples.empty())
		{
			continue;
		}
		const tfps::EntityState& L = Pl.Samples.back().second;
		O.Obj().Key("id").Int(Pl.Id).Key("name").Str(Pl.Name).Key("team").Int(Pl.Team);
		O.Key("position").Vec(Pl.RenderPos).Key("yaw").Num(Pl.RenderYawDeg).Key("pitch").Num(Pl.RenderPitchDeg);
		O.Key("flags").Int(Pl.Flags).Key("health").Int(Pl.Health).Key("lean").Num(L.Lean()).Key("aim").Num(L.Aim());
		O.Key("weapon").Int(L.Weapon).Key("interrupted").Bool(Pl.ConnState != tfps::EConnState::Connected).EndObj();
	}
	O.EndArr();

	O.Key("gadget_entities").Arr();
	for (const tfps::GadgetEntityState& G : Core->Gadgets())
	{
		const tfps::GadgetDef* Def = Gadgets.Get(G.Gadget);
		O.Obj().Key("id").Int(G.Id).Key("gadget").Int(G.Gadget).Key("phase").Int(G.Phase).Key("owner").Int(G.Owner);
		O.Key("position").Vec(G.Position()).Key("kind").Int(Def ? static_cast<int>(Def->Kind) : -1);
		O.Key("radius").Num(Def ? Def->Radius : 0.0f).Key("duration").Num(Def ? Def->Duration : 0.0f).EndObj();
	}
	O.EndArr();

	if (Core->HasMatch())
	{
		const tfps::MatchInfo& Mi = Core->Match();
		O.Key("match").Obj().Key("mode").Int(Mi.Mode).Key("phase").Int(Mi.Phase).Key("round").Int(Mi.Round);
		O.Key("score").Arr().Int(Mi.Score[0]).Int(Mi.Score[1]).EndArr();
		O.Key("attacking_squad").Int(Mi.AttackingSquad).Key("time_left").Num(Core->MatchTimeLeft(Now));
		O.Key("rounds_to_win").Int(Mi.RoundsToWin).Key("device_site").Int(Mi.DeviceSite);
		O.Key("device_position").Vec(tfps::Vec3(Mi.DevicePosMm[0] * 0.001f, Mi.DevicePosMm[1] * 0.001f, Mi.DevicePosMm[2] * 0.001f));
		O.Key("last_winner").Int(Mi.LastWinner).Key("last_reason").Int(Mi.LastReason).EndObj();
	}

	const tfps::ClientNetStats& St = Core->Stats();
	O.Key("stats").Obj().Key("rtt_ms").Num(St.RttMs).Key("corrections").Int(static_cast<long long>(St.Corrections));
	O.Key("snapshots").Int(static_cast<long long>(St.SnapshotsReceived)).Key("tick_rate").Int(Core->TickRate());
	O.Key("bytes_in").Int(static_cast<long long>(St.BytesReceived)).Key("bytes_out").Int(static_cast<long long>(St.BytesSent)).EndObj();

	// Things that happened since the last frame.
	O.Key("events").Arr();
	tfps::ClientEvent E;
	while (Core->PollEvent(E))
	{
		O.Obj().Key("type").Str(EventName(E.Type)).Key("player_id").Int(E.PlayerId).Key("other_id").Int(E.OtherId);
		O.Key("team").Int(E.Team).Key("name").Str(E.Name).Key("amount").Int(E.Amount).Key("health").Int(E.Health);
		O.Key("weapon_id").Int(E.WeaponId).Key("headshot").Bool(E.bHeadshot).Key("killed").Bool(E.bKilled);
		O.Key("position").Vec(E.From).Key("radius").Num(E.Radius).Key("intensity").Num(E.Intensity).Key("duration").Num(E.Duration);
		O.Key("connected").Bool(E.ConnState == tfps::EConnState::Connected);
		if (E.Type == tfps::ClientEvent::EType::PlayerLeft)
		{
			O.Key("reason").Str(tfps::DisconnectReasonName(E.LeftReason));
		}
		O.EndObj();
	}
	O.EndArr();

	O.Key("shots").Arr();
	tfps::FxShot S;
	while (Core->PollShot(S))
	{
		O.Obj().Key("shooter").Int(S.Shooter).Key("weapon_id").Int(S.WeaponId).Key("local").Bool(S.bLocal).Key("suppressed").Bool(S.bSuppressed);
		O.Key("origin").Vec(S.Origin);
		if (S.bLocal)
		{
			// The server decides real hits; this is where the tracer and impact are drawn.
			const tfps::Vec3 Dir = tfps::DirectionFromAngles(S.YawDeg, S.PitchDeg);
			float T = 150.0f;
			tfps::EMaterial Mat = tfps::EMaterial::Concrete;
			const bool bHit = tfps::RaycastSolids(Map, S.Origin, Dir, 150.0f, T, Mat);
			O.Key("end").Vec(tfps::Vec3(S.Origin.X + Dir.X * T, S.Origin.Y + Dir.Y * T, S.Origin.Z + Dir.Z * T));
			O.Key("surface").Int(bHit ? static_cast<int>(Mat) : 0xFF);
		}
		else
		{
			O.Key("end").Vec(S.End).Key("surface").Int(S.SurfaceKind);
		}
		O.EndObj();
	}
	O.EndArr();

	uint32_t Id = 0;
	O.Key("destroyed").Arr();
	while (Core->PollDestroyedCell(Id))
	{
		O.Int(Id);
	}
	O.EndArr().Key("added_solids").Arr();
	while (Core->PollAddedSolid(Id))
	{
		O.Int(Id);
	}
	O.EndArr().Key("reinforced_solids").Arr();
	while (Core->PollReinforcedSolid(Id))
	{
		O.Int(Id);
	}
	O.EndArr().Key("doors").Arr();
	bool bOpen = false;
	while (Core->PollDoorChange(Id, bOpen))
	{
		O.Obj().Key("index").Int(Id).Key("open").Bool(bOpen).EndObj();
	}
	O.EndArr().Key("map_reset").Bool(Core->PollMapReset());
	O.EndObj();
	return O.S;
}

std::string TfpsNative::Raycast(const tfps::JsonValue& R) const
{
	JsonOut O;
	O.Obj();
	tfps::Vec3 Dir = SimVec(R, "direction");
	const float L = std::sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y + Dir.Z * Dir.Z);
	float T = 0.0f;
	tfps::EMaterial Mat = tfps::EMaterial::Concrete;
	if (bHasMap && L > 1e-6f)
	{
		Dir = tfps::Vec3(Dir.X / L, Dir.Y / L, Dir.Z / L);
		const tfps::Vec3 From = SimVec(R, "origin");
		if (tfps::RaycastSolids(Map, From, Dir, static_cast<float>(tfps::JsonNumber(R, "max", 100.0)), T, Mat))
		{
			O.Key("hit").Bool(true).Key("distance").Num(T).Key("material").Int(static_cast<int>(Mat));
			O.Key("position").Vec(tfps::Vec3(From.X + Dir.X * T, From.Y + Dir.Y * T, From.Z + Dir.Z * T));
			return O.EndObj().S;
		}
	}
	return O.Key("hit").Bool(false).EndObj().S;
}

std::string TfpsNative::Sounds() const
{
	JsonOut O;
	O.Obj().Key("cues").Arr();
	for (int C = 0; C < tfps::audio::kCueCount; ++C)
	{
		const tfps::audio::CueInfo& I = tfps::audio::Info(static_cast<tfps::audio::Cue>(C));
		O.Obj().Key("name").Str(I.Name).Key("loop").Bool(I.bLoop).Key("spatial").Bool(I.bSpatial);
		O.Key("range").Num(I.Range).Key("variants").Int(I.Variants).EndObj();
	}
	O.EndArr().Key("shot").Arr();
	for (int W = 0; W < 16; ++W)
	{
		O.Int(static_cast<int>(tfps::audio::ShotCue(static_cast<uint8_t>(W))));
	}
	O.EndArr().Key("step").Arr();
	for (int M = 0; M < 8; ++M)
	{
		O.Int(static_cast<int>(tfps::audio::StepCue(static_cast<uint8_t>(M))));
	}
	O.EndArr().Key("impact").Arr();
	for (int M = 0; M < 8; ++M)
	{
		O.Int(static_cast<int>(tfps::audio::ImpactCue(static_cast<uint8_t>(M))));
	}
	O.EndArr().Key("impact_player").Int(static_cast<int>(tfps::audio::ImpactCue(0xFE))).EndObj();
	return O.S;
}

std::string TfpsNative::Sound(const tfps::JsonValue& R) const
{
	const int Cue = static_cast<int>(tfps::JsonNumber(R, "cue", -1));
	if (Cue < 0 || Cue >= tfps::audio::kCueCount)
	{
		return Error("no such sound");
	}
	const std::vector<int16_t> Pcm = tfps::audio::Render(static_cast<tfps::audio::Cue>(Cue), static_cast<uint32_t>(tfps::JsonNumber(R, "variant", 0)));
	JsonOut O;
	O.Obj().Key("pcm").Str(Base64(reinterpret_cast<const uint8_t*>(Pcm.data()), Pcm.size() * 2)).EndObj();
	return O.S;
}
