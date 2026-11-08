// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Interfaces/IHttpRequest.h"

#include "TfpsLobbySubsystem.generated.h"

/**
 * Client of the lobby service (Lobby/, HTTP JSON API): host a lobby and get
 * an invite code, join friends by code, choose squad / readiness, edit the
 * match settings as host, start the match. When the lobby's match starts,
 * connects UTfpsNetSubsystem to the game server with the lobby's join
 * token; when the match ends the lobby is open again.
 *
 * Driven by the front-end menu (tfps::ui::Menu on ATfpsPlayerController,
 * drawn by ATfpsHUD); the console commands TfpsHost, TfpsJoin, TfpsSet,
 * TfpsSquad, TfpsReady, TfpsStart and TfpsLeave remain for scripting.
 */
UCLASS()
class TACTICALFPS_API UTfpsLobbySubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	struct FMember { int32 Id = 0; FString Name; int32 Squad = 0; bool bReady = false; };
	struct FView
	{
		FString Code;
		FString Status;   // "open" or "in_game"
		int32 Host = 0;
		int32 You = 0;
		TArray<FMember> Members;
		FString Map, Mode;
		int32 TeamSize = 0, RoundsToWin = 0, ScoreLimit = 0, TickRate = 64;
		float RoundTime = 0.0f, PrepTime = 0.0f;
		bool bFriendlyFire = false;
		FString GameAddress;
		uint64 JoinToken = 0;
		/** Maps the service can host. */
		TArray<FString> Maps;
	};

	/** Base URL of the lobby service, e.g. http://127.0.0.1:7700 (-lobby= on the command line). */
	void SetService(const FString& BaseUrl) { Service = BaseUrl; }
	bool IsConfigured() const { return !Service.IsEmpty(); }
	bool InLobby() const { return !Token.IsEmpty(); }
	const FView& GetView() const { return View; }
	const FString& GetLastError() const { return LastError; }
	bool IsHost() const { return InLobby() && View.Host == View.You; }

	void Host(const FString& PlayerName);
	void Join(const FString& Code, const FString& PlayerName);
	/** Host only: one setting (map, mode, team_size, rounds_to_win, round_time, prep_time, score_limit, friendly_fire, tick_rate). */
	void SetSetting(const FString& Key, const FString& Value);
	void SetSquad(int32 Squad);
	void SetReady(bool bReady);
	void Start();
	void Leave();

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UTfpsLobbySubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return !IsTemplate() && InLobby(); }
	virtual ETickableTickType GetTickableTickType() const override { return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional; }

private:
	enum class ERequest : uint8 { Create, Join, View, Action, Leave };

	void Send(const FString& Verb, const FString& Path, const FString& Body, ERequest Kind);
	void OnResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnected, ERequest Kind);
	bool ParseView(const std::string& Json, bool bWithToken);
	FString SettingsJson() const;

	FString Service;
	FString Token;
	FString PlayerName;
	FView View;
	FString LastError;
	double LastPoll = 0.0;
	bool bPollInFlight = false;
	/** Join token of the game server we already connected to (avoid reconnecting every poll). */
	uint64 ConnectedToken = 0;
};
