// Tests the C++ client core against a running Rust server:
//   tfps_netcore_tests --server 127.0.0.1:7777
#include "TestFramework.h"
#include "UdpTransport.h"
#include "NetCore/TfpsClientCore.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace
{
	const std::string& GServer = tfpstest::GServerAddr;

	tfps::MapDef LoadArena()
	{
		std::ifstream F(std::string(TFPS_REPO_ROOT) + "/Shared/Definitions/maps/test_arena.json");
		std::stringstream SS;
		SS << F.rdbuf();
		tfps::MapDef Map;
		std::string Err;
		tfps::ParseMapJson(SS.str(), Map, Err);
		return Map;
	}

	void Pump(std::vector<tfps::ClientCore*> Clients, double Seconds)
	{
		const double End = NowSeconds() + Seconds;
		while (NowSeconds() < End)
		{
			for (auto* C : Clients) { C->Update(NowSeconds()); }
			SleepMs(1);
		}
	}

	bool WaitConnected(std::vector<tfps::ClientCore*> Clients)
	{
		const double End = NowSeconds() + 5.0;
		while (NowSeconds() < End)
		{
			bool bAll = true;
			for (auto* C : Clients) { C->Update(NowSeconds()); bAll = bAll && C->IsConnected(); }
			if (bAll) { return true; }
			SleepMs(1);
		}
		return false;
	}
}

TFPS_SERVER_TEST(LiveTwoCppClientsSeeEachOtherMove)
{
	const tfps::MapDef Map = LoadArena();
	FUdpTransport TA, TB;
	TFPS_REQUIRE(TA.Open(GServer) && TB.Open(GServer));
	tfps::ClientConfig CA; CA.Name = "CppAlpha";
	tfps::ClientConfig CB; CB.Name = "CppBravo";
	tfps::ClientCore A(CA, Map, TA, NowSeconds());
	tfps::ClientCore B(CB, Map, TB, NowSeconds());
	TFPS_REQUIRE(WaitConnected({&A, &B}));
	Pump({&A, &B}, 0.4);
	TFPS_REQUIRE(B.Players().count(A.PlayerId()) == 1);
	TFPS_REQUIRE(A.Players().count(B.PlayerId()) == 1);
	TFPS_EXPECT(B.Players().at(A.PlayerId()).Name == "CppAlpha");

	const tfps::Vec3 Start = A.Predicted().Pos;
	tfps::MoveInput Run;
	Run.Buttons = tfps::Buttons::Forward;
	Run.YawQ = 16384; // +Y
	A.SetInput(Run);
	Pump({&A, &B}, 1.0);
	A.SetInput(tfps::MoveInput());
	Pump({&A, &B}, 0.5);
	const tfps::Vec3 End = A.Predicted().Pos;
	std::printf("  A moved (%.3f %.3f %.3f) -> (%.3f %.3f %.3f), server (%.3f %.3f %.3f)\n", Start.X, Start.Y, Start.Z, End.X, End.Y, End.Z,
		A.ServerState().Pos.X, A.ServerState().Pos.Y, A.ServerState().Pos.Z);
	TFPS_EXPECT(End.Y - Start.Y > 2.5f);
	TFPS_EXPECT(tfps::Distance(End, A.ServerState().Pos) < 1e-4f);
	// On a clean network mispredictions only come from input starvation at the
	// server (late input packets), never from simulation differences. Coarse
	// OS timers (Windows Sleep(1) ~ 15.6 ms) make the test client bursty, so a
	// few small corrections are allowed; the end state must still be exact.
	std::printf("  corrections %llu (max %.3f m)\n", static_cast<unsigned long long>(A.Stats().Corrections), A.Stats().MaxCorrectionM);
	TFPS_EXPECT(A.Stats().Corrections <= 10);
	TFPS_EXPECT(A.Stats().MaxCorrectionM < 0.25f);
	const tfps::Vec3 Seen = B.Players().at(A.PlayerId()).RenderPos;
	std::printf("  B sees A at (%.3f %.3f %.3f); rtt %.2f ms; delta snapshots %llu/%llu\n", Seen.X, Seen.Y, Seen.Z, B.Stats().RttMs,
		static_cast<unsigned long long>(B.Stats().DeltaSnapshots), static_cast<unsigned long long>(B.Stats().SnapshotsReceived));
	TFPS_EXPECT(tfps::Distance(Seen, End) < 0.01f);
	A.Disconnect();
	B.Disconnect();
}

TFPS_SERVER_TEST(LiveCppClientUnderPacketLoss)
{
	const tfps::MapDef Map = LoadArena();
	FUdpTransport TA, TB;
	TFPS_REQUIRE(TA.Open(GServer) && TB.Open(GServer));
	TA.SimulatedLoss = 0.2f;
	TB.SimulatedLoss = 0.2f;
	tfps::ClientConfig CA; CA.Name = "LossyA";
	tfps::ClientConfig CB; CB.Name = "LossyB";
	tfps::ClientCore A(CA, Map, TA, NowSeconds());
	tfps::ClientCore B(CB, Map, TB, NowSeconds());
	TFPS_REQUIRE(WaitConnected({&A, &B}));
	Pump({&A, &B}, 0.4);
	tfps::MoveInput Run;
	Run.Buttons = tfps::Buttons::Forward | tfps::Buttons::Sprint;
	Run.YawQ = 16384;
	const tfps::Vec3 Start = A.Predicted().Pos;
	A.SetInput(Run);
	Pump({&A, &B}, 1.2);
	A.SetInput(tfps::MoveInput());
	Pump({&A, &B}, 1.0);
	const tfps::Vec3 End = A.Predicted().Pos;
	TFPS_EXPECT(End.Y - Start.Y > 3.0f);
	TFPS_EXPECT(tfps::Distance(End, A.ServerState().Pos) < 1e-3f);
	TFPS_REQUIRE(B.Players().count(A.PlayerId()) == 1);
	TFPS_EXPECT(tfps::Distance(B.Players().at(A.PlayerId()).RenderPos, End) < 0.05f);
	std::printf("  corrections under 20%% loss: %llu (max %.3f m)\n", static_cast<unsigned long long>(A.Stats().Corrections), A.Stats().MaxCorrectionM);
	A.Disconnect();
	B.Disconnect();
}
