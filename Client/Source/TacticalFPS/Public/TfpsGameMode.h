// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "TfpsAudioSubsystem.h"

#include "TfpsGameMode.generated.h"

class ATfpsRemotePlayer;

/**
 * Client-side game mode (the game runs as a standalone Unreal world; the Rust
 * server is authoritative). Spawns the local pawn, the lighting rig and the
 * map graybox, and keeps one ATfpsRemotePlayer per remote player.
 */
UCLASS()
class TACTICALFPS_API ATfpsGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATfpsGameMode();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void RestartPlayer(AController* NewPlayer) override;

private:
	void SyncRemotePlayers();

	UPROPERTY()
	TMap<int32, TObjectPtr<ATfpsRemotePlayer>> RemoteActors;
	/** Footstep / reload sound state per remote player. */
	struct FRemoteSound { FTfpsStepTracker Steps; uint8 Flags = 0; };
	TMap<int32, FRemoteSound> RemoteSounds;
};
