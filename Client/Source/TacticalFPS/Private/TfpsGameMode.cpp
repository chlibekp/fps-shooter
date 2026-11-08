// Copyright TacticalFPS project.
#include "TfpsGameMode.h"
#include "TacticalFPS.h"
#include "TfpsArenaBuilder.h"
#include "TfpsEnvironment.h"
#include "TfpsHUD.h"
#include "TfpsNetSubsystem.h"
#include "TfpsPlayerController.h"
#include "TfpsPlayerPawn.h"
#include "TfpsRemotePlayer.h"
#include "TfpsShotFx.h"
#include "TfpsGadgetFx.h"
#include "TfpsParticleFx.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"

ATfpsGameMode::ATfpsGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	DefaultPawnClass = ATfpsPlayerPawn::StaticClass();
	PlayerControllerClass = ATfpsPlayerController::StaticClass();
	HUDClass = ATfpsHUD::StaticClass();
}

void ATfpsGameMode::BeginPlay()
{
	Super::BeginPlay();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<ATfpsEnvironment>(ATfpsEnvironment::StaticClass(), FTransform::Identity, Params);
	GetWorld()->SpawnActor<ATfpsArenaBuilder>(ATfpsArenaBuilder::StaticClass(), FTransform::Identity, Params);
	GetWorld()->SpawnActor<ATfpsShotFx>(ATfpsShotFx::StaticClass(), FTransform::Identity, Params);
	GetWorld()->SpawnActor<ATfpsGadgetFx>(ATfpsGadgetFx::StaticClass(), FTransform::Identity, Params);
	GetWorld()->SpawnActor<ATfpsParticleFx>(ATfpsParticleFx::StaticClass(), FTransform::Identity, Params);
}

void ATfpsGameMode::RestartPlayer(AController* NewPlayer)
{
	// There are no PlayerStarts: the pawn is positioned from network state.
	if (NewPlayer == nullptr || NewPlayer->GetPawn() != nullptr)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (APawn* Pawn = GetWorld()->SpawnActor<ATfpsPlayerPawn>(ATfpsPlayerPawn::StaticClass(), FTransform::Identity, Params))
	{
		NewPlayer->Possess(Pawn);
	}
}

void ATfpsGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	SyncRemotePlayers();
}

void ATfpsGameMode::SyncRemotePlayers()
{
	const UTfpsNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTfpsNetSubsystem>() : nullptr;
	const tfps::ClientCore* Core = Net ? Net->GetCore() : nullptr;

	UTfpsAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTfpsAudioSubsystem>() : nullptr;
	TSet<int32> Alive;
	if (Core != nullptr && Core->IsConnected())
	{
		for (const auto& KV : Core->Players())
		{
			const tfps::RemotePlayer& P = KV.second;
			if (!P.bPresent || P.Samples.empty())
			{
				continue;
			}
			Alive.Add(P.Id);
			TObjectPtr<ATfpsRemotePlayer>& Actor = RemoteActors.FindOrAdd(P.Id);
			if (Actor == nullptr)
			{
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Actor = GetWorld()->SpawnActor<ATfpsRemotePlayer>(ATfpsRemotePlayer::StaticClass(), FTransform::Identity, Params);
			}
			if (Actor != nullptr)
			{
				const tfps::EntityState& Latest = P.Samples.back().second;
				const FVector Feet(P.RenderPos.X, P.RenderPos.Y, P.RenderPos.Z);
				Actor->ApplyState(Feet, P.RenderYawDeg, P.RenderPitchDeg, P.Flags,
					Latest.Lean(), Latest.Weapon, UTF8_TO_TCHAR(P.Name.c_str()), P.Team, P.ConnState != tfps::EConnState::Connected);
				FRemoteSound& Snd = RemoteSounds.FindOrAdd(P.Id);
				const bool bAliveNow = (P.Flags & tfps::EntityFlags::Alive) != 0;
				if (Audio != nullptr && bAliveNow)
				{
					// Sprinting is loud, crouching quiet (sound tells where enemies are).
					const bool bSprint = (P.Flags & tfps::EntityFlags::Sprinting) != 0;
					const bool bQuiet = (P.Flags & (tfps::EntityFlags::Crouched | tfps::EntityFlags::Prone)) != 0;
					bool bLanded = false;
					if (Snd.Steps.Update(Feet, (P.Flags & tfps::EntityFlags::OnGround) != 0, bSprint ? 2.2f : 1.7f, bLanded))
					{
						Audio->Footstep(Net->GetMap(), Feet, bSprint ? 1.0f : (bQuiet ? 0.35f : 0.7f), false);
					}
					if (bLanded)
					{
						Audio->PlayAt(tfps::audio::Cue::Land, Feet * TfpsMetersToUnreal);
					}
					if ((P.Flags & tfps::EntityFlags::Reloading) != 0 && (Snd.Flags & tfps::EntityFlags::Reloading) == 0)
					{
						Audio->PlayAt(tfps::audio::Cue::MagOut, Feet * TfpsMetersToUnreal + FVector(0.0, 0.0, 120.0));
					}
				}
				Snd.Flags = P.Flags;
			}
		}
	}
	for (auto It = RemoteActors.CreateIterator(); It; ++It)
	{
		if (!Alive.Contains(It.Key()))
		{
			if (It.Value() != nullptr)
			{
				It.Value()->Destroy();
			}
			RemoteSounds.Remove(It.Key());
			It.RemoveCurrent();
		}
	}
}
