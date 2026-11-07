// C++ protocol vs Rust golden byte vectors, plus local round-trip tests.
#include "TestFramework.h"
#include "NetCore/TfpsProtocol.h"

#include <fstream>
#include <map>
#include <sstream>

using namespace tfps;

namespace
{
	std::map<std::string, std::vector<uint8_t>> LoadVectors()
	{
		std::map<std::string, std::vector<uint8_t>> Out;
		std::ifstream F(std::string(TFPS_REPO_ROOT) + "/Shared/Definitions/conformance/protocol_vectors.txt");
		std::string Name, Hex;
		while (F >> Name >> Hex)
		{
			std::vector<uint8_t> Bytes;
			for (size_t I = 0; I + 1 < Hex.size(); I += 2) { Bytes.push_back(static_cast<uint8_t>(std::stoul(Hex.substr(I, 2), nullptr, 16))); }
			Out[Name] = Bytes;
		}
		return Out;
	}

	MoveState GoldenLocal()
	{
		MoveState L;
		L.Pos = Vec3(-20.0f, 1.5f, 0.1f);
		L.Vel = Vec3(3.4f, -0.25f, 0.0f);
		L.bOnGround = true;
		L.JumpTimer = 0.4375f;
		return L;
	}

	std::vector<EntityState> GoldenBase()
	{
		EntityState A; A.Id = 2; A.PosMm[0] = 20000; A.PosMm[1] = -6000; A.PosMm[2] = 0; A.YawQ = 32768; A.PitchQ = 0; A.Flags = 9; A.Health = 100;
		EntityState B; B.Id = 5; B.PosMm[0] = -1; B.PosMm[1] = 2; B.PosMm[2] = 3300; B.YawQ = 1; B.PitchQ = -32767; B.Flags = 11; B.Health = 42;
		return {A, B};
	}
}

TFPS_TEST(GoldenHandshakeBytesMatchRust)
{
	auto V = LoadVectors();
	TFPS_REQUIRE(V.size() == 9);
	std::vector<uint8_t> B;
	ConnectRequest CR; CR.ClientNonce = 0x0102030405060708ull; CR.Encode(B);
	TFPS_EXPECT(B == V["connect_request"]);
	B.clear();
	Challenge Ch; Ch.ClientNonce = 0x0102030405060708ull; Ch.Cookie = 0xAABBCCDDEEFF0011ull; Ch.Encode(B);
	TFPS_EXPECT(B == V["challenge"]);
	B.clear();
	ChallengeResponse Resp; Resp.ClientNonce = 0x0102030405060708ull; Resp.Cookie = 0xAABBCCDDEEFF0011ull;
	Resp.ReconnectToken = 0x1111222233334444ull; Resp.JoinToken = 0x5555; Resp.Name = "Operator-7"; Resp.Encode(B);
	TFPS_EXPECT(B == V["challenge_response"]);
	B.clear();
	DisconnectPacket D; D.SessionKey = 0xDEADBEEFCAFEF00Dull; D.Reason = EDisconnectReason::ClientQuit; D.Encode(B);
	TFPS_EXPECT(B == V["disconnect"]);

	// Decode server packets produced by Rust.
	{
		const auto& P = V["connect_accepted"];
		ByteReader R(P.data(), P.size());
		PacketHeader H;
		TFPS_REQUIRE(ReadHeader(R, H) && H.Type == EPacketType::ConnectAccepted && H.Version == kProtocolVersion);
		ConnectAccepted A;
		TFPS_REQUIRE(A.Decode(R));
		TFPS_EXPECT(A.SessionKey == 0xDEADBEEFCAFEF00Dull && A.PlayerId == 3 && A.TickRate == 128 && A.SnapshotInterval == 2);
		TFPS_EXPECT(A.ServerTick == 123456 && A.ReconnectToken == 0x777788889999AAAAull && A.MapHash == 0x5567bc867d6907e6ull && A.MapName == "test_arena");
		std::vector<uint8_t> Re;
		A.Encode(Re);
		TFPS_EXPECT(Re == P);
	}
	{
		const auto& P = V["connect_rejected"];
		ByteReader R(P.data(), P.size());
		PacketHeader H;
		TFPS_REQUIRE(ReadHeader(R, H));
		ConnectRejected Rj;
		TFPS_REQUIRE(Rj.Decode(R));
		TFPS_EXPECT(Rj.ClientNonce == 9 && Rj.Reason == ERejectReason::ServerFull);
	}
}

TFPS_TEST(GoldenDataPacketsMatchRust)
{
	auto V = LoadVectors();
	// Client input packet.
	std::vector<uint8_t> B;
	{
		ByteWriter W(B);
		DataHeader H; H.SessionKey = 0xDEADBEEFCAFEF00Dull; H.Seq = 65535; H.Ack = 17; H.AckBits = 0x80000001u;
		H.Encode(W);
		WriteEmptyReliable(W);
		W.U8(static_cast<uint8_t>(EBodyKind::Input));
		InputBundle IB; IB.AckSnapshotTick = 1000; IB.NewestSeq = 50; IB.Count = 3;
		IB.Cmds[0] = {0x0041, 16384, -1200};
		IB.Cmds[1] = {0x0001, 16000, -1100};
		IB.Cmds[2] = {0x0000, 15000, 32767};
		IB.Encode(W);
	}
	TFPS_EXPECT(B == V["data_input"]);

	// Server snapshot packet: decode reliable messages and the full snapshot.
	const auto& P = V["data_snapshot_full"];
	ByteReader R(P.data(), P.size());
	PacketHeader PH;
	TFPS_REQUIRE(ReadHeader(R, PH) && PH.Type == EPacketType::Data);
	DataHeader DH;
	TFPS_REQUIRE(DH.Decode(R));
	TFPS_EXPECT(DH.Seq == 7 && DH.Ack == 49 && DH.AckBits == 0xFFFFFFFFu);
	ReliableReceiver Rx;
	TFPS_REQUIRE(Rx.Read(R));
	uint8_t Type = 0;
	std::vector<uint8_t> Payload;
	ServerMessage M;
	TFPS_REQUIRE(Rx.Pop(Type, Payload) && M.Decode(Type, Payload));
	TFPS_EXPECT(M.Type == MsgType::PlayerJoined && M.PlayerId == 2 && M.Team == 1 && M.Name == "Bravo");
	TFPS_REQUIRE(Rx.Pop(Type, Payload) && M.Decode(Type, Payload));
	TFPS_EXPECT(M.Type == MsgType::PlayerConnState && M.PlayerId == 5 && M.State == EConnState::Interrupted);
	TFPS_REQUIRE(Rx.Pop(Type, Payload) && M.Decode(Type, Payload));
	TFPS_EXPECT(M.Type == MsgType::PlayerLeft && M.PlayerId == 9 && M.Reason == 2);
	TFPS_EXPECT(!Rx.Pop(Type, Payload));
	TFPS_REQUIRE(R.U8() == static_cast<uint8_t>(EBodyKind::Snapshot));
	SnapshotHeader SH;
	std::vector<EntityState> Ents;
	TFPS_REQUIRE(DecodeSnapshot(R, nullptr, SH, Ents));
	TFPS_EXPECT(R.Remaining() == 0);
	TFPS_EXPECT(SH.ServerTick == 1000 && SH.BaselineTick == 0 && SH.LastCmdSeq == 48 && SH.InputBufferDepth == 2 && SH.LocalHealth == 100);
	TFPS_EXPECT(SH.Local == GoldenLocal());
	TFPS_EXPECT(Ents == GoldenBase());

	// Re-encode the snapshot body and compare with the Rust bytes.
	std::vector<uint8_t> Body;
	ByteWriter BW(Body);
	EncodeSnapshot(BW, SH, Ents, nullptr);
	const std::vector<uint8_t> RustBody(P.end() - static_cast<long>(Body.size()), P.end());
	TFPS_EXPECT(Body == RustBody);
}

TFPS_TEST(GoldenDeltaSnapshotMatchesRust)
{
	auto V = LoadVectors();
	const auto& P = V["snapshot_delta_body"];
	const std::vector<EntityState> Base = GoldenBase();
	uint32_t BT = 0;
	TFPS_REQUIRE(PeekSnapshotBaselineTick(P.data(), P.size(), BT) && BT == 1000);
	ByteReader R(P.data(), P.size());
	SnapshotHeader SH;
	std::vector<EntityState> Ents;
	TFPS_REQUIRE(DecodeSnapshot(R, &Base, SH, Ents));
	TFPS_EXPECT(SH.ServerTick == 1002 && SH.InputBufferDepth == -1 && SH.LocalHealth == 99);
	TFPS_REQUIRE(Ents.size() == 2);
	TFPS_EXPECT(Ents[0].Id == 2 && Ents[0].PosMm[0] == 19950 && Ents[0].YawQ == 32000 && Ents[0].Health == 100);
	TFPS_EXPECT(Ents[1].Id == 8 && Ents[1].PosMm[2] == 1 && Ents[1].PitchQ == 3);
	// Without the baseline it must be refused.
	ByteReader R2(P.data(), P.size());
	SnapshotHeader SH2;
	std::vector<EntityState> Ents2;
	TFPS_EXPECT(!DecodeSnapshot(R2, nullptr, SH2, Ents2));
	// Re-encode identically.
	std::vector<uint8_t> Out;
	ByteWriter W(Out);
	EncodeSnapshot(W, SH, Ents, &Base);
	TFPS_EXPECT(Out == P);
}

TFPS_TEST(AckStateHandlesLossAndNoFalseAck)
{
	AckState A, B;
	std::vector<uint16_t> Acked;
	uint16_t S, Ak; uint32_t Bits;
	A.NextOutgoing(0.0, S, Ak, Bits); // lost
	B.NextOutgoing(0.0, S, Ak, Bits);
	TFPS_EXPECT(Ak == 0 && Bits == 0);
	A.OnIncoming(S, Ak, Bits, 0.01, &Acked);
	TFPS_EXPECT(Acked.empty());
	// Duplicate / old packets are rejected.
	AckState C;
	TFPS_EXPECT(C.OnIncoming(10, 0, 0, 0.0, nullptr));
	TFPS_EXPECT(C.OnIncoming(12, 0, 0, 0.0, nullptr));
	TFPS_EXPECT(C.OnIncoming(11, 0, 0, 0.0, nullptr));
	TFPS_EXPECT(!C.OnIncoming(11, 0, 0, 0.0, nullptr));
	TFPS_EXPECT(!C.OnIncoming(static_cast<uint16_t>(12 - 40), 0, 0, 0.0, nullptr));
	TFPS_EXPECT(SeqGreater(0, 65535) && !SeqGreater(65535, 0));
}

TFPS_TEST(MalformedInputIsRejectedWithoutCrash)
{
	const uint8_t Junk[] = {0x54, 0x46, 1, 0, 6, 1, 2, 3};
	ByteReader R(Junk, sizeof(Junk));
	PacketHeader H;
	TFPS_EXPECT(ReadHeader(R, H));
	DataHeader DH;
	TFPS_EXPECT(!DH.Decode(R));
	uint32_t Seed = 1;
	for (int Len = 0; Len < 300; ++Len)
	{
		std::vector<uint8_t> Buf(static_cast<size_t>(Len));
		for (auto& X : Buf) { Seed = Seed * 1103515245u + 12345u; X = static_cast<uint8_t>(Seed >> 16); }
		ByteReader RR(Buf.data(), Buf.size());
		SnapshotHeader SH;
		std::vector<EntityState> E;
		DecodeSnapshot(RR, nullptr, SH, E);
		ByteReader RR2(Buf.data(), Buf.size());
		ReliableReceiver Rx;
		Rx.Read(RR2);
	}
	// NaN in local state is refused.
	std::vector<uint8_t> Bad;
	ByteWriter W(Bad);
	SnapshotHeader SH;
	SH.Local.Pos.X = BitsToFloat(0x7FC00000u);
	EncodeSnapshot(W, SH, {}, nullptr);
	ByteReader RB(Bad.data(), Bad.size());
	std::vector<EntityState> E;
	TFPS_EXPECT(!DecodeSnapshot(RB, nullptr, SH, E));
}
