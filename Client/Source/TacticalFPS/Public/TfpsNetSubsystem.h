// Copyright TacticalFPS project.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "NetCore/TfpsClientCore.h"
#include "NetCore/TfpsGadget.h"
#include "TfpsSocketTransport.h"

#include <memory>

#include "TfpsNetSubsystem.generated.h"

UENUM(BlueprintType)
enum class ETfpsConnectionStatus : uint8
{
	Offline,
	Connecting,
	Connected,
	Failed,
};

/**
 * Owns the connection to the Rust dedicated server for the lifetime of the
 * game instance. Wraps the engine-independent tfps::ClientCore (prediction,
 * reconciliation, interpolation) and exposes its state to gameplay actors.
 *
 * Update order per frame (driven by ATfpsPlayerController::PlayerTick):
 * SetLocalInput -> Pump -> pawn/remote actors read the new state.
 */
UCLASS()
class TACTICALFPS_API UTfpsNetSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Loads the map definition and starts connecting. Address is "host:port". */
	UFUNCTION(BlueprintCallable, Category = "TacticalFPS|Net")
	bool Connect(const FString& Address, const FString& PlayerName, const FString& MapName);

	UFUNCTION(BlueprintCallable, Category = "TacticalFPS|Net")
	void Disconnect();

	UFUNCTION(BlueprintPure, Category = "TacticalFPS|Net")
	ETfpsConnectionStatus GetStatus() const { return Status; }

	UFUNCTION(BlueprintPure, Category = "TacticalFPS|Net")
	FString GetStatusText() const { return StatusText; }

	/** Runs network I/O and simulation ticks at most once per frame. */
	void Pump();
	void SetLocalInput(const tfps::MoveInput& Input);

	const tfps::ClientCore* GetCore() const { return Core.get(); }
	const tfps::MapDef* GetMap() const { return bHasMap ? &Map : nullptr; }
	const tfps::WeaponSet& GetWeapons() const { return Weapons; }
	const tfps::GadgetSet& GetGadgets() const { return Gadgets; }
	/** Display name of a held item / kill-feed weapon id (weapon id, or 0x80 | gadget id). */
	FString ItemName(uint8 ItemId) const;

	/** Combat feedback for the HUD (consumed as it is drawn). */
	struct FKillFeedEntry { FString Text; double Time = 0.0; bool bInvolvesMe = false; };
	struct FHitMarker { double Time = -100.0; bool bHeadshot = false; bool bKill = false; };
	struct FDamageIndicator { double Time = -100.0; FVector FromMeters = FVector::ZeroVector; int32 Amount = 0; };
	/** Last flashbang that affected the local player (Intensity 0..1). */
	struct FFlash { double Time = -100.0; float Intensity = 0.0f; float Duration = 4.0f; };
	const TArray<FKillFeedEntry>& GetKillFeed() const { return KillFeed; }
	const FHitMarker& GetHitMarker() const { return HitMarker; }
	const FDamageIndicator& GetDamageIndicator() const { return DamageIndicator; }
	const FFlash& GetFlash() const { return Flash; }
	/** Flashbang detonations to visualise (position in meters), drained by ATfpsGadgetFx. */
	TArray<FVector>& GetPendingFlashes() { return PendingFlashes; }
	/** Shots to visualise this frame (drained by ATfpsShotFx). */
	TArray<tfps::FxShot>& GetPendingShots() { return PendingShots; }
	/** Destroyed cell ids since last drained (ATfpsArenaBuilder removes them and spawns debris). */
	TArray<uint32>& GetPendingDestroyedCells() { return PendingDestroyedCells; }
	/** Explosions to visualise: centre (meters) and radius (meters). */
	TArray<TPair<FVector, float>>& GetPendingExplosions() { return PendingExplosions; }
	FString PlayerName(uint16 PlayerId) const;
	bool DeterminismSelfTestPassed() const { return bDeterminismOk; }
	const TArray<FString>& GetRecentEvents() const { return RecentEvents; }
	FString GetServerAddress() const { return ServerAddress; }

	/** Bumped whenever a new map definition is loaded (actors rebuild). */
	int32 GetMapRevision() const { return MapRevision; }

	// FTickableGameObject: keeps the connection alive when no player controller pumps it.
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UTfpsNetSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return !IsTemplate() && Core != nullptr; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override { return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional; }

private:
	void HandleEvents();
	void PushEvent(const FString& Text);

	FTfpsSocketTransport Transport;
	std::unique_ptr<tfps::ClientCore> Core;
	tfps::MapDef Map;
	tfps::WeaponSet Weapons;
	tfps::GadgetSet Gadgets;
	bool bHasMap = false;
	FFlash Flash;
	TArray<FVector> PendingFlashes;
	TArray<FKillFeedEntry> KillFeed;
	FHitMarker HitMarker;
	FDamageIndicator DamageIndicator;
	TArray<tfps::FxShot> PendingShots;
	TArray<uint32> PendingDestroyedCells;
	TArray<TPair<FVector, float>> PendingExplosions;
	bool bDeterminismOk = true;
	int32 MapRevision = 0;
	uint64 LastPumpFrame = 0;
	ETfpsConnectionStatus Status = ETfpsConnectionStatus::Offline;
	FString StatusText = TEXT("Offline");
	FString ServerAddress;
	FString LocalName;
	TArray<FString> RecentEvents;
};
