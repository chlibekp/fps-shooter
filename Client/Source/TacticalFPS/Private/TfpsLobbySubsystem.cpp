// Copyright TacticalFPS project.
#include "TfpsLobbySubsystem.h"
#include "TacticalFPS.h"
#include "TfpsNetSubsystem.h"

#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "NetCore/TfpsJson.h"

#include <cstdlib>
#include <string>

namespace
{
	constexpr double PollInterval = 1.0;

	FString JsonEscape(const FString& In)
	{
		FString Out;
		for (const TCHAR C : In)
		{
			if (C == TEXT('"') || C == TEXT('\\')) { Out += TEXT('\\'); }
			if (C >= 0x20) { Out += C; }
		}
		return Out;
	}

	FString Str(const tfps::JsonValue& O, const char* Key)
	{
		const tfps::JsonValue* V = O.Find(Key);
		return (V != nullptr && V->Kind == tfps::JsonValue::EKind::String) ? FString(UTF8_TO_TCHAR(V->String.c_str())) : FString();
	}
}

void UTfpsLobbySubsystem::Host(const FString& InName)
{
	PlayerName = InName.Left(24);
	Send(TEXT("POST"), TEXT("/lobbies"), FString::Printf(TEXT("{\"name\":\"%s\"}"), *JsonEscape(PlayerName)), ERequest::Create);
}

void UTfpsLobbySubsystem::Join(const FString& Code, const FString& InName)
{
	PlayerName = InName.Left(24);
	Send(TEXT("POST"), TEXT("/lobbies/join"),
		FString::Printf(TEXT("{\"code\":\"%s\",\"name\":\"%s\"}"), *JsonEscape(Code), *JsonEscape(PlayerName)), ERequest::Join);
}

FString UTfpsLobbySubsystem::SettingsJson() const
{
	return FString::Printf(TEXT("{\"map\":\"%s\",\"mode\":\"%s\",\"team_size\":%d,\"rounds_to_win\":%d,\"round_time\":%.1f,\"prep_time\":%.1f,")
		TEXT("\"score_limit\":%d,\"friendly_fire\":%s,\"tick_rate\":%d}"),
		*JsonEscape(View.Map), *JsonEscape(View.Mode), View.TeamSize, View.RoundsToWin, View.RoundTime, View.PrepTime, View.ScoreLimit,
		View.bFriendlyFire ? TEXT("true") : TEXT("false"), View.TickRate);
}

void UTfpsLobbySubsystem::SetSetting(const FString& Key, const FString& Value)
{
	if (!IsHost())
	{
		LastError = TEXT("only the host changes settings");
		return;
	}
	const int32 I = FCString::Atoi(*Value);
	const float F = FCString::Atof(*Value);
	if (Key == TEXT("map")) { View.Map = Value; }
	else if (Key == TEXT("mode")) { View.Mode = Value; }
	else if (Key == TEXT("team_size")) { View.TeamSize = I; }
	else if (Key == TEXT("rounds_to_win")) { View.RoundsToWin = I; }
	else if (Key == TEXT("round_time")) { View.RoundTime = F; }
	else if (Key == TEXT("prep_time")) { View.PrepTime = F; }
	else if (Key == TEXT("score_limit")) { View.ScoreLimit = I; }
	else if (Key == TEXT("friendly_fire")) { View.bFriendlyFire = Value == TEXT("1") || Value == TEXT("true") || Value == TEXT("on"); }
	else if (Key == TEXT("tick_rate")) { View.TickRate = I; }
	else
	{
		LastError = FString::Printf(TEXT("unknown setting '%s'"), *Key);
		return;
	}
	Send(TEXT("POST"), FString::Printf(TEXT("/lobbies/%s/settings"), *View.Code), SettingsJson(), ERequest::Action);
}

void UTfpsLobbySubsystem::SetSquad(int32 Squad)
{
	Send(TEXT("POST"), FString::Printf(TEXT("/lobbies/%s/squad"), *View.Code), FString::Printf(TEXT("{\"squad\":%d}"), Squad), ERequest::Action);
}

void UTfpsLobbySubsystem::SetReady(bool bReady)
{
	Send(TEXT("POST"), FString::Printf(TEXT("/lobbies/%s/ready"), *View.Code), bReady ? TEXT("{\"ready\":true}") : TEXT("{\"ready\":false}"),
		ERequest::Action);
}

void UTfpsLobbySubsystem::Start()
{
	Send(TEXT("POST"), FString::Printf(TEXT("/lobbies/%s/start"), *View.Code), TEXT("{}"), ERequest::Action);
}

void UTfpsLobbySubsystem::Leave()
{
	if (InLobby())
	{
		Send(TEXT("POST"), FString::Printf(TEXT("/lobbies/%s/leave"), *View.Code), TEXT("{}"), ERequest::Leave);
	}
	Token.Reset();
	View = FView();
	ConnectedToken = 0;
}

void UTfpsLobbySubsystem::Send(const FString& Verb, const FString& Path, const FString& Body, ERequest Kind)
{
	if (Service.IsEmpty())
	{
		LastError = TEXT("no lobby service configured (-lobby=http://host:7700)");
		return;
	}
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(Service + Path);
	Req->SetVerb(Verb);
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	if (!Token.IsEmpty())
	{
		Req->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Token);
	}
	if (Verb != TEXT("GET"))
	{
		Req->SetContentAsString(Body);
	}
	Req->OnProcessRequestComplete().BindUObject(this, &UTfpsLobbySubsystem::OnResponse, Kind);
	Req->ProcessRequest();
}

bool UTfpsLobbySubsystem::ParseView(const std::string& Json, bool bWithToken)
{
	tfps::JsonValue Root;
	std::string Err;
	if (!tfps::ParseJson(Json, Root, Err))
	{
		LastError = TEXT("lobby service sent invalid JSON");
		return false;
	}
	if (const tfps::JsonValue* E = Root.Find("error"))
	{
		LastError = UTF8_TO_TCHAR(E->String.c_str());
		return false;
	}
	if (bWithToken)
	{
		Token = Str(Root, "token");
	}
	const tfps::JsonValue* L = Root.Find("lobby");
	if (L == nullptr)
	{
		return true;
	}
	FView V;
	V.Code = Str(*L, "code");
	V.Status = Str(*L, "status");
	V.Host = static_cast<int32>(tfps::JsonNumber(*L, "host", 0));
	V.You = static_cast<int32>(tfps::JsonNumber(*L, "you", 0));
	if (const tfps::JsonValue* Ms = L->Find("members"))
	{
		for (const tfps::JsonValue& M : Ms->Items)
		{
			FMember Mem;
			Mem.Id = static_cast<int32>(tfps::JsonNumber(M, "id", 0));
			Mem.Name = Str(M, "name");
			Mem.Squad = static_cast<int32>(tfps::JsonNumber(M, "squad", 0));
			const tfps::JsonValue* R = M.Find("ready");
			Mem.bReady = R != nullptr && R->Bool;
			V.Members.Add(Mem);
		}
	}
	if (const tfps::JsonValue* S = L->Find("settings"))
	{
		V.Map = Str(*S, "map");
		V.Mode = Str(*S, "mode");
		V.TeamSize = static_cast<int32>(tfps::JsonNumber(*S, "team_size", 5));
		V.RoundsToWin = static_cast<int32>(tfps::JsonNumber(*S, "rounds_to_win", 4));
		V.RoundTime = static_cast<float>(tfps::JsonNumber(*S, "round_time", 180));
		V.PrepTime = static_cast<float>(tfps::JsonNumber(*S, "prep_time", 30));
		V.ScoreLimit = static_cast<int32>(tfps::JsonNumber(*S, "score_limit", 50));
		V.TickRate = static_cast<int32>(tfps::JsonNumber(*S, "tick_rate", 64));
		const tfps::JsonValue* FF = S->Find("friendly_fire");
		V.bFriendlyFire = FF != nullptr && FF->Bool;
	}
	if (const tfps::JsonValue* G = L->Find("game"))
	{
		if (G->Kind == tfps::JsonValue::EKind::Object)
		{
			V.GameAddress = Str(*G, "address");
			const FString Hex = Str(*G, "join_token");
			V.JoinToken = static_cast<uint64>(std::strtoull(TCHAR_TO_UTF8(*Hex), nullptr, 16));
		}
	}
	View = V;
	LastError.Reset();
	return true;
}

void UTfpsLobbySubsystem::OnResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnected, ERequest Kind)
{
	if (Kind == ERequest::View)
	{
		bPollInFlight = false;
	}
	if (Kind == ERequest::Leave)
	{
		return;
	}
	if (!bConnected || !Response.IsValid())
	{
		LastError = TEXT("lobby service unreachable");
		return;
	}
	const std::string Body(TCHAR_TO_UTF8(*Response->GetContentAsString()));
	ParseView(Body, Kind == ERequest::Create || Kind == ERequest::Join);
	(void)Request;
}

void UTfpsLobbySubsystem::Tick(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds();
	if (!bPollInFlight && Now - LastPoll >= PollInterval && !View.Code.IsEmpty())
	{
		// The poll is also the heartbeat that keeps us in the lobby.
		LastPoll = Now;
		bPollInFlight = true;
		Send(TEXT("GET"), FString::Printf(TEXT("/lobbies/%s"), *View.Code), FString(), ERequest::View);
	}
	// The match started: connect to its game server with the lobby's join token.
	UTfpsNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTfpsNetSubsystem>() : nullptr;
	if (Net != nullptr && View.Status == TEXT("in_game") && View.JoinToken != 0 && View.JoinToken != ConnectedToken)
	{
		ConnectedToken = View.JoinToken;
		Net->Disconnect();
		Net->Connect(View.GameAddress, PlayerName, View.Map, View.JoinToken);
	}
	if (View.Status == TEXT("open") && ConnectedToken != 0)
	{
		ConnectedToken = 0; // match over: back in the lobby
	}
	(void)DeltaTime;
}
