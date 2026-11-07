// Copyright TacticalFPS project. Engine-independent NetCore.
#include "NetCore/TfpsClientCore.h"

#include <cmath>
#include <random>

namespace tfps
{
	namespace
	{
		constexpr double kHandshakeResend = 0.25;
		constexpr double kHandshakeTimeout = 10.0;
		constexpr size_t kSnapshotRing = 32;
		constexpr size_t kCmdHistory = 256;
		constexpr size_t kRemoteSampleKeep = 128;
		constexpr int kMaxTicksPerUpdate = 8;

		uint64_t RandomU64()
		{
			std::random_device Rd;
			const uint64_t A = (static_cast<uint64_t>(Rd()) << 32) ^ static_cast<uint64_t>(Rd());
			std::mt19937_64 Gen(A ^ static_cast<uint64_t>(std::random_device()()));
			uint64_t V = 0;
			while (V == 0) { V = Gen(); }
			return V;
		}
	}

	ClientCore::ClientCore(const ClientConfig& InConfig, const MapDef& Map, ITransport& InTransport, double Now)
		: Config(InConfig)
		, Transport(InTransport)
		, Collision(CollisionWorld::FromMap(Map))
		, LocalMapHash(Map.GeometryHash())
		, Nonce(RandomU64())
		, HandshakeStarted(Now)
		, LastRecv(Now)
		, History(kCmdHistory)
		, Ring(kSnapshotRing)
		, RecvBuf(2048)
	{
		SendBuf.reserve(kMaxPacketSize);
		SessionReconnectToken = Config.ReconnectToken;
	}

	bool ClientCore::PollEvent(ClientEvent& Out)
	{
		if (Events.empty()) { return false; }
		Out = std::move(Events.front());
		Events.pop_front();
		return true;
	}

	void ClientCore::SetDisconnected(EDisconnectCause InCause)
	{
		if (CurPhase == EClientPhase::Disconnected) { return; }
		CurPhase = EClientPhase::Disconnected;
		Cause = InCause;
		ClientEvent E;
		E.Type = ClientEvent::EType::Disconnected;
		E.Cause = InCause;
		E.RejectReason = RejectReason;
		Events.push_back(E);
	}

	void ClientCore::Disconnect()
	{
		if (CurPhase == EClientPhase::Connected)
		{
			SendBuf.clear();
			DisconnectPacket P;
			P.SessionKey = SessionKey;
			P.Reason = EDisconnectReason::ClientQuit;
			P.Encode(SendBuf);
			for (int I = 0; I < 3; ++I) { Transport.Send(SendBuf.data(), SendBuf.size()); }
		}
		SetDisconnected(EDisconnectCause::Local);
	}

	void ClientCore::SendCurrent()
	{
		if (Transport.Send(SendBuf.data(), SendBuf.size()))
		{
			++NetStats.PacketsSent;
			NetStats.BytesSent += SendBuf.size();
		}
	}

	void ClientCore::SendHandshake(double Now)
	{
		if (LastHandshakeSend >= 0.0 && Now - LastHandshakeSend < kHandshakeResend) { return; }
		LastHandshakeSend = Now;
		SendBuf.clear();
		if (CurPhase == EClientPhase::Requesting)
		{
			ConnectRequest P;
			P.ClientNonce = Nonce;
			P.Encode(SendBuf);
		}
		else
		{
			ChallengeResponse P;
			P.ClientNonce = Nonce;
			P.Cookie = Cookie;
			P.ReconnectToken = Config.ReconnectToken;
			P.JoinToken = Config.JoinToken;
			P.Name = Config.Name;
			P.Encode(SendBuf);
		}
		SendCurrent();
	}

	void ClientCore::Update(double Now)
	{
		Receive(Now);
		switch (CurPhase)
		{
		case EClientPhase::Requesting:
		case EClientPhase::Responding:
			if (Now - HandshakeStarted > kHandshakeTimeout)
			{
				SetDisconnected(EDisconnectCause::HandshakeTimeout);
			}
			else
			{
				SendHandshake(Now);
			}
			break;
		case EClientPhase::Connected:
			if (Now - LastRecv > Config.ConnectionTimeout)
			{
				SetDisconnected(EDisconnectCause::Timeout);
				return;
			}
			AdvanceTicks(Now);
			UpdateInterpolation();
			break;
		case EClientPhase::Disconnected:
			break;
		}
	}

	void ClientCore::AdvanceTicks(double Now)
	{
		const double Elapsed = LastFrame < 0.0 ? static_cast<double>(Dt) : Now - LastFrame;
		LastFrame = Now;
		ServerTickEstimate += Elapsed * ServerTickRate;
		TickAccum += Elapsed * TimeScale;
		const double TickLen = static_cast<double>(Dt);
		int Ran = 0;
		while (TickAccum >= TickLen && Ran < kMaxTicksPerUpdate)
		{
			TickAccum -= TickLen;
			RunTick(Now);
			++Ran;
		}
		if (Ran == kMaxTicksPerUpdate) { TickAccum = 0.0; }
	}

	float ClientCore::TickAlpha() const
	{
		const double A = TickAccum / static_cast<double>(Dt);
		return static_cast<float>(A < 0.0 ? 0.0 : (A > 1.0 ? 1.0 : A));
	}

	void ClientCore::RunTick(double Now)
	{
		++CmdSeq;
		const MoveInput Input = CurrentInput;
		PrevPredictedState = PredictedState;
		Simulate(PredictedState, Input, Dt, Collision);
		FCmdRecord& Rec = History[CmdSeq % kCmdHistory];
		Rec.Seq = CmdSeq;
		Rec.Input = Input;
		Rec.After = PredictedState;
		SendInput(Now);
	}

	void ClientCore::SendInput(double Now)
	{
		uint32_t Count = Config.InputRedundancy;
		if (Count < 1) { Count = 1; }
		if (Count > kMaxInputRedundancy) { Count = kMaxInputRedundancy; }
		if (Count > CmdSeq) { Count = CmdSeq; }
		InputBundle Bundle;
		Bundle.AckSnapshotTick = LatestTick;
		Bundle.NewestSeq = CmdSeq;
		Bundle.Count = static_cast<uint8_t>(Count);
		for (uint32_t I = 0; I < Count; ++I)
		{
			Bundle.Cmds[I] = History[(CmdSeq - I) % kCmdHistory].Input;
		}
		SendBuf.clear();
		ByteWriter W(SendBuf);
		DataHeader H;
		H.SessionKey = SessionKey;
		Acks.NextOutgoing(Now, H.Seq, H.Ack, H.AckBits);
		H.Encode(W);
		WriteEmptyReliable(W);
		W.U8(static_cast<uint8_t>(EBodyKind::Input));
		Bundle.Encode(W);
		SendCurrent();
	}

	void ClientCore::Receive(double Now)
	{
		while (true)
		{
			const size_t N = Transport.Receive(RecvBuf.data(), RecvBuf.size());
			if (N == 0) { break; }
			++NetStats.PacketsReceived;
			NetStats.BytesReceived += N;
			Process(RecvBuf.data(), N, Now);
		}
	}

	void ClientCore::Process(const uint8_t* Data, size_t Len, double Now)
	{
		ByteReader R(Data, Len);
		PacketHeader Hdr;
		if (!ReadHeader(R, Hdr)) { return; }
		if (Hdr.Type == EPacketType::Challenge && CurPhase == EClientPhase::Requesting)
		{
			Challenge C;
			if (C.Decode(R) && C.ClientNonce == Nonce)
			{
				Cookie = C.Cookie;
				CurPhase = EClientPhase::Responding;
				LastHandshakeSend = -1.0;
				SendHandshake(Now);
			}
		}
		else if (Hdr.Type == EPacketType::ConnectAccepted && CurPhase == EClientPhase::Responding)
		{
			ConnectAccepted A;
			if (!A.Decode(R) || A.ClientNonce != Nonce) { return; }
			ServerHash = A.MapHash;
			if (A.MapHash != LocalMapHash)
			{
				SendBuf.clear();
				DisconnectPacket P;
				P.SessionKey = A.SessionKey;
				P.Reason = EDisconnectReason::ProtocolError;
				P.Encode(SendBuf);
				Transport.Send(SendBuf.data(), SendBuf.size());
				SetDisconnected(EDisconnectCause::MapMismatch);
				return;
			}
			SessionKey = A.SessionKey;
			LocalPlayerId = A.PlayerId;
			ServerTickRate = A.TickRate;
			SnapshotInterval = A.SnapshotInterval < 1 ? 1 : A.SnapshotInterval;
			Dt = TickDt(A.TickRate);
			SessionReconnectToken = A.ReconnectToken;
			ServerTickEstimate = A.ServerTick;
			LastRecv = Now;
			CurPhase = EClientPhase::Connected;
			ClientEvent E;
			E.Type = ClientEvent::EType::Connected;
			E.PlayerId = A.PlayerId;
			Events.push_back(E);
		}
		else if (Hdr.Type == EPacketType::ConnectRejected && (CurPhase == EClientPhase::Requesting || CurPhase == EClientPhase::Responding))
		{
			ConnectRejected Rj;
			if (Rj.Decode(R) && Rj.ClientNonce == Nonce)
			{
				RejectReason = Rj.Reason;
				SetDisconnected(EDisconnectCause::Rejected);
			}
		}
		else if (Hdr.Type == EPacketType::Data && CurPhase == EClientPhase::Connected)
		{
			ProcessData(R, Now);
		}
		else if (Hdr.Type == EPacketType::Disconnect && CurPhase == EClientPhase::Connected)
		{
			DisconnectPacket D;
			if (D.Decode(R) && D.SessionKey == SessionKey)
			{
				SetDisconnected(EDisconnectCause::ServerDisconnect);
			}
		}
	}

	void ClientCore::ProcessData(ByteReader& R, double Now)
	{
		DataHeader H;
		if (!H.Decode(R) || H.SessionKey != SessionKey) { return; }
		AckedScratch.clear();
		if (!Acks.OnIncoming(H.Seq, H.Ack, H.AckBits, Now, &AckedScratch)) { return; }
		LastRecv = Now;
		NetStats.RttMs = static_cast<float>(Acks.RttSeconds() * 1000.0);
		if (!RelRx.Read(R)) { return; }
		uint8_t Type = 0;
		std::vector<uint8_t> Payload;
		while (RelRx.Pop(Type, Payload))
		{
			ServerMessage M;
			if (M.Decode(Type, Payload)) { OnMessage(M); }
		}
		const uint8_t Kind = R.U8();
		if (R.Ok() && Kind == static_cast<uint8_t>(EBodyKind::Snapshot))
		{
			OnSnapshot(R);
		}
	}

	void ClientCore::OnMessage(const ServerMessage& M)
	{
		ClientEvent E;
		E.PlayerId = M.PlayerId;
		switch (M.Type)
		{
		case MsgType::PlayerJoined:
		{
			if (M.PlayerId != LocalPlayerId)
			{
				RemotePlayer& P = RemotePlayers[M.PlayerId];
				P.Id = M.PlayerId;
				P.Name = M.Name;
				P.Team = M.Team;
				P.ConnState = EConnState::Connected;
			}
			E.Type = ClientEvent::EType::PlayerJoined;
			E.Team = M.Team;
			E.Name = M.Name;
			break;
		}
		case MsgType::PlayerLeft:
			RemotePlayers.erase(M.PlayerId);
			E.Type = ClientEvent::EType::PlayerLeft;
			E.LeftReason = DisconnectReasonFromU8(M.Reason);
			break;
		default:
		{
			auto It = RemotePlayers.find(M.PlayerId);
			if (It != RemotePlayers.end()) { It->second.ConnState = M.State; }
			E.Type = ClientEvent::EType::PlayerConnState;
			E.ConnState = M.State;
			break;
		}
		}
		Events.push_back(E);
	}

	void ClientCore::OnSnapshot(ByteReader& R)
	{
		uint32_t BaseTick = 0;
		if (!PeekSnapshotBaselineTick(R.Cursor(), R.Remaining(), BaseTick)) { return; }
		const std::vector<EntityState>* Base = nullptr;
		if (BaseTick != 0)
		{
			for (const auto& Slot : Ring)
			{
				if (Slot.first == BaseTick) { Base = &Slot.second; break; }
			}
			if (Base == nullptr) { ++NetStats.SnapshotsDroppedNoBaseline; return; }
		}
		SnapshotHeader H;
		if (!DecodeSnapshot(R, Base, H, DecodeBuf)) { return; }
		++NetStats.SnapshotsReceived;
		if (H.BaselineTick != 0) { ++NetStats.DeltaSnapshots; }
		if (H.ServerTick <= LatestTick) { return; }

		auto& Slot = Ring[RingCursor];
		Slot.first = H.ServerTick;
		Slot.second = DecodeBuf;
		RingCursor = (RingCursor + 1) % kSnapshotRing;
		LatestTick = H.ServerTick;
		if (static_cast<double>(H.ServerTick) > ServerTickEstimate) { ServerTickEstimate = H.ServerTick; }

		for (auto& KV : RemotePlayers) { KV.second.bPresent = false; }
		for (const EntityState& E : DecodeBuf)
		{
			auto It = RemotePlayers.find(E.Id);
			if (It == RemotePlayers.end()) { continue; }
			It->second.bPresent = true;
			It->second.Samples.emplace_back(H.ServerTick, E);
			while (It->second.Samples.size() > kRemoteSampleKeep) { It->second.Samples.pop_front(); }
		}

		AuthState = H.Local;
		LocalHealth = H.LocalHealth;
		NetStats.ServerInputDepth = H.InputBufferDepth;
		Reconcile(H.LastCmdSeq, H.Local);
		AdjustTimeScale(H.InputBufferDepth);
	}

	void ClientCore::Reconcile(uint32_t AckedSeq, const MoveState& Server)
	{
		if (AckedSeq > CmdSeq)
		{
			return;
		}
		if (AckedSeq == 0)
		{
			// Server has not simulated any of our commands yet: Server is the
			// spawn state. Re-predict every pending command from it so their
			// stored predictions are not based on the pre-spawn origin.
			MoveState State = Server;
			PrevPredictedState = Server;
			for (uint32_t Seq = 1; Seq <= CmdSeq; ++Seq)
			{
				FCmdRecord& Rec = History[Seq % kCmdHistory];
				if (Rec.Seq != Seq) { continue; }
				PrevPredictedState = State;
				Simulate(State, Rec.Input, Dt, Collision);
				Rec.After = State;
			}
			PredictedState = State;
			bHaveAuthoritative = true;
			return;
		}
		const FCmdRecord Acked = History[AckedSeq % kCmdHistory];
		const bool bHadPrediction = Acked.Seq == AckedSeq && bHaveAuthoritative;
		const bool bFirst = !bHaveAuthoritative;
		bHaveAuthoritative = true;
		MoveState State = Server;
		for (uint32_t Seq = AckedSeq + 1; Seq <= CmdSeq; ++Seq)
		{
			FCmdRecord& Rec = History[Seq % kCmdHistory];
			if (Rec.Seq != Seq) { break; }
			if (Seq == CmdSeq) { PrevPredictedState = State; }
			Simulate(State, Rec.Input, Dt, Collision);
			Rec.After = State;
		}
		if (bHadPrediction)
		{
			const float Err = Distance(Acked.After.Pos, Server.Pos);
			NetStats.LastCorrectionM = Err;
			if (Err > 1e-4f || Acked.After.Vel != Server.Vel)
			{
				++NetStats.Corrections;
				if (Err > NetStats.MaxCorrectionM) { NetStats.MaxCorrectionM = Err; }
			}
		}
		if (bFirst || AckedSeq == CmdSeq) { PrevPredictedState = State; }
		PredictedState = State;
	}

	void ClientCore::AdjustTimeScale(int8_t Depth)
	{
		if (Depth < 0) { TimeScale = 1.06f; }
		else if (Depth == 0) { TimeScale = 1.03f; }
		else if (Depth <= 3) { TimeScale = 1.0f; }
		else if (Depth <= 6) { TimeScale = 0.97f; }
		else { TimeScale = 0.92f; }
		NetStats.TimeScale = TimeScale;
	}

	double ClientCore::RenderTick() const
	{
		double Delay = static_cast<double>(Config.InterpDelay) * ServerTickRate;
		const double MinDelay = 2.0 * SnapshotInterval;
		if (Delay < MinDelay) { Delay = MinDelay; }
		return ServerTickEstimate - Delay;
	}

	void ClientCore::UpdateInterpolation()
	{
		const double Rt = RenderTick();
		for (auto& KV : RemotePlayers)
		{
			RemotePlayer& P = KV.second;
			if (P.Samples.empty()) { continue; }
			auto A = P.Samples.front();
			auto B = A;
			for (const auto& S : P.Samples)
			{
				if (static_cast<double>(S.first) <= Rt) { A = S; B = S; }
				else { B = S; break; }
			}
			float T = 0.0f;
			if (B.first > A.first)
			{
				double TT = (Rt - A.first) / static_cast<double>(B.first - A.first);
				TT = TT < 0.0 ? 0.0 : (TT > 1.0 ? 1.0 : TT);
				T = static_cast<float>(TT);
			}
			const Vec3 PA = A.second.Position();
			const Vec3 PB = B.second.Position();
			P.RenderPos = Vec3(PA.X + (PB.X - PA.X) * T, PA.Y + (PB.Y - PA.Y) * T, PA.Z + (PB.Z - PA.Z) * T);
			const float YA = DequantizeYawDeg(A.second.YawQ);
			const float YB = DequantizeYawDeg(B.second.YawQ);
			float DY = YB - YA;
			if (DY > 180.0f) { DY -= 360.0f; }
			else if (DY < -180.0f) { DY += 360.0f; }
			float Yaw = YA + DY * T;
			if (Yaw < 0.0f) { Yaw += 360.0f; }
			if (Yaw >= 360.0f) { Yaw -= 360.0f; }
			P.RenderYawDeg = Yaw;
			const float PitA = DequantizePitchDeg(A.second.PitchQ);
			const float PitB = DequantizePitchDeg(B.second.PitchQ);
			P.RenderPitchDeg = PitA + (PitB - PitA) * T;
			P.Flags = B.second.Flags;
			P.Health = B.second.Health;
		}
	}
}
