// Headless C++ client: connects to the Rust server with the same NetCore code
// the Unreal client uses, runs a movement script and prints what it sees.
//
//   tfps_cli_client [--server host:port] [--name N] [--seconds S] [--pattern circle|idle|square]
#include "UdpTransport.h"
#include "NetCore/TfpsClientCore.h"
#include "NetCore/TfpsDeterminism.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

int main(int Argc, char** Argv)
{
	std::string Server = "127.0.0.1:7777";
	std::string Name = "CppClient";
	std::string Pattern = "circle";
	std::string MapPath = std::string(TFPS_REPO_ROOT) + "/Shared/Definitions/maps/test_arena.json";
	double Seconds = 10.0;
	for (int I = 1; I + 1 < Argc; I += 2)
	{
		if (!std::strcmp(Argv[I], "--server")) { Server = Argv[I + 1]; }
		else if (!std::strcmp(Argv[I], "--name")) { Name = Argv[I + 1]; }
		else if (!std::strcmp(Argv[I], "--seconds")) { Seconds = std::atof(Argv[I + 1]); }
		else if (!std::strcmp(Argv[I], "--pattern")) { Pattern = Argv[I + 1]; }
		else if (!std::strcmp(Argv[I], "--map")) { MapPath = Argv[I + 1]; }
	}
	std::printf("determinism self-test: %s\n", tfps::DeterminismSelfTestPasses() ? "PASS" : "FAIL (prediction will be corrected often)");
	std::ifstream F(MapPath);
	std::stringstream SS;
	SS << F.rdbuf();
	tfps::MapDef Map;
	std::string Err;
	if (!tfps::ParseMapJson(SS.str(), Map, Err)) { std::fprintf(stderr, "map: %s\n", Err.c_str()); return 1; }
	FUdpTransport Transport;
	if (!Transport.Open(Server)) { std::fprintf(stderr, "cannot open %s\n", Server.c_str()); return 1; }
	tfps::ClientConfig Cfg;
	Cfg.Name = Name;
	tfps::ClientCore Client(Cfg, Map, Transport, NowSeconds());
	const double Start = NowSeconds();
	double NextReport = Start + 1.0;
	while (NowSeconds() - Start < Seconds)
	{
		const double T = NowSeconds() - Start;
		tfps::MoveInput In;
		if (Pattern == "circle")
		{
			In.Buttons = tfps::Buttons::Forward;
			In.YawQ = tfps::QuantizeYawDeg(static_cast<float>(std::fmod(T * 45.0, 360.0)));
		}
		else if (Pattern == "square")
		{
			In.Buttons = tfps::Buttons::Forward;
			In.YawQ = static_cast<uint16_t>((static_cast<int>(T / 1.5) % 4) * 16384);
		}
		Client.SetInput(In);
		Client.Update(NowSeconds());
		tfps::ClientEvent E;
		while (Client.PollEvent(E))
		{
			switch (E.Type)
			{
			case tfps::ClientEvent::EType::Connected: std::printf("[%s] connected as player %u\n", Name.c_str(), E.PlayerId); break;
			case tfps::ClientEvent::EType::PlayerJoined:
				if (E.PlayerId != Client.PlayerId()) { std::printf("[%s] sees player %u '%s' (team %u)\n", Name.c_str(), E.PlayerId, E.Name.c_str(), E.Team); }
				break;
			case tfps::ClientEvent::EType::PlayerLeft: std::printf("[%s] player %u left (%s)\n", Name.c_str(), E.PlayerId, tfps::DisconnectReasonName(E.LeftReason)); break;
			case tfps::ClientEvent::EType::PlayerConnState: std::printf("[%s] player %u connection state %u\n", Name.c_str(), E.PlayerId, static_cast<unsigned>(E.ConnState)); break;
			case tfps::ClientEvent::EType::Disconnected:
				std::printf("[%s] disconnected (cause %u%s%s)\n", Name.c_str(), static_cast<unsigned>(E.Cause),
					E.Cause == tfps::EDisconnectCause::Rejected ? ": " : "", E.Cause == tfps::EDisconnectCause::Rejected ? tfps::RejectReasonName(E.RejectReason) : "");
				return 2;
			}
		}
		if (Client.IsConnected() && NowSeconds() >= NextReport)
		{
			NextReport += 1.0;
			const tfps::Vec3 P = Client.Predicted().Pos;
			std::printf("[%s] pos=(%.2f,%.2f,%.2f) rtt=%.1fms corrections=%llu sees:", Name.c_str(), P.X, P.Y, P.Z, Client.Stats().RttMs,
				static_cast<unsigned long long>(Client.Stats().Corrections));
			for (const auto& KV : Client.Players())
			{
				if (KV.second.bPresent)
				{
					std::printf(" %s@(%.2f,%.2f,%.2f)", KV.second.Name.c_str(), KV.second.RenderPos.X, KV.second.RenderPos.Y, KV.second.RenderPos.Z);
				}
			}
			std::printf("\n");
			std::fflush(stdout);
		}
		SleepMs(1);
	}
	Client.Disconnect();
	return 0;
}
