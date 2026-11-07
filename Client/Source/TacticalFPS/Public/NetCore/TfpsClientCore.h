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
		uint8_t InputRedundancy = 8;
		/** Remote entity interpolation delay in seconds (minimum: 2 snapshots). */
		float InterpDelay = 0.05f;
		double ConnectionTimeout = 10.0;
	};

	enum class EClientPhase : uint8_t { Requesting, Responding, Connected, Disconnected };

	enum class EDisconnectCause : uint8_t { None, Rejected, HandshakeTimeout, Timeout, MapMismatch, ServerDisconnect, Local };

	struct ClientEvent
	{
		enum class EType : uint8_t { Connected, PlayerJoined, PlayerLeft, PlayerConnState, Disconnected } Type = EType::Connected;
		uint16_t PlayerId = 0;
		uint8_t Team = 0;
		std::string Name;
		EConnState ConnState = EConnState::Connected;
		EDisconnectReason LeftReason = EDisconnectReason::Unknown;
		EDisconnectCause Cause = EDisconnectCause::None;
		ERejectReason RejectReason = ERejectReason::Unknown;
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
		ClientCore(const ClientConfig& InConfig, const MapDef& Map, ITransport& InTransport, double Now);

		/** Pumps network I/O and advances fixed simulation ticks. Call every frame. */
		void Update(double Now);
		/** Input sampled at the next simulation ticks. */
		void SetInput(const MoveInput& Input) { CurrentInput = Input; }
		void Disconnect();

		bool PollEvent(ClientEvent& Out);

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
		uint8_t Health() const { return LocalHealth; }
		const std::map<uint16_t, RemotePlayer>& Players() const { return RemotePlayers; }
		const ClientNetStats& Stats() const { return NetStats; }
		const CollisionWorld& World() const { return Collision; }

	private:
		struct FCmdRecord { uint32_t Seq = 0; MoveInput Input; MoveState After; };

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
		void Reconcile(uint32_t AckedSeq, const MoveState& Server);
		void AdjustTimeScale(int8_t Depth);
		void UpdateInterpolation();
		void SetDisconnected(EDisconnectCause InCause);
		double RenderTick() const;

		ClientConfig Config;
		ITransport& Transport;
		CollisionWorld Collision;
		uint64_t LocalMapHash = 0;

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
		std::vector<uint16_t> AckedScratch;
	};
}
