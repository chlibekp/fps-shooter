// Copyright TacticalFPS project. Engine-independent NetCore.
//
// C++ implementation of protocol v1 (Shared/Protocol/PROTOCOL.md). Mirrors
// the Rust crate Shared/Protocol/rust. Byte-level compatibility is verified by
// Client/NetCoreTests (golden vectors + live tests against the Rust server).
#pragma once

#include "NetCore/TfpsBytes.h"
#include "NetCore/TfpsMovement.h"
#include "NetCore/TfpsWeapon.h"

#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace tfps
{
	constexpr uint16_t kMagic = 0x4654;
	constexpr uint16_t kProtocolVersion = 6;
	constexpr size_t kMaxPacketSize = 1200;
	constexpr size_t kHandshakePacketSize = 256;
	constexpr size_t kMaxNameLen = 24;
	constexpr size_t kMaxMapNameLen = 64;
	constexpr size_t kMaxInputRedundancy = 16;
	constexpr size_t kMaxEntitiesPerSnapshot = 64;
	constexpr size_t kMaxReliablePayload = 512;
	constexpr size_t kMaxShotEvents = 32;
	constexpr size_t kMaxGadgetEntities = 24;

	enum class EPacketType : uint8_t
	{
		ConnectRequest = 1,
		Challenge = 2,
		ChallengeResponse = 3,
		ConnectAccepted = 4,
		ConnectRejected = 5,
		Data = 6,
		Disconnect = 7,
	};

	enum class ERejectReason : uint8_t
	{
		ServerFull = 1,
		VersionMismatch = 2,
		InvalidChallenge = 3,
		InvalidName = 4,
		Unauthorized = 5,
		Unknown = 255,
	};
	ERejectReason RejectReasonFromU8(uint8_t V);
	const char* RejectReasonName(ERejectReason R);

	enum class EDisconnectReason : uint8_t
	{
		ClientQuit = 1,
		Timeout = 2,
		Kicked = 3,
		ServerShutdown = 4,
		ProtocolError = 5,
		Unknown = 255,
	};
	EDisconnectReason DisconnectReasonFromU8(uint8_t V);
	const char* DisconnectReasonName(EDisconnectReason R);

	enum class EBodyKind : uint8_t { None = 0, Input = 1, Snapshot = 2 };

	enum class EConnState : uint8_t { Connected = 0, Interrupted = 1, Reconnecting = 2 };

	namespace MsgType
	{
		constexpr uint8_t PlayerJoined = 1;
		constexpr uint8_t PlayerLeft = 2;
		constexpr uint8_t PlayerConnState = 3;
		constexpr uint8_t HitConfirm = 4;
		constexpr uint8_t Damaged = 5;
		constexpr uint8_t Kill = 6;
		constexpr uint8_t CellsDestroyed = 7;
		constexpr uint8_t Explosion = 8;
		constexpr uint8_t Flash = 9;
		constexpr uint8_t Spotted = 10;
	}
	constexpr size_t kMaxCellsPerMessage = 90;

	namespace EntityFlags
	{
		constexpr uint8_t OnGround = 1u << 0;
		constexpr uint8_t Crouched = 1u << 1;
		constexpr uint8_t Sprinting = 1u << 2;
		constexpr uint8_t Alive = 1u << 3;
		constexpr uint8_t Prone = 1u << 4;
		constexpr uint8_t Reloading = 1u << 5;
	}

	namespace Surface
	{
		constexpr uint8_t Player = 0xFE;
		constexpr uint8_t None = 0xFF;
	}

	// ---- Envelope ---------------------------------------------------------

	struct PacketHeader
	{
		uint16_t Version = 0;
		EPacketType Type = EPacketType::Data;
	};

	void WriteHeader(ByteWriter& W, EPacketType Type);
	/** Validates magic and type. Version is returned, not checked. */
	bool ReadHeader(ByteReader& R, PacketHeader& Out);

	struct ConnectRequest
	{
		uint64_t ClientNonce = 0;
		void Encode(std::vector<uint8_t>& Out) const;
	};

	struct Challenge
	{
		uint64_t ClientNonce = 0;
		uint64_t Cookie = 0;
		void Encode(std::vector<uint8_t>& Out) const;
		bool Decode(ByteReader& R);
	};

	struct ChallengeResponse
	{
		uint64_t ClientNonce = 0;
		uint64_t Cookie = 0;
		uint64_t ReconnectToken = 0;
		uint64_t JoinToken = 0;
		std::string Name;
		void Encode(std::vector<uint8_t>& Out) const;
	};

	struct ConnectAccepted
	{
		uint64_t ClientNonce = 0;
		uint64_t SessionKey = 0;
		uint16_t PlayerId = 0;
		uint16_t TickRate = 0;
		uint8_t SnapshotInterval = 1;
		uint32_t ServerTick = 0;
		uint64_t ReconnectToken = 0;
		uint64_t MapHash = 0;
		std::string MapName;
		uint64_t WeaponsHash = 0;
		uint8_t Loadout[2] = {0, 0};
		void Encode(std::vector<uint8_t>& Out) const;
		bool Decode(ByteReader& R);
	};

	struct ConnectRejected
	{
		uint64_t ClientNonce = 0;
		ERejectReason Reason = ERejectReason::Unknown;
		bool Decode(ByteReader& R);
	};

	struct DisconnectPacket
	{
		uint64_t SessionKey = 0;
		EDisconnectReason Reason = EDisconnectReason::Unknown;
		void Encode(std::vector<uint8_t>& Out) const;
		bool Decode(ByteReader& R);
	};

	struct DataHeader
	{
		uint64_t SessionKey = 0;
		uint16_t Seq = 0;
		uint16_t Ack = 0;
		uint32_t AckBits = 0;
		void Encode(ByteWriter& W) const;
		bool Decode(ByteReader& R);
	};

	// ---- Acks ---------------------------------------------------------------

	inline bool SeqGreater(uint16_t A, uint16_t B)
	{
		return (A > B && A - B <= 32768) || (A < B && B - A > 32768);
	}

	/** Packet sequence / ack bookkeeping. Mirror of ack.rs. */
	class AckState
	{
	public:
		AckState();
		/** Returns (seq, ack, ack_bits) for the next outgoing packet. */
		void NextOutgoing(double Now, uint16_t& OutSeq, uint16_t& OutAck, uint32_t& OutAckBits);
		/** False if the packet is a duplicate or too old. Acked local seqs are appended to OutAcked. */
		bool OnIncoming(uint16_t Seq, uint16_t Ack, uint32_t AckBits, double Now, std::vector<uint16_t>* OutAcked);
		double RttSeconds() const { return Rtt; }

	private:
		struct FSent { uint16_t Seq = 0; bool bValid = false; bool bAcked = false; double SentAt = 0.0; };
		void AckOne(uint16_t Seq, double Now, std::vector<uint16_t>* OutAcked);
		uint16_t NextSeq = 0;
		uint16_t RemoteLatest = 0;
		uint32_t RemoteBits = 0;
		bool bHasRemote = false;
		std::vector<FSent> Sent;
		double Rtt = 0.1;
		bool bRttInit = false;
	};

	// ---- Reliable channel ---------------------------------------------------

	/** Receives reliable messages and releases them in order. Mirror of reliable.rs. */
	class ReliableReceiver
	{
	public:
		ReliableReceiver();
		bool Read(ByteReader& R);
		bool Pop(uint8_t& OutType, std::vector<uint8_t>& OutPayload);

	private:
		uint16_t NextExpected = 0;
		std::vector<std::pair<bool, std::pair<uint8_t, std::vector<uint8_t>>>> Buffer;
		std::deque<std::pair<uint8_t, std::vector<uint8_t>>> Ready;
	};

	/** Writes an empty reliable section (the v1 client sends no reliable messages). */
	inline void WriteEmptyReliable(ByteWriter& W) { W.U8(0); }

	struct ServerMessage
	{
		uint8_t Type = 0;
		uint16_t PlayerId = 0;
		uint8_t Team = 0;
		std::string Name;
		uint8_t Reason = 0;
		EConnState State = EConnState::Connected;
		// HitConfirm / Damaged / Kill
		uint16_t OtherId = 0;     // HitConfirm: victim; Damaged: attacker; Kill: killer (PlayerId = victim)
		uint8_t Amount = 0;       // damage
		uint8_t Health = 0;       // Damaged: remaining health
		uint8_t WeaponId = 0;     // Kill
		bool bHeadshot = false;
		bool bKilled = false;     // HitConfirm
		int32_t FromMm[3] = {0, 0, 0};    // Damaged: attacker position; Explosion / Flash: centre
		std::vector<uint32_t> Cells;      // CellsDestroyed
		uint16_t RadiusCm = 0;            // Explosion
		uint8_t Intensity = 0;            // Flash: how strongly the receiving player is blinded (0..255)
		uint8_t DurationDs = 0;           // Spotted: marker lifetime in tenths of a second (PlayerId = target, OtherId = spotter, FromMm = position)

		bool Decode(uint8_t InType, const std::vector<uint8_t>& Payload);
		std::vector<uint8_t> EncodePayload() const;
	};

	// ---- Input --------------------------------------------------------------

	struct InputBundle
	{
		uint32_t AckSnapshotTick = 0;
		uint32_t NewestSeq = 0;
		uint8_t Count = 0;
		MoveInput Cmds[kMaxInputRedundancy];
		/** Remote-entity render time per command (24.8 fixed-point server ticks). */
		uint32_t RenderTimes[kMaxInputRedundancy] = {};

		void Encode(ByteWriter& W) const;
		bool Decode(ByteReader& R);
	};

	// ---- Snapshot -----------------------------------------------------------

	struct EntityState
	{
		uint16_t Id = 0;
		int32_t PosMm[3] = {0, 0, 0};
		uint16_t YawQ = 0;
		int16_t PitchQ = 0;
		uint8_t Flags = 0;
		uint8_t Health = 0;
		int8_t LeanQ = 0;
		uint8_t Weapon = 0;
		uint8_t AimQ = 0;

		Vec3 Position() const;
		float Lean() const { return static_cast<float>(LeanQ) / 127.0f; }
		float Aim() const { return static_cast<float>(AimQ) / 255.0f; }
		bool operator==(const EntityState& O) const
		{
			return Id == O.Id && PosMm[0] == O.PosMm[0] && PosMm[1] == O.PosMm[1] && PosMm[2] == O.PosMm[2]
				&& YawQ == O.YawQ && PitchQ == O.PitchQ && Flags == O.Flags && Health == O.Health
				&& LeanQ == O.LeanQ && Weapon == O.Weapon && AimQ == O.AimQ;
		}
	};

	struct ShotEvent
	{
		uint16_t Shooter = 0;
		uint8_t Weapon = 0;
		int32_t EndMm[3] = {0, 0, 0};
		uint8_t SurfaceKind = Surface::None;
		bool operator==(const ShotEvent& O) const
		{
			return Shooter == O.Shooter && Weapon == O.Weapon && EndMm[0] == O.EndMm[0] && EndMm[1] == O.EndMm[1] && EndMm[2] == O.EndMm[2] && SurfaceKind == O.SurfaceKind;
		}
	};

	/** Gadget entity (grenade, placed charge, smoke cloud), sent in full in every snapshot. */
	struct GadgetEntityState
	{
		uint16_t Id = 0;
		uint8_t Gadget = 0;   // gadget definition id
		uint8_t Phase = 0;    // GadgetPhase
		uint16_t Owner = 0;
		int16_t PosCm[3] = {0, 0, 0};

		Vec3 Position() const { return Vec3(PosCm[0] * 0.01f, PosCm[1] * 0.01f, PosCm[2] * 0.01f); }
		bool operator==(const GadgetEntityState& O) const
		{
			return Id == O.Id && Gadget == O.Gadget && Phase == O.Phase && Owner == O.Owner && PosCm[0] == O.PosCm[0] && PosCm[1] == O.PosCm[1] && PosCm[2] == O.PosCm[2];
		}
	};

	namespace GadgetPhase
	{
		constexpr uint8_t Flying = 0;
		constexpr uint8_t Resting = 1;
		constexpr uint8_t Planted = 2;
		constexpr uint8_t Armed = 3;
		constexpr uint8_t Smoking = 4;
	}

	struct GadgetStock
	{
		uint8_t Id = 0;
		uint8_t Count = 0;
	};

	int32_t QuantizeMm(float V);
	float DequantizeMm(int32_t V);

	struct SnapshotHeader
	{
		uint32_t ServerTick = 0;
		uint32_t BaselineTick = 0;
		uint32_t LastCmdSeq = 0;
		int8_t InputBufferDepth = 0;
		MoveState Local;
		WeaponState Weapon;
		uint8_t LocalHealth = 0;
		bool bAlive = true;
		float RespawnIn = 0.0f;
		GadgetStock Gadgets[kGadgetSlots];
	};

	/** Entities must be sorted by id. Baseline may be null (then Header.BaselineTick must be 0). */
	void EncodeSnapshot(ByteWriter& W, const SnapshotHeader& Header, const std::vector<EntityState>& Entities, const std::vector<EntityState>* Baseline,
		const std::vector<ShotEvent>& Shots, const std::vector<GadgetEntityState>* Gadgets = nullptr);
	/** Returns false on malformed data or missing baseline. */
	bool PeekSnapshotBaselineTick(const uint8_t* Data, size_t Len, uint32_t& OutBaseline);
	bool DecodeSnapshot(ByteReader& R, const std::vector<EntityState>* Baseline, SnapshotHeader& OutHeader, std::vector<EntityState>& OutEntities,
		std::vector<ShotEvent>& OutShots, std::vector<GadgetEntityState>* OutGadgets = nullptr);
}
