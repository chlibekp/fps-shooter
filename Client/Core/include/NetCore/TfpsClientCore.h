// Copyright TacticalFPS project. Engine-independent NetCore.
//
// Client-side netcode, independent of Unreal: handshake, fixed-tick input
// sampling, client-side prediction, server reconciliation, delta snapshot
// decoding, remote entity interpolation and time dilation.
//
// Mirror of the Rust reference client (Tools/DevClient/src/lib.rs). Unreal's
// UTfpsNetSubsystem owns one instance and supplies a socket transport and the
// current time; the standalone test client does the same with BSD sockets.
#pragma once

#include "NetCore/TfpsMap.h"
#include "NetCore/TfpsMovement.h"
#include "NetCore/TfpsProtocol.h"
#include "NetCore/TfpsWeapon.h"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace tfps
{
	/** Datagram transport to a single server address. Non-blocking. */
	class ITransport
	{
	public:
		virtual ~ITransport() = default;
		virtual bool Send(const uint8_t* Data, size_t Len) = 0;
		/** Returns bytes received from the server (0 = nothing pending). Must not block. */
		virtual size_t Receive(uint8_t* Buffer, size_t Capacity) = 0;
	};

	struct ClientConfig
	{
		std::string Name = "Operator";
		uint64_t ReconnectToken = 0;
		uint64_t JoinToken = 0;
		/** Armour class requested at connect (0 medium, 1 light, 2 heavy). */
		uint8_t Armor = 0;
		/** Primary weapon attachments requested at connect (applied to the predicted weapon too). */
		uint8_t Attachments = 0;
		uint8_t InputRedundancy = 8;
		/** Remote entity interpolation delay in seconds (minimum: 2 snapshots). */
		float InterpDelay = 0.05f;
		double ConnectionTimeout = 10.0;
	};

	enum class EClientPhase : uint8_t { Requesting, Responding, Connected, Disconnected };

	enum class EDisconnectCause : uint8_t { None, Rejected, HandshakeTimeout, Timeout, MapMismatch, WeaponsMismatch, ServerDisconnect, Local };

	struct ClientEvent
	{
		enum class EType : uint8_t { Connected, PlayerJoined, PlayerLeft, PlayerConnState, Disconnected, HitConfirm, Damaged, Kill, Explosion, Flash, Spotted, MatchState, MapReset } Type = EType::Connected;
		uint16_t PlayerId = 0;
		uint8_t Team = 0;
		std::string Name;
		EConnState ConnState = EConnState::Connected;
		EDisconnectReason LeftReason = EDisconnectReason::Unknown;
		EDisconnectCause Cause = EDisconnectCause::None;
		ERejectReason RejectReason = ERejectReason::Unknown;
		// HitConfirm: PlayerId = victim. Damaged: PlayerId = attacker. Kill: PlayerId = victim, OtherId = killer.
		uint16_t OtherId = 0;
		uint8_t Amount = 0;
		uint8_t Health = 0;
		uint8_t WeaponId = 0;
		bool bHeadshot = false;
		bool bKilled = false;
		Vec3 From;          // Damaged: attacker position; Explosion / Flash: centre
		float Radius = 0;   // Explosion
		float Intensity = 0; // Flash: 0..1, how strongly the local player is blinded
		float Duration = 0;  // Spotted: marker lifetime in seconds (PlayerId = target, OtherId = spotter, From = position)
	};

	/** A shot to visualise (muzzle flash, tracer, impact). */
	struct FxShot
	{
		uint16_t Shooter = 0;
		uint8_t WeaponId = 0;
		bool bLocal = false;
		/** Fired through a suppressor (quiet, no flash). */
		bool bSuppressed = false;
		/** Local shots: origin and aim angles (the impact is decided by the server). */
		Vec3 Origin;
		float YawDeg = 0.0f;
		float PitchDeg = 0.0f;
		/** Remote shots: server-computed end point and surface. */
		Vec3 End;
		uint8_t SurfaceKind = Surface::None;
	};

	struct RemotePlayer
	{
		uint16_t Id = 0;
		std::string Name;
		uint8_t Team = 0;
		EConnState ConnState = EConnState::Connected;
		bool bPresent = false;
		std::deque<std::pair<uint32_t, EntityState>> Samples;
		Vec3 RenderPos;
		float RenderYawDeg = 0.0f;
		float RenderPitchDeg = 0.0f;
		uint8_t Flags = 0;
		uint8_t Health = 100;
	};

	struct ClientNetStats
	{
		uint64_t PacketsSent = 0;
		uint64_t PacketsReceived = 0;
		/** Data packets that failed authentication (forged, corrupted or replayed). */
		uint64_t PacketsRejectedAuth = 0;
		uint64_t BytesSent = 0;
		uint64_t BytesReceived = 0;
		uint64_t SnapshotsReceived = 0;
		uint64_t DeltaSnapshots = 0;
		uint64_t SnapshotsDroppedNoBaseline = 0;
		uint64_t Corrections = 0;
		float MaxCorrectionM = 0.0f;
		float LastCorrectionM = 0.0f;
		float RttMs = 0.0f;
		int8_t ServerInputDepth = 0;
		float TimeScale = 1.0f;
	};

	class ClientCore
	{
	public:
		/** Starts connecting immediately. Map must match the server's (verified by hash). */
		ClientCore(const ClientConfig& InConfig, const MapDef& Map, const WeaponSet& InWeapons, ITransport& InTransport, double Now);

		/** Pumps network I/O and advances fixed simulation ticks. Call every frame. */
		void Update(double Now);
		/** Input sampled at the next simulation ticks. */
		void SetInput(const MoveInput& Input) { CurrentInput = Input; }
		void Disconnect();

		bool PollEvent(ClientEvent& Out);
		/** Shots to visualise since the last call (local predicted and remote). */
		bool PollShot(FxShot& Out);
		/** Cells destroyed since the last call (remove visuals, spawn debris). */
		bool PollDestroyedCell(uint32_t& OutCell);

		EClientPhase Phase() const { return CurPhase; }
		bool IsConnected() const { return CurPhase == EClientPhase::Connected; }
		EDisconnectCause DisconnectCause() const { return Cause; }
		ERejectReason LastRejectReason() const { return RejectReason; }

		uint16_t PlayerId() const { return LocalPlayerId; }
		uint16_t TickRate() const { return ServerTickRate; }
		uint64_t ReconnectToken() const { return SessionReconnectToken; }
		uint32_t CommandSeq() const { return CmdSeq; }
		uint32_t LatestSnapshotTick() const { return LatestTick; }
		uint64_t ServerMapHash() const { return ServerHash; }

		/** Predicted local state (use for the camera). */
		const MoveState& Predicted() const { return PredictedState; }
		/** Fraction (0..1) of the way to the next fixed tick, for render interpolation. */
		float TickAlpha() const;
		/** Predicted state before the most recent tick (interpolate with Predicted()). */
		const MoveState& PreviousPredicted() const { return PrevPredictedState; }
		const MoveState& ServerState() const { return AuthState; }
		/** Predicted weapon state (ammo, reload, recoil kick for the camera). */
		const WeaponState& Weapon() const { return PredictedWeapon; }
		/** Authoritative weapon state from the newest snapshot. */
		const WeaponState& ServerWeapon() const { return AuthWeapon; }
		/** Weapon definition in a loadout slot (nullptr before connecting). */
		const WeaponDef* LoadoutWeapon(int Slot) const { return (Slot >= 0 && Slot < kWeaponSlots) ? Loadout[Slot] : nullptr; }
		const WeaponSet& Weapons() const { return WeaponDefs; }
		bool IsAlive() const { return bAlive; }
		float RespawnIn() const { return RespawnSeconds; }
		uint8_t Health() const { return LocalHealth; }
		/** Newest match state (mode, phase, round, scores, device) and when it arrived. */
		const MatchInfo& Match() const { return CurrentMatch; }
		bool HasMatch() const { return bHasMatch; }
		double MatchReceivedAt() const { return MatchAt; }
		/** Seconds left in the current phase, counted down locally since the last MatchState. */
		float MatchTimeLeft(double Now) const;
		/** Our side this round (0 attackers, 1 defenders) and squad (owner of the score). */
		uint8_t Side() const { return LocalTeam; }
		uint8_t Squad() const { return LocalTeam == 0 ? CurrentMatch.AttackingSquad : static_cast<uint8_t>(CurrentMatch.AttackingSquad ^ 1); }
		/** Hold-USE action in progress (0 none, 1 reinforce, 2 plant, 3 defuse). */
		uint8_t Action() const { return LocalAction; }
		/** The map was rebuilt for a new round since the last call (visuals rebuild too). */
		bool PollMapReset() { const bool b = bMapResetPending; bMapResetPending = false; return b; }
		/** Wall reinforcements left and progress (0..1) of the one being applied. */
		uint8_t Reinforcements() const { return LocalReinforcements; }
		float ReinforceProgress() const { return static_cast<float>(LocalReinforceProgress) / 255.0f; }
		/** Next solid added at runtime (barricade) or reinforced since the last call (visuals). Returns false when none. */
		bool PollAddedSolid(uint32_t& OutSolidIndex);
		bool PollReinforcedSolid(uint32_t& OutSolidIndex);
		/** Next door opened or closed since the last call. Returns false when none. */
		bool PollDoorChange(uint32_t& OutSolidIndex, bool& bOutOpen);
		/** SolidAdded messages that skipped an index (must stay 0). */
		uint32_t SolidDesyncs() const { return SolidDesyncCount; }
		/** Gadget entities (grenades, placed charges, smoke) in the newest snapshot. */
		const std::vector<GadgetEntityState>& Gadgets() const { return GadgetEntities; }
		/** The local player's gadget slot `Slot` (0..kGadgetSlots-1): id and remaining count. */
		GadgetStock LocalGadget(int Slot) const { return (Slot >= 0 && Slot < kGadgetSlots) ? LocalGadgets[Slot] : GadgetStock{}; }
		const std::map<uint16_t, RemotePlayer>& Players() const { return RemotePlayers; }
		const ClientNetStats& Stats() const { return NetStats; }
		const CollisionWorld& World() const { return Collision; }

	private:
		struct FCmdRecord { uint32_t Seq = 0; MoveInput Input; uint32_t RenderTime = 0; MoveState After; WeaponState WeaponAfter; };

		void SendCurrent();
		void SendHandshake(double Now);
		void AdvanceTicks(double Now);
		void RunTick(double Now);
		void SendInput(double Now);
		void Receive(double Now);
		void Process(const uint8_t* Data, size_t Len, double Now);
		void ProcessData(ByteReader& R, double Now);
		void OnMessage(const ServerMessage& M);
		void OnSnapshot(ByteReader& R);
		void Reconcile(uint32_t AckedSeq, const MoveState& Server, const WeaponState& ServerWeapon);
		void StepPredicted(MoveState& Ms, WeaponState& Ws, const MoveInput& Input, ShotBuffer* EmitShots);
		void AdjustTimeScale(int8_t Depth);
		void UpdateInterpolation();
		void SetDisconnected(EDisconnectCause InCause);
		double RenderTick() const;

		ClientConfig Config;
		ITransport& Transport;
		CollisionWorld Collision;
		uint64_t LocalMapHash = 0;
		WeaponSet WeaponDefs;
		const WeaponDef* Loadout[kWeaponSlots] = {nullptr, nullptr};
		/** The loadout with this client's attachments fitted (Loadout points here). */
		WeaponDef Fitted[kWeaponSlots];
		bool bAlive = true;
		float RespawnSeconds = 0.0f;
		WeaponState PredictedWeapon;
		WeaponState AuthWeapon;
		ShotBuffer ShotScratch;
		std::deque<FxShot> Shots;
		std::deque<uint32_t> DestroyedCells;
		std::vector<ShotEvent> ShotEventBuf;
		std::vector<GadgetEntityState> GadgetBuf;
		std::deque<uint32_t> AddedSolids;
		std::deque<uint32_t> ReinforcedSolids;
		std::deque<std::pair<uint32_t, bool>> DoorChanges;
		uint32_t SolidDesyncCount = 0;
		uint8_t LocalReinforcements = 0;
		uint8_t LocalAction = 0;
		uint8_t LocalTeam = 0;
		MatchInfo CurrentMatch;
		bool bHasMatch = false;
		double MatchAt = 0.0;
		bool bMapResetPending = false;
		MapDef MapCopy;
		uint8_t LocalReinforceProgress = 0;
		std::vector<GadgetEntityState> GadgetEntities;
		GadgetStock LocalGadgets[kGadgetSlots];

		EClientPhase CurPhase = EClientPhase::Requesting;
		EDisconnectCause Cause = EDisconnectCause::None;
		ERejectReason RejectReason = ERejectReason::Unknown;
		uint64_t Nonce = 0;
		uint64_t Cookie = 0;
		double HandshakeStarted = 0.0;
		double LastHandshakeSend = -1.0;

		uint64_t SessionKey = 0;
		uint16_t LocalPlayerId = 0;
		uint16_t ServerTickRate = 64;
		uint8_t SnapshotInterval = 1;
		uint64_t SessionReconnectToken = 0;
		uint64_t ServerHash = 0;
		float Dt = 1.0f / 64.0f;
		AckState Acks;
		ReliableReceiver RelRx;
		double LastRecv = 0.0;

		MoveInput CurrentInput;
		uint32_t CmdSeq = 0;
		std::vector<FCmdRecord> History;
		MoveState PredictedState;
		MoveState PrevPredictedState;
		MoveState AuthState;
		uint8_t LocalHealth = 100;
		bool bHaveAuthoritative = false;
		double LastFrame = -1.0;
		double TickAccum = 0.0;
		float TimeScale = 1.0f;

		std::vector<std::pair<uint32_t, std::vector<EntityState>>> Ring;
		size_t RingCursor = 0;
		uint32_t LatestTick = 0;
		std::vector<EntityState> DecodeBuf;
		double ServerTickEstimate = 0.0;

		std::map<uint16_t, RemotePlayer> RemotePlayers;
		std::deque<ClientEvent> Events;
		ClientNetStats NetStats;
		std::vector<uint8_t> SendBuf;
		std::vector<uint8_t> RecvBuf;
		// Session encryption (see TfpsCrypto.h and crypto.rs).
		uint8_t SecretKey[32] = {};
		uint8_t PublicKey[32] = {};
		uint8_t SendKey[32] = {};
		uint8_t RecvKey[32] = {};
		bool bCipher = false;
		uint32_t SendCounter = 0;
		bool bRecvAny = false;
		uint32_t RecvTop = 0;
		uint64_t RecvWindow = 0;
		void Seal(std::vector<uint8_t>& Packet);
		/** Decrypts a sealed Data packet in place; returns the plaintext length, 0 if rejected. */
		size_t Open(uint8_t* Packet, size_t Len);
		std::vector<uint16_t> AckedScratch;
	};
}
