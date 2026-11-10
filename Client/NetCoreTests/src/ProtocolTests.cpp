// C++ protocol vs Rust golden byte vectors, plus local round-trip tests.
#include "TestFramework.h"
#include "NetCore/TfpsProtocol.h"

#include <fstream>
#include <map>
#include <sstream>
#include "NetCore/TfpsMap.h"
#include "NetCore/TfpsWeapon.h"
#include "NetCore/TfpsGadget.h"
#include <cstdio>

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
		L.bCrouched = true;
		L.JumpTimer = 0.4375f;
		L.StanceTimer = 0.125f;
		L.Lean = 0.5f;
		L.Aim = 0.75f;
		return L;
	}

	WeaponState GoldenWeapon()
	{
		WeaponState W;
		W.Active = 1; W.Cooldown = 0.0625f; W.ShotsFired = 3; W.RecoilPitch = 1.5f; W.RecoilYaw = -0.25f; W.ShotCounter = 42;
		W.Slots[0].Ammo = 29; W.Slots[0].bChambered = true; W.Slots[0].Reserve = 150;
		W.Slots[1].Ammo = 0; W.Slots[1].bChambered = false; W.Slots[1].Reserve = 60;
		return W;
	}

	std::vector<EntityState> GoldenBase()
	{
		EntityState A; A.Id = 2; A.PosMm[0] = 20000; A.PosMm[1] = -6000; A.PosMm[2] = 0; A.YawQ = 32768; A.PitchQ = 0; A.Flags = 9; A.Health = 100;
		A.LeanQ = -127; A.Weapon = 1; A.AimQ = 255;
		EntityState B; B.Id = 5; B.PosMm[0] = -1; B.PosMm[1] = 2; B.PosMm[2] = 3300; B.YawQ = 1; B.PitchQ = -32767; B.Flags = 27; B.Health = 42;
		B.LeanQ = 0; B.Weapon = 4; B.AimQ = 0;
		return {A, B};
	}
}

TFPS_TEST(GoldenHandshakeBytesMatchRust)
{
	auto V = LoadVectors();
	TFPS_REQUIRE(V.size() == 15);
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
		TFPS_EXPECT(A.WeaponsHash == 0x0123456789ABCDEFull && A.Loadout[0] == 1 && A.Loadout[1] == 4);
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
		IB.Cmds[0] = {0x0041, 16384, -1200, 1};
		IB.Cmds[1] = {0x0001, 16000, -1100, 0};
		IB.Cmds[2] = {0x0000, 15000, 32767, 0};
		IB.RenderTimes[0] = 0x00012345u;
		IB.RenderTimes[1] = 0x00012300u;
		IB.RenderTimes[2] = 0x00012200u;
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
	TFPS_REQUIRE(Rx.Pop(Type, Payload) && M.Decode(Type, Payload));
	TFPS_EXPECT(M.Type == MsgType::HitConfirm && M.OtherId == 5 && M.Amount == 108 && M.bHeadshot && M.bKilled);
	TFPS_EXPECT(M.EncodePayload() == Payload);
	TFPS_REQUIRE(Rx.Pop(Type, Payload) && M.Decode(Type, Payload));
	TFPS_EXPECT(M.Type == MsgType::Damaged && M.OtherId == 2 && M.Amount == 36 && M.Health == 64 && M.FromMm[0] == 20000 && M.FromMm[2] == 1650);
	TFPS_EXPECT(M.EncodePayload() == Payload);
	TFPS_REQUIRE(Rx.Pop(Type, Payload) && M.Decode(Type, Payload));
	TFPS_EXPECT(M.Type == MsgType::CellsDestroyed && M.Cells.size() == 3 && M.Cells[2] == 1000);
	TFPS_EXPECT(M.EncodePayload() == Payload);
	TFPS_REQUIRE(Rx.Pop(Type, Payload) && M.Decode(Type, Payload));
	TFPS_EXPECT(M.Type == MsgType::Explosion && M.FromMm[0] == -1500 && M.RadiusCm == 350);
	TFPS_EXPECT(M.EncodePayload() == Payload);
	TFPS_REQUIRE(Rx.Pop(Type, Payload) && M.Decode(Type, Payload));
	TFPS_EXPECT(M.Type == MsgType::Kill && M.OtherId == 2 && M.PlayerId == 5 && M.WeaponId == 1 && M.bHeadshot);
	TFPS_EXPECT(M.EncodePayload() == Payload);
	TFPS_REQUIRE(Rx.Pop(Type, Payload) && M.Decode(Type, Payload));
	TFPS_EXPECT(M.Type == MsgType::Flash && M.FromMm[0] == 3000 && M.FromMm[1] == -4000 && M.FromMm[2] == 120 && M.Intensity == 211);
	TFPS_EXPECT(M.EncodePayload() == Payload);
	TFPS_REQUIRE(Rx.Pop(Type, Payload) && M.Decode(Type, Payload));
	TFPS_EXPECT(M.Type == MsgType::Spotted && M.PlayerId == 5 && M.OtherId == 2 && M.FromMm[0] == 10000 && M.FromMm[1] == -2500 && M.DurationDs == 40);
	TFPS_EXPECT(M.EncodePayload() == Payload);
	TFPS_REQUIRE(Rx.Pop(Type, Payload) && M.Decode(Type, Payload));
	TFPS_EXPECT(M.Type == MsgType::SolidAdded && M.SolidIndex == 105 && M.MinMm[0] == 4000 && M.MinMm[1] == -950 && M.MaxMm[2] == 2200 && M.Material == 2 && M.bDestructible);
	TFPS_EXPECT(M.EncodePayload() == Payload);
	TFPS_REQUIRE(Rx.Pop(Type, Payload) && M.Decode(Type, Payload));
	TFPS_EXPECT(M.Type == MsgType::SolidReinforced && M.SolidIndex == 17);
	TFPS_EXPECT(M.EncodePayload() == Payload);
	TFPS_REQUIRE(Rx.Pop(Type, Payload) && M.Decode(Type, Payload));
	TFPS_EXPECT(M.Type == MsgType::MatchState && M.Match.Mode == 2 && M.Match.Phase == 1 && M.Match.Round == 2 && M.Match.Score[0] == 1
		&& M.Match.TimeLeftDs == 300 && M.Match.RoundsToWin == 4 && M.Match.DeviceSite == 255 && M.Match.LastReason == 3);
	TFPS_EXPECT(M.EncodePayload() == Payload);
	TFPS_REQUIRE(Rx.Pop(Type, Payload) && M.Decode(Type, Payload));
	TFPS_EXPECT(M.Type == MsgType::MapReset && Payload.empty());
	TFPS_REQUIRE(Rx.Pop(Type, Payload) && M.Decode(Type, Payload));
	TFPS_EXPECT(M.Type == MsgType::DoorState && M.SolidIndex == 23 && M.bOpen);
	TFPS_EXPECT(M.EncodePayload() == Payload);
	TFPS_EXPECT(!Rx.Pop(Type, Payload));
	TFPS_REQUIRE(R.U8() == static_cast<uint8_t>(EBodyKind::Snapshot));
	SnapshotHeader SH;
	std::vector<EntityState> Ents;
	std::vector<ShotEvent> ShotsIn;
	std::vector<GadgetEntityState> GadgetsIn;
	TFPS_REQUIRE(DecodeSnapshot(R, nullptr, SH, Ents, ShotsIn, &GadgetsIn));
	TFPS_EXPECT(R.Remaining() == 0);
	TFPS_EXPECT(SH.Reinforcements == 2 && SH.ReinforceProgress == 128 && SH.Action == 2);
	TFPS_EXPECT(SH.Gadgets[0].Id == 1 && SH.Gadgets[0].Count == 2 && SH.Gadgets[1].Id == 4 && SH.Gadgets[1].Count == 3 && SH.Gadgets[2].Id == 5 && SH.Gadgets[2].Count == 1 && SH.Gadgets[3].Id == 3 && SH.Gadgets[3].Count == 2);
	TFPS_REQUIRE(GadgetsIn.size() == 1);
	TFPS_EXPECT(GadgetsIn[0].Id == 3 && GadgetsIn[0].Gadget == 1 && GadgetsIn[0].Phase == GadgetPhase::Flying && GadgetsIn[0].Owner == 2
		&& GadgetsIn[0].PosCm[0] == -1234 && GadgetsIn[0].PosCm[1] == 560 && GadgetsIn[0].PosCm[2] == 87);
	TFPS_EXPECT(SH.ServerTick == 1000 && SH.BaselineTick == 0 && SH.LastCmdSeq == 48 && SH.InputBufferDepth == 2 && SH.LocalHealth == 100);
	TFPS_EXPECT(SH.Local == GoldenLocal());
	TFPS_EXPECT(SH.Weapon == GoldenWeapon());
	TFPS_EXPECT(SH.bAlive && SH.RespawnIn == 0.0f);
	TFPS_EXPECT(Ents == GoldenBase());
	TFPS_REQUIRE(ShotsIn.size() == 1);
	TFPS_EXPECT(ShotsIn[0].Shooter == 2 && ShotsIn[0].Weapon == 1 && ShotsIn[0].EndMm[0] == -20000 && ShotsIn[0].SurfaceKind == Surface::Player);

	// Re-encode the snapshot body and compare with the Rust bytes.
	std::vector<uint8_t> Body;
	ByteWriter BW(Body);
	EncodeSnapshot(BW, SH, Ents, nullptr, ShotsIn, &GadgetsIn);
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
	std::vector<ShotEvent> ShotsIn;
	TFPS_REQUIRE(DecodeSnapshot(R, &Base, SH, Ents, ShotsIn));
	TFPS_EXPECT(SH.ServerTick == 1002 && SH.InputBufferDepth == -1 && SH.LocalHealth == 0 && !SH.bAlive && SH.RespawnIn == 4.5f);
	TFPS_REQUIRE(Ents.size() == 2);
	TFPS_EXPECT(Ents[0].Id == 2 && Ents[0].PosMm[0] == 19950 && Ents[0].YawQ == 32000 && Ents[0].Health == 100 && Ents[0].LeanQ == 64 && Ents[0].Weapon == 1);
	TFPS_EXPECT(Ents[1].Id == 8 && Ents[1].PosMm[2] == 1 && Ents[1].PitchQ == 3 && Ents[1].Weapon == 2 && Ents[1].AimQ == 7);
	TFPS_EXPECT(ShotsIn.empty());
	// Without the baseline it must be refused.
	ByteReader R2(P.data(), P.size());
	SnapshotHeader SH2;
	std::vector<EntityState> Ents2;
	std::vector<ShotEvent> Shots2;
	TFPS_EXPECT(!DecodeSnapshot(R2, nullptr, SH2, Ents2, Shots2));
	// Re-encode identically.
	std::vector<uint8_t> Out;
	ByteWriter W(Out);
	EncodeSnapshot(W, SH, Ents, &Base, ShotsIn);
	TFPS_EXPECT(Out == P);
}

TFPS_TEST(DefinitionHashesMatchRust)
{
	auto V = LoadVectors();
	auto ReadText = [](const std::string& Path)
	{
		std::ifstream F(Path, std::ios::binary);
		std::stringstream SS;
		SS << F.rdbuf();
		return SS.str();
	};
	auto HexU64 = [](const std::vector<uint8_t>& B)
	{
		// Vector loader parsed the hex as bytes; rebuild the big-endian value.
		uint64_t X = 0;
		for (uint8_t C : B) { X = (X << 8) | C; }
		return X;
	};
	MapDef Map;
	WeaponSet Weapons;
	GadgetSet Gadgets;
	std::string Err;
	TFPS_REQUIRE(ParseGadgetsJson(ReadText(std::string(TFPS_REPO_ROOT) + "/Shared/Definitions/gadgets.json"), Gadgets, Err));
	TFPS_EXPECT(Gadgets.Gadgets.size() == 9 && Gadgets.Get(9)->Kind == EGadgetKind::Barricade && Gadgets.Get(7)->Kind == EGadgetKind::MotionSensor && Gadgets.Get(4) != nullptr && Gadgets.Get(4)->Kind == EGadgetKind::Breach);
	TFPS_EXPECT(Gadgets.Get(5) != nullptr && Gadgets.Get(5)->Kind == EGadgetKind::Drone && Gadgets.Get(5)->SpotRange == 40.0f);
	std::printf("  gadgets hash %016llx\n", static_cast<unsigned long long>(Gadgets.Hash()));
	TFPS_EXPECT(Gadgets.Hash() == HexU64(V["gadgets_hash"]));
	TFPS_REQUIRE(ParseMapJson(ReadText(std::string(TFPS_REPO_ROOT) + "/Shared/Definitions/maps/test_arena.json"), Map, Err));
	TFPS_REQUIRE(ParseWeaponsJson(ReadText(std::string(TFPS_REPO_ROOT) + "/Shared/Definitions/weapons.json"), Weapons, Err));
	TFPS_EXPECT(Map.GeometryHash() == HexU64(V["map_hash_test_arena"]));
	for (const char* Name : {"harbor", "villa", "consulate"})
	{
		MapDef M;
		TFPS_REQUIRE(ParseMapJson(ReadText(std::string(TFPS_REPO_ROOT) + "/Shared/Definitions/maps/" + Name + ".json"), M, Err));
		std::printf("  map %s: %d solids, hash %016llx\n", Name, static_cast<int>(M.Solids.size()), static_cast<unsigned long long>(M.GeometryHash()));
		TFPS_EXPECT(M.GeometryHash() == HexU64(V[std::string("map_hash_") + Name]));
		TFPS_EXPECT(M.Objectives.size() == 2 && !M.Objectives[0].Name.empty());
	}
	TFPS_EXPECT(Weapons.Hash() == HexU64(V["weapons_hash"]));
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
		std::vector<ShotEvent> Sv;
		DecodeSnapshot(RR, nullptr, SH, E, Sv);
		ByteReader RR2(Buf.data(), Buf.size());
		ReliableReceiver Rx;
		Rx.Read(RR2);
	}
	// NaN in local state is refused.
	std::vector<uint8_t> Bad;
	ByteWriter W(Bad);
	SnapshotHeader SH;
	SH.Local.Pos.X = BitsToFloat(0x7FC00000u);
	EncodeSnapshot(W, SH, {}, nullptr, {});
	ByteReader RB(Bad.data(), Bad.size());
	std::vector<EntityState> E;
	std::vector<ShotEvent> Sv;
	TFPS_EXPECT(!DecodeSnapshot(RB, nullptr, SH, E, Sv));
}
