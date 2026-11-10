// Copyright TacticalFPS project. Engine-independent NetCore.
#include "NetCore/TfpsProtocol.h"

#include <cmath>

namespace tfps
{
	namespace
	{
		constexpr size_t kAckWindow = 256;
		constexpr size_t kRecvWindow = 256;

		constexpr uint16_t kMaskPX = 1u << 0;
		constexpr uint16_t kMaskPY = 1u << 1;
		constexpr uint16_t kMaskPZ = 1u << 2;
		constexpr uint16_t kMaskYaw = 1u << 3;
		constexpr uint16_t kMaskPitch = 1u << 4;
		constexpr uint16_t kMaskFlags = 1u << 5;
		constexpr uint16_t kMaskHealth = 1u << 6;
		constexpr uint16_t kMaskLean = 1u << 7;
		constexpr uint16_t kMaskWeapon = 1u << 8;
		constexpr uint16_t kMaskAim = 1u << 9;
		constexpr uint16_t kMaskFull = 1u << 15;
		constexpr uint16_t kMaskAll = 0x03FF;

		const EntityState* FindEntity(const std::vector<EntityState>& List, uint16_t Id)
		{
			size_t Lo = 0;
			size_t Hi = List.size();
			while (Lo < Hi)
			{
				const size_t Mid = (Lo + Hi) / 2;
				if (List[Mid].Id == Id) { return &List[Mid]; }
				if (List[Mid].Id < Id) { Lo = Mid + 1; } else { Hi = Mid; }
			}
			return nullptr;
		}

		uint16_t ChangeMask(const EntityState& Cur, const EntityState& Base)
		{
			uint16_t M = 0;
			if (Cur.PosMm[0] != Base.PosMm[0]) { M |= kMaskPX; }
			if (Cur.PosMm[1] != Base.PosMm[1]) { M |= kMaskPY; }
			if (Cur.PosMm[2] != Base.PosMm[2]) { M |= kMaskPZ; }
			if (Cur.YawQ != Base.YawQ) { M |= kMaskYaw; }
			if (Cur.PitchQ != Base.PitchQ) { M |= kMaskPitch; }
			if (Cur.Flags != Base.Flags) { M |= kMaskFlags; }
			if (Cur.Health != Base.Health) { M |= kMaskHealth; }
			if (Cur.LeanQ != Base.LeanQ) { M |= kMaskLean; }
			if (Cur.Weapon != Base.Weapon) { M |= kMaskWeapon; }
			if (Cur.AimQ != Base.AimQ) { M |= kMaskAim; }
			return M;
		}

		void EncodeLocal(ByteWriter& W, const SnapshotHeader& H)
		{
			const MoveState& L = H.Local;
			W.F32(L.Pos.X); W.F32(L.Pos.Y); W.F32(L.Pos.Z);
			W.F32(L.Vel.X); W.F32(L.Vel.Y); W.F32(L.Vel.Z);
			W.U8(static_cast<uint8_t>((L.bOnGround ? 1 : 0) | (L.bCrouched ? 2 : 0) | (L.bProne ? 4 : 0) | ((L.Armor & 3) << 3)));
			W.F32(L.JumpTimer); W.F32(L.StanceTimer); W.F32(L.Lean); W.F32(L.Aim);
			const WeaponState& Ws = H.Weapon;
			W.U8(Ws.Active);
			for (const SlotAmmo& Sl : Ws.Slots) { W.U16(Sl.Ammo); W.U8(Sl.bChambered ? 1 : 0); W.U16(Sl.Reserve); }
			W.F32(Ws.Cooldown); W.F32(Ws.ReloadTimer); W.U8(Ws.bReloadEmpty ? 1 : 0); W.F32(Ws.EquipTimer);
			W.U8(Ws.BurstLeft); W.U8(Ws.bTriggerPrev ? 1 : 0); W.U16(Ws.ShotsFired);
			W.F32(Ws.RecoilPitch); W.F32(Ws.RecoilYaw); W.F32(Ws.RecoverTimer); W.U32(Ws.ShotCounter);
			W.U8(H.LocalHealth);
			W.U8(H.bAlive ? 1 : 0);
			W.F32(H.RespawnIn);
			W.U8(H.Reinforcements);
			W.U8(H.ReinforceProgress);
			W.U8(H.Action);
			for (const GadgetStock& G : H.Gadgets) { W.U8(G.Id); W.U8(G.Count); }
		}

		bool DecodeLocal(ByteReader& R, SnapshotHeader& H)
		{
			MoveState& L = H.Local;
			L.Pos.X = R.F32Finite(); L.Pos.Y = R.F32Finite(); L.Pos.Z = R.F32Finite();
			L.Vel.X = R.F32Finite(); L.Vel.Y = R.F32Finite(); L.Vel.Z = R.F32Finite();
			const uint8_t MF = R.U8();
			L.bOnGround = (MF & 1) != 0; L.bCrouched = (MF & 2) != 0; L.bProne = (MF & 4) != 0;
			L.Armor = static_cast<uint8_t>((MF >> 3) & 3);
			if (L.Armor > 2) { R.Fail(); return false; }
			L.JumpTimer = R.F32Finite(); L.StanceTimer = R.F32Finite(); L.Lean = R.F32Finite(); L.Aim = R.F32Finite();
			WeaponState& Ws = H.Weapon;
			Ws.Active = R.U8();
			if (Ws.Active >= kWeaponSlots + kGadgetSlots) { R.Fail(); return false; }
			for (SlotAmmo& Sl : Ws.Slots) { Sl.Ammo = R.U16(); Sl.bChambered = R.U8() != 0; Sl.Reserve = R.U16(); }
			Ws.Cooldown = R.F32Finite(); Ws.ReloadTimer = R.F32Finite(); Ws.bReloadEmpty = R.U8() != 0; Ws.EquipTimer = R.F32Finite();
			Ws.BurstLeft = R.U8(); Ws.bTriggerPrev = R.U8() != 0; Ws.ShotsFired = R.U16();
			Ws.RecoilPitch = R.F32Finite(); Ws.RecoilYaw = R.F32Finite(); Ws.RecoverTimer = R.F32Finite(); Ws.ShotCounter = R.U32();
			H.LocalHealth = R.U8();
			H.bAlive = R.U8() != 0;
			H.RespawnIn = R.F32Finite();
			H.Reinforcements = R.U8();
			H.ReinforceProgress = R.U8();
			H.Action = R.U8();
			for (GadgetStock& G : H.Gadgets) { G.Id = R.U8(); G.Count = R.U8(); }
			return R.Ok();
		}
	}

	ERejectReason RejectReasonFromU8(uint8_t V)
	{
		switch (V)
		{
		case 1: return ERejectReason::ServerFull;
		case 2: return ERejectReason::VersionMismatch;
		case 3: return ERejectReason::InvalidChallenge;
		case 4: return ERejectReason::InvalidName;
		case 5: return ERejectReason::Unauthorized;
		default: return ERejectReason::Unknown;
		}
	}

	const char* RejectReasonName(ERejectReason R)
	{
		switch (R)
		{
		case ERejectReason::ServerFull: return "server full";
		case ERejectReason::VersionMismatch: return "protocol version mismatch";
		case ERejectReason::InvalidChallenge: return "invalid challenge";
		case ERejectReason::InvalidName: return "invalid name";
		case ERejectReason::Unauthorized: return "not authorized to join";
		default: return "unknown";
		}
	}

	EDisconnectReason DisconnectReasonFromU8(uint8_t V)
	{
		switch (V)
		{
		case 1: return EDisconnectReason::ClientQuit;
		case 2: return EDisconnectReason::Timeout;
		case 3: return EDisconnectReason::Kicked;
		case 4: return EDisconnectReason::ServerShutdown;
		case 5: return EDisconnectReason::ProtocolError;
		default: return EDisconnectReason::Unknown;
		}
	}

	const char* DisconnectReasonName(EDisconnectReason R)
	{
		switch (R)
		{
		case EDisconnectReason::ClientQuit: return "quit";
		case EDisconnectReason::Timeout: return "timed out";
		case EDisconnectReason::Kicked: return "kicked";
		case EDisconnectReason::ServerShutdown: return "server shut down";
		case EDisconnectReason::ProtocolError: return "protocol error";
		default: return "unknown";
		}
	}

	// ---- Envelope -------------------------------------------------------------

	void WriteHeader(ByteWriter& W, EPacketType Type)
	{
		W.U16(kMagic);
		W.U16(kProtocolVersion);
		W.U8(static_cast<uint8_t>(Type));
	}

	bool ReadHeader(ByteReader& R, PacketHeader& Out)
	{
		if (R.U16() != kMagic) { return false; }
		Out.Version = R.U16();
		const uint8_t T = R.U8();
		if (!R.Ok() || T < 1 || T > 7) { return false; }
		Out.Type = static_cast<EPacketType>(T);
		return true;
	}

	void ConnectRequest::Encode(std::vector<uint8_t>& Out) const
	{
		ByteWriter W(Out);
		WriteHeader(W, EPacketType::ConnectRequest);
		W.U64(ClientNonce);
		W.PadTo(kHandshakePacketSize);
	}

	void Challenge::Encode(std::vector<uint8_t>& Out) const
	{
		ByteWriter W(Out);
		WriteHeader(W, EPacketType::Challenge);
		W.U64(ClientNonce);
		W.U64(Cookie);
	}

	bool Challenge::Decode(ByteReader& R)
	{
		ClientNonce = R.U64();
		Cookie = R.U64();
		return R.Ok();
	}

	void ChallengeResponse::Encode(std::vector<uint8_t>& Out) const
	{
		ByteWriter W(Out);
		WriteHeader(W, EPacketType::ChallengeResponse);
		W.U64(ClientNonce);
		W.U64(Cookie);
		W.U64(ReconnectToken);
		W.U64(JoinToken);
		W.Str8(Name, kMaxNameLen);
		W.U8(Armor);
		W.PadTo(kHandshakePacketSize);
	}

	void ConnectAccepted::Encode(std::vector<uint8_t>& Out) const
	{
		ByteWriter W(Out);
		WriteHeader(W, EPacketType::ConnectAccepted);
		W.U64(ClientNonce);
		W.U64(SessionKey);
		W.U16(PlayerId);
		W.U16(TickRate);
		W.U8(SnapshotInterval);
		W.U32(ServerTick);
		W.U64(ReconnectToken);
		W.U64(MapHash);
		W.Str8(MapName, kMaxMapNameLen);
		W.U64(WeaponsHash);
		W.U8(Loadout[0]);
		W.U8(Loadout[1]);
	}

	bool ConnectAccepted::Decode(ByteReader& R)
	{
		ClientNonce = R.U64();
		SessionKey = R.U64();
		PlayerId = R.U16();
		TickRate = R.U16();
		SnapshotInterval = R.U8();
		ServerTick = R.U32();
		ReconnectToken = R.U64();
		MapHash = R.U64();
		MapName = R.Str8(kMaxMapNameLen);
		WeaponsHash = R.U64();
		Loadout[0] = R.U8();
		Loadout[1] = R.U8();
		return R.Ok() && TickRate >= 1;
	}

	bool ConnectRejected::Decode(ByteReader& R)
	{
		ClientNonce = R.U64();
		Reason = RejectReasonFromU8(R.U8());
		return R.Ok();
	}

	void DisconnectPacket::Encode(std::vector<uint8_t>& Out) const
	{
		ByteWriter W(Out);
		WriteHeader(W, EPacketType::Disconnect);
		W.U64(SessionKey);
		W.U8(static_cast<uint8_t>(Reason));
	}

	bool DisconnectPacket::Decode(ByteReader& R)
	{
		SessionKey = R.U64();
		Reason = DisconnectReasonFromU8(R.U8());
		return R.Ok();
	}

	void DataHeader::Encode(ByteWriter& W) const
	{
		WriteHeader(W, EPacketType::Data);
		W.U64(SessionKey);
		W.U16(Seq);
		W.U16(Ack);
		W.U32(AckBits);
	}

	bool DataHeader::Decode(ByteReader& R)
	{
		SessionKey = R.U64();
		Seq = R.U16();
		Ack = R.U16();
		AckBits = R.U32();
		return R.Ok();
	}

	// ---- Acks -------------------------------------------------------------------

	AckState::AckState() : Sent(kAckWindow) {}

	void AckState::NextOutgoing(double Now, uint16_t& OutSeq, uint16_t& OutAck, uint32_t& OutAckBits)
	{
		const uint16_t S = NextSeq;
		NextSeq = static_cast<uint16_t>(NextSeq + 1);
		FSent& Rec = Sent[S % kAckWindow];
		Rec.Seq = S;
		Rec.bValid = true;
		Rec.bAcked = false;
		Rec.SentAt = Now;
		OutSeq = S;
		OutAck = RemoteLatest;
		OutAckBits = RemoteBits;
	}

	bool AckState::OnIncoming(uint16_t Seq, uint16_t Ack, uint32_t AckBits, double Now, std::vector<uint16_t>* OutAcked)
	{
		if (!bHasRemote)
		{
			bHasRemote = true;
			RemoteLatest = Seq;
			RemoteBits = 0;
		}
		else if (SeqGreater(Seq, RemoteLatest))
		{
			const uint32_t Shift = static_cast<uint16_t>(Seq - RemoteLatest);
			if (Shift >= 32)
			{
				RemoteBits = (Shift == 32) ? (1u << 31) : 0u;
			}
			else
			{
				RemoteBits = (RemoteBits << Shift) | (1u << (Shift - 1));
			}
			RemoteLatest = Seq;
		}
		else
		{
			const uint32_t Back = static_cast<uint16_t>(RemoteLatest - Seq);
			if (Back == 0 || Back > 32) { return false; }
			const uint32_t Bit = 1u << (Back - 1);
			if ((RemoteBits & Bit) != 0) { return false; }
			RemoteBits |= Bit;
		}
		// (0, 0) means "peer has received nothing yet" (see PROTOCOL.md).
		if (Ack == 0 && AckBits == 0) { return true; }
		AckOne(Ack, Now, OutAcked);
		for (uint16_t I = 0; I < 32; ++I)
		{
			if ((AckBits & (1u << I)) != 0)
			{
				AckOne(static_cast<uint16_t>(Ack - (I + 1)), Now, OutAcked);
			}
		}
		return true;
	}

	void AckState::AckOne(uint16_t Seq, double Now, std::vector<uint16_t>* OutAcked)
	{
		FSent& Rec = Sent[Seq % kAckWindow];
		if (!Rec.bValid || Rec.Seq != Seq || Rec.bAcked) { return; }
		Rec.bAcked = true;
		const double Sample = Now - Rec.SentAt > 0.0 ? Now - Rec.SentAt : 0.0;
		if (!bRttInit) { Rtt = Sample; bRttInit = true; }
		else { Rtt = (Rtt * 7.0 + Sample) / 8.0; }
		if (OutAcked != nullptr) { OutAcked->push_back(Seq); }
	}

	// ---- Reliable -----------------------------------------------------------

	ReliableReceiver::ReliableReceiver() : Buffer(kRecvWindow) {}

	bool ReliableReceiver::Read(ByteReader& R)
	{
		const uint8_t Count = R.U8();
		for (uint8_t I = 0; I < Count && R.Ok(); ++I)
		{
			const uint16_t Seq = R.U16();
			const uint8_t Type = R.U8();
			const uint16_t Len = R.U16();
			if (!R.Ok() || Len > kMaxReliablePayload || R.Remaining() < Len) { R.Fail(); return false; }
			std::vector<uint8_t> Payload(R.Cursor(), R.Cursor() + Len);
			R.Skip(Len);
			if (Seq == NextExpected)
			{
				Ready.emplace_back(Type, std::move(Payload));
				NextExpected = static_cast<uint16_t>(NextExpected + 1);
				while (true)
				{
					auto& Slot = Buffer[NextExpected % kRecvWindow];
					if (!Slot.first) { break; }
					Ready.push_back(std::move(Slot.second));
					Slot.first = false;
					NextExpected = static_cast<uint16_t>(NextExpected + 1);
				}
			}
			else if (SeqGreater(Seq, NextExpected) && static_cast<uint16_t>(Seq - NextExpected) < kRecvWindow)
			{
				auto& Slot = Buffer[Seq % kRecvWindow];
				if (!Slot.first)
				{
					Slot.first = true;
					Slot.second = std::make_pair(Type, std::move(Payload));
				}
			}
		}
		return R.Ok();
	}

	bool ReliableReceiver::Pop(uint8_t& OutType, std::vector<uint8_t>& OutPayload)
	{
		if (Ready.empty()) { return false; }
		OutType = Ready.front().first;
		OutPayload = std::move(Ready.front().second);
		Ready.pop_front();
		return true;
	}

	bool ServerMessage::Decode(uint8_t InType, const std::vector<uint8_t>& Payload)
	{
		ByteReader R(Payload.data(), Payload.size());
		Type = InType;
		switch (InType)
		{
		case MsgType::PlayerJoined:
			PlayerId = R.U16();
			Team = R.U8();
			Name = R.Str8(kMaxNameLen);
			break;
		case MsgType::PlayerLeft:
			PlayerId = R.U16();
			Reason = R.U8();
			break;
		case MsgType::PlayerConnState:
		{
			PlayerId = R.U16();
			const uint8_t S = R.U8();
			if (S > 2) { return false; }
			State = static_cast<EConnState>(S);
			break;
		}
		case MsgType::HitConfirm:
		{
			OtherId = R.U16();
			Amount = R.U8();
			const uint8_t F = R.U8();
			bHeadshot = (F & 1) != 0;
			bKilled = (F & 2) != 0;
			break;
		}
		case MsgType::Damaged:
			OtherId = R.U16();
			Amount = R.U8();
			Health = R.U8();
			FromMm[0] = R.I32(); FromMm[1] = R.I32(); FromMm[2] = R.I32();
			break;
		case MsgType::CellsDestroyed:
		{
			const uint8_t N = R.U8();
			if (N > kMaxCellsPerMessage) { return false; }
			Cells.clear();
			for (uint8_t I = 0; I < N && R.Ok(); ++I) { Cells.push_back(R.U32()); }
			break;
		}
		case MsgType::Explosion:
			FromMm[0] = R.I32(); FromMm[1] = R.I32(); FromMm[2] = R.I32();
			RadiusCm = R.U16();
			break;
		case MsgType::Flash:
			FromMm[0] = R.I32(); FromMm[1] = R.I32(); FromMm[2] = R.I32();
			Intensity = R.U8();
			break;
		case MsgType::SolidAdded:
			SolidIndex = R.U16();
			for (int32_t& Mm : MinMm) { Mm = R.I32(); }
			for (int32_t& Mm : MaxMm) { Mm = R.I32(); }
			Material = R.U8();
			bDestructible = R.U8() != 0;
			break;
		case MsgType::SolidReinforced:
			SolidIndex = R.U16();
			break;
		case MsgType::DoorState:
			SolidIndex = R.U16();
			bOpen = R.U8() != 0;
			break;
		case MsgType::MatchState:
			Match.Mode = R.U8(); Match.Phase = R.U8(); Match.Round = R.U8();
			Match.Score[0] = R.U8(); Match.Score[1] = R.U8(); Match.AttackingSquad = R.U8();
			Match.TimeLeftDs = R.U16(); Match.RoundsToWin = R.U8(); Match.DeviceSite = R.U8();
			for (int32_t& Mm : Match.DevicePosMm) { Mm = R.I32(); }
			Match.LastWinner = R.U8(); Match.LastReason = R.U8();
			break;
		case MsgType::MapReset:
			break;
		case MsgType::Spotted:
			PlayerId = R.U16();
			OtherId = R.U16();
			FromMm[0] = R.I32(); FromMm[1] = R.I32(); FromMm[2] = R.I32();
			DurationDs = R.U8();
			break;
		case MsgType::Kill:
			OtherId = R.U16();   // killer
			PlayerId = R.U16();  // victim
			WeaponId = R.U8();
			bHeadshot = R.U8() != 0;
			break;
		default:
			return false;
		}
		return R.Ok();
	}

	std::vector<uint8_t> ServerMessage::EncodePayload() const
	{
		std::vector<uint8_t> V;
		ByteWriter W(V);
		switch (Type)
		{
		case MsgType::PlayerJoined: W.U16(PlayerId); W.U8(Team); W.Str8(Name, kMaxNameLen); break;
		case MsgType::PlayerLeft: W.U16(PlayerId); W.U8(Reason); break;
		case MsgType::PlayerConnState: W.U16(PlayerId); W.U8(static_cast<uint8_t>(State)); break;
		case MsgType::HitConfirm: W.U16(OtherId); W.U8(Amount); W.U8(static_cast<uint8_t>((bHeadshot ? 1 : 0) | (bKilled ? 2 : 0))); break;
		case MsgType::Damaged: W.U16(OtherId); W.U8(Amount); W.U8(Health); W.I32(FromMm[0]); W.I32(FromMm[1]); W.I32(FromMm[2]); break;
		case MsgType::Kill: W.U16(OtherId); W.U16(PlayerId); W.U8(WeaponId); W.U8(bHeadshot ? 1 : 0); break;
		case MsgType::CellsDestroyed:
		{
			const size_t N = Cells.size() < kMaxCellsPerMessage ? Cells.size() : kMaxCellsPerMessage;
			W.U8(static_cast<uint8_t>(N));
			for (size_t I = 0; I < N; ++I) { W.U32(Cells[I]); }
			break;
		}
		case MsgType::Explosion: W.I32(FromMm[0]); W.I32(FromMm[1]); W.I32(FromMm[2]); W.U16(RadiusCm); break;
		case MsgType::Flash: W.I32(FromMm[0]); W.I32(FromMm[1]); W.I32(FromMm[2]); W.U8(Intensity); break;
		case MsgType::SolidAdded:
			W.U16(SolidIndex);
			for (int32_t Mm : MinMm) { W.I32(Mm); }
			for (int32_t Mm : MaxMm) { W.I32(Mm); }
			W.U8(Material);
			W.U8(bDestructible ? 1 : 0);
			break;
		case MsgType::SolidReinforced: W.U16(SolidIndex); break;
		case MsgType::DoorState: W.U16(SolidIndex); W.U8(bOpen ? 1 : 0); break;
		case MsgType::MatchState:
			W.U8(Match.Mode); W.U8(Match.Phase); W.U8(Match.Round); W.U8(Match.Score[0]); W.U8(Match.Score[1]);
			W.U8(Match.AttackingSquad); W.U16(Match.TimeLeftDs); W.U8(Match.RoundsToWin); W.U8(Match.DeviceSite);
			for (int32_t Mm : Match.DevicePosMm) { W.I32(Mm); }
			W.U8(Match.LastWinner); W.U8(Match.LastReason);
			break;
		case MsgType::MapReset: break;
		case MsgType::Spotted: W.U16(PlayerId); W.U16(OtherId); W.I32(FromMm[0]); W.I32(FromMm[1]); W.I32(FromMm[2]); W.U8(DurationDs); break;
		default: break;
		}
		return V;
	}

	// ---- Input ----------------------------------------------------------------

	void InputBundle::Encode(ByteWriter& W) const
	{
		W.U32(AckSnapshotTick);
		W.U32(NewestSeq);
		W.U8(Count);
		for (uint8_t I = 0; I < Count; ++I)
		{
			W.U16(Cmds[I].Buttons);
			W.U16(Cmds[I].YawQ);
			W.I16(Cmds[I].PitchQ);
			W.U8(Cmds[I].Slot);
			W.U32(RenderTimes[I]);
		}
	}

	bool InputBundle::Decode(ByteReader& R)
	{
		AckSnapshotTick = R.U32();
		NewestSeq = R.U32();
		Count = R.U8();
		if (!R.Ok() || Count == 0 || Count > kMaxInputRedundancy || Count > NewestSeq) { R.Fail(); return false; }
		for (uint8_t I = 0; I < Count; ++I)
		{
			Cmds[I].Buttons = R.U16();
			Cmds[I].YawQ = R.U16();
			Cmds[I].PitchQ = R.I16();
			Cmds[I].Slot = R.U8();
			RenderTimes[I] = R.U32();
		}
		return R.Ok();
	}

	// ---- Snapshot -------------------------------------------------------------

	int32_t QuantizeMm(float V)
	{
		return static_cast<int32_t>(std::lround(V * 1000.0f));
	}

	float DequantizeMm(int32_t V)
	{
		return static_cast<float>(V) * 0.001f;
	}

	Vec3 EntityState::Position() const
	{
		return Vec3(DequantizeMm(PosMm[0]), DequantizeMm(PosMm[1]), DequantizeMm(PosMm[2]));
	}

	void EncodeSnapshot(ByteWriter& W, const SnapshotHeader& H, const std::vector<EntityState>& Entities, const std::vector<EntityState>* Baseline,
		const std::vector<ShotEvent>& Shots, const std::vector<GadgetEntityState>* Gadgets)
	{
		W.U32(H.ServerTick);
		W.U32(H.BaselineTick);
		W.U32(H.LastCmdSeq);
		W.I8(H.InputBufferDepth);
		EncodeLocal(W, H);
		const size_t N = Entities.size() < kMaxEntitiesPerSnapshot ? Entities.size() : kMaxEntitiesPerSnapshot;
		W.U8(static_cast<uint8_t>(N));
		for (size_t I = 0; I < N; ++I)
		{
			const EntityState& E = Entities[I];
			const EntityState* Base = Baseline != nullptr ? FindEntity(*Baseline, E.Id) : nullptr;
			const uint16_t Mask = Base != nullptr ? ChangeMask(E, *Base) : static_cast<uint16_t>(kMaskFull | kMaskAll);
			W.U16(E.Id);
			W.U16(Mask);
			if (Mask & kMaskPX) { W.I32(E.PosMm[0]); }
			if (Mask & kMaskPY) { W.I32(E.PosMm[1]); }
			if (Mask & kMaskPZ) { W.I32(E.PosMm[2]); }
			if (Mask & kMaskYaw) { W.U16(E.YawQ); }
			if (Mask & kMaskPitch) { W.I16(E.PitchQ); }
			if (Mask & kMaskFlags) { W.U8(E.Flags); }
			if (Mask & kMaskHealth) { W.U8(E.Health); }
			if (Mask & kMaskLean) { W.I8(E.LeanQ); }
			if (Mask & kMaskWeapon) { W.U8(E.Weapon); }
			if (Mask & kMaskAim) { W.U8(E.AimQ); }
		}
		const size_t NS = Shots.size() < kMaxShotEvents ? Shots.size() : kMaxShotEvents;
		W.U8(static_cast<uint8_t>(NS));
		for (size_t I = 0; I < NS; ++I)
		{
			const ShotEvent& S = Shots[I];
			W.U16(S.Shooter);
			W.U8(S.Weapon);
			W.I32(S.EndMm[0]); W.I32(S.EndMm[1]); W.I32(S.EndMm[2]);
			W.U8(S.SurfaceKind);
		}
		const size_t NG = Gadgets == nullptr ? 0 : (Gadgets->size() < kMaxGadgetEntities ? Gadgets->size() : kMaxGadgetEntities);
		W.U8(static_cast<uint8_t>(NG));
		for (size_t I = 0; I < NG; ++I)
		{
			const GadgetEntityState& G = (*Gadgets)[I];
			W.U16(G.Id); W.U8(G.Gadget); W.U8(G.Phase); W.U16(G.Owner);
			W.I16(G.PosCm[0]); W.I16(G.PosCm[1]); W.I16(G.PosCm[2]);
		}
	}

	bool PeekSnapshotBaselineTick(const uint8_t* Data, size_t Len, uint32_t& OutBaseline)
	{
		ByteReader R(Data, Len);
		R.U32();
		OutBaseline = R.U32();
		return R.Ok();
	}

	bool DecodeSnapshot(ByteReader& R, const std::vector<EntityState>* Baseline, SnapshotHeader& H, std::vector<EntityState>& Out,
		std::vector<ShotEvent>& OutShots, std::vector<GadgetEntityState>* OutGadgets)
	{
		Out.clear();
		OutShots.clear();
		if (OutGadgets != nullptr) { OutGadgets->clear(); }
		H.ServerTick = R.U32();
		H.BaselineTick = R.U32();
		H.LastCmdSeq = R.U32();
		H.InputBufferDepth = R.I8();
		if (H.BaselineTick != 0 && Baseline == nullptr) { return false; }
		const std::vector<EntityState>* Base = H.BaselineTick != 0 ? Baseline : nullptr;
		if (!DecodeLocal(R, H)) { return false; }
		const uint8_t N = R.U8();
		if (!R.Ok() || N > kMaxEntitiesPerSnapshot) { return false; }
		int LastId = -1;
		for (uint8_t I = 0; I < N; ++I)
		{
			const uint16_t Id = R.U16();
			if (!R.Ok() || static_cast<int>(Id) <= LastId) { return false; }
			LastId = Id;
			uint16_t Mask = R.U16();
			EntityState E;
			if (Mask & kMaskFull)
			{
				E.Id = Id;
				Mask = kMaskAll;
			}
			else
			{
				const EntityState* B = Base != nullptr ? FindEntity(*Base, Id) : nullptr;
				if (B == nullptr) { return false; }
				E = *B;
			}
			if (Mask & kMaskPX) { E.PosMm[0] = R.I32(); }
			if (Mask & kMaskPY) { E.PosMm[1] = R.I32(); }
			if (Mask & kMaskPZ) { E.PosMm[2] = R.I32(); }
			if (Mask & kMaskYaw) { E.YawQ = R.U16(); }
			if (Mask & kMaskPitch) { E.PitchQ = R.I16(); }
			if (Mask & kMaskFlags) { E.Flags = R.U8(); }
			if (Mask & kMaskHealth) { E.Health = R.U8(); }
			if (Mask & kMaskLean) { E.LeanQ = R.I8(); }
			if (Mask & kMaskWeapon) { E.Weapon = R.U8(); }
			if (Mask & kMaskAim) { E.AimQ = R.U8(); }
			Out.push_back(E);
		}
		const uint8_t NS = R.U8();
		if (!R.Ok() || NS > kMaxShotEvents) { return false; }
		for (uint8_t I = 0; I < NS; ++I)
		{
			ShotEvent S;
			S.Shooter = R.U16();
			S.Weapon = R.U8();
			S.EndMm[0] = R.I32(); S.EndMm[1] = R.I32(); S.EndMm[2] = R.I32();
			S.SurfaceKind = R.U8();
			OutShots.push_back(S);
		}
		const uint8_t NG = R.U8();
		if (!R.Ok() || NG > kMaxGadgetEntities) { return false; }
		for (uint8_t I = 0; I < NG; ++I)
		{
			GadgetEntityState G;
			G.Id = R.U16(); G.Gadget = R.U8(); G.Phase = R.U8(); G.Owner = R.U16();
			G.PosCm[0] = R.I16(); G.PosCm[1] = R.I16(); G.PosCm[2] = R.I16();
			if (OutGadgets != nullptr) { OutGadgets->push_back(G); }
		}
		return R.Ok();
	}
}
