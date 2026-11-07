// Copyright TacticalFPS project.
#include "TfpsNetSubsystem.h"
#include "TacticalFPS.h"
#include "TfpsMapLoader.h"

#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "NetCore/TfpsDeterminism.h"

void UTfpsNetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bDeterminismOk = tfps::DeterminismSelfTestPasses();
	if (bDeterminismOk)
	{
		UE_LOG(LogTfps, Log, TEXT("Movement determinism self-test passed"));
	}
	else
	{
		UE_LOG(LogTfps, Error, TEXT("Movement determinism self-test FAILED (hash %016llx, expected %016llx). Client prediction will be corrected by the server constantly; check compiler floating point settings."),
			static_cast<unsigned long long>(tfps::DeterminismSelfTestHash()), static_cast<unsigned long long>(tfps::kExpectedSelfTestHash));
	}
}

void UTfpsNetSubsystem::Deinitialize()
{
	Disconnect();
	Super::Deinitialize();
}

bool UTfpsNetSubsystem::Connect(const FString& Address, const FString& PlayerName, const FString& MapName)
{
	Disconnect();
	FString Error;
	tfps::MapDef NewMap;
	if (!FTfpsMapLoader::Load(MapName, NewMap, Error))
	{
		Status = ETfpsConnectionStatus::Failed;
		StatusText = Error;
		UE_LOG(LogTfps, Error, TEXT("%s"), *Error);
		return false;
	}
	tfps::WeaponSet NewWeapons;
	if (!FTfpsMapLoader::LoadWeapons(NewWeapons, Error))
	{
		Status = ETfpsConnectionStatus::Failed;
		StatusText = Error;
		UE_LOG(LogTfps, Error, TEXT("%s"), *Error);
		return false;
	}
	tfps::GadgetSet NewGadgets;
	if (!FTfpsMapLoader::LoadGadgets(NewGadgets, Error))
	{
		Status = ETfpsConnectionStatus::Failed;
		StatusText = Error;
		UE_LOG(LogTfps, Error, TEXT("%s"), *Error);
		return false;
	}
	Map = std::move(NewMap);
	Weapons = std::move(NewWeapons);
	Gadgets = std::move(NewGadgets);
	bHasMap = true;
	++MapRevision;

	if (!Transport.Open(Address, Error))
	{
		Status = ETfpsConnectionStatus::Failed;
		StatusText = Error;
		UE_LOG(LogTfps, Error, TEXT("Connect failed: %s"), *Error);
		return false;
	}
	tfps::ClientConfig Config;
	Config.Name = TCHAR_TO_UTF8(*PlayerName.Left(24));
	Core = std::make_unique<tfps::ClientCore>(Config, Map, Weapons, Transport, FPlatformTime::Seconds());
	ServerAddress = Address;
	LocalName = PlayerName;
	Status = ETfpsConnectionStatus::Connecting;
	StatusText = FString::Printf(TEXT("Connecting to %s..."), *Address);
	UE_LOG(LogTfps, Log, TEXT("Connecting to %s as '%s'"), *Address, *PlayerName);
	return true;
}

void UTfpsNetSubsystem::Disconnect()
{
	if (Core)
	{
		Core->Disconnect();
		Core.reset();
	}
	Transport.Close();
	if (Status != ETfpsConnectionStatus::Failed)
	{
		Status = ETfpsConnectionStatus::Offline;
		StatusText = TEXT("Offline");
	}
}

void UTfpsNetSubsystem::SetLocalInput(const tfps::MoveInput& Input)
{
	LastInput = Input;
	if (Core)
	{
		Core->SetInput(Input);
	}
}

void UTfpsNetSubsystem::Pump()
{
	if (!Core || LastPumpFrame == GFrameCounter)
	{
		return;
	}
	LastPumpFrame = GFrameCounter;
	Core->Update(FPlatformTime::Seconds());
	HandleEvents();
	tfps::FxShot Shot;
	while (Core && Core->PollShot(Shot))
	{
		if (PendingShots.Num() < 256)
		{
			PendingShots.Add(Shot);
		}
	}
	uint32_t Cell = 0;
	while (Core && Core->PollDestroyedCell(Cell))
	{
		PendingDestroyedCells.Add(Cell);
	}
	const double Now = FPlatformTime::Seconds();
	KillFeed.RemoveAll([Now](const FKillFeedEntry& E) { return Now - E.Time > 6.0; });
}

FString UTfpsNetSubsystem::PlayerName(uint16 PlayerId) const
{
	if (Core)
	{
		if (PlayerId == Core->PlayerId())
		{
			return LocalName;
		}
		const auto It = Core->Players().find(PlayerId);
		if (It != Core->Players().end())
		{
			return UTF8_TO_TCHAR(It->second.Name.c_str());
		}
	}
	return FString::Printf(TEXT("Player %d"), static_cast<int32>(PlayerId));
}

void UTfpsNetSubsystem::Tick(float DeltaTime)
{
	Pump();
}

void UTfpsNetSubsystem::PushEvent(const FString& Text)
{
	UE_LOG(LogTfps, Log, TEXT("%s"), *Text);
	RecentEvents.Add(Text);
	if (RecentEvents.Num() > 8)
	{
		RecentEvents.RemoveAt(0);
	}
}

void UTfpsNetSubsystem::HandleEvents()
{
	tfps::ClientEvent Event;
	while (Core && Core->PollEvent(Event))
	{
		const FString Name = UTF8_TO_TCHAR(Event.Name.c_str());
		switch (Event.Type)
		{
		case tfps::ClientEvent::EType::Connected:
			Status = ETfpsConnectionStatus::Connected;
			StatusText = FString::Printf(TEXT("Connected to %s"), *ServerAddress);
			PushEvent(FString::Printf(TEXT("Connected as player %d"), static_cast<int32>(Event.PlayerId)));
			break;
		case tfps::ClientEvent::EType::PlayerJoined:
			if (Event.PlayerId != Core->PlayerId())
			{
				PushEvent(FString::Printf(TEXT("%s joined (team %d)"), *Name, static_cast<int32>(Event.Team)));
			}
			break;
		case tfps::ClientEvent::EType::PlayerLeft:
			PushEvent(FString::Printf(TEXT("Player %d left (%s)"), static_cast<int32>(Event.PlayerId), UTF8_TO_TCHAR(tfps::DisconnectReasonName(Event.LeftReason))));
			break;
		case tfps::ClientEvent::EType::PlayerConnState:
			PushEvent(FString::Printf(TEXT("Player %d connection: %s"), static_cast<int32>(Event.PlayerId),
				Event.ConnState == tfps::EConnState::Connected ? TEXT("restored") : TEXT("interrupted")));
			break;
		case tfps::ClientEvent::EType::HitConfirm:
			HitMarker.Time = FPlatformTime::Seconds();
			HitMarker.bHeadshot = Event.bHeadshot;
			HitMarker.bKill = Event.bKilled;
			break;
		case tfps::ClientEvent::EType::Damaged:
			DamageIndicator.Time = FPlatformTime::Seconds();
			DamageIndicator.FromMeters = FVector(Event.From.X, Event.From.Y, Event.From.Z);
			DamageIndicator.Amount = Event.Amount;
			break;
		case tfps::ClientEvent::EType::Explosion:
			if (PendingExplosions.Num() < 32)
			{
				PendingExplosions.Add(TPair<FVector, float>(FVector(Event.From.X, Event.From.Y, Event.From.Z), Event.Radius));
			}
			break;
		case tfps::ClientEvent::EType::Spotted:
		{
			FSpotMarker Marker;
			Marker.Target = Event.PlayerId;
			Marker.PosMeters = FVector(Event.From.X, Event.From.Y, Event.From.Z);
			Marker.Until = FPlatformTime::Seconds() + Event.Duration;
			SpotMarkers.RemoveAll([&Marker](const FSpotMarker& M) { return M.Target == Marker.Target; });
			SpotMarkers.Add(Marker);
			PushEvent(FString::Printf(TEXT("%s spotted %s"), *PlayerName(Event.OtherId), *PlayerName(Event.PlayerId)));
			break;
		}
		case tfps::ClientEvent::EType::Flash:
		{
			if (PendingFlashes.Num() < 16)
			{
				PendingFlashes.Add(FVector(Event.From.X, Event.From.Y, Event.From.Z));
			}
			if (Event.Intensity > 0.0f)
			{
				// Hold time scales with how strongly the flash hit (gadget "duration" at full intensity).
				float Duration = 4.0f;
				for (const tfps::GadgetDef& G : Gadgets.Gadgets)
				{
					if (G.Kind == tfps::EGadgetKind::Flash) { Duration = G.Duration; }
				}
				Flash.Time = FPlatformTime::Seconds();
				Flash.Intensity = Event.Intensity;
				Flash.Duration = FMath::Max(0.5f, Duration * Event.Intensity);
			}
			break;
		}
		case tfps::ClientEvent::EType::Kill:
		{
			const FString Item = ItemName(Event.WeaponId);
			FKillFeedEntry Entry;
			Entry.Text = FString::Printf(TEXT("%s  [%s%s]  %s"), *PlayerName(Event.OtherId), *Item,
				Event.bHeadshot ? TEXT(", headshot") : TEXT(""), *PlayerName(Event.PlayerId));
			Entry.Time = FPlatformTime::Seconds();
			Entry.bInvolvesMe = Event.OtherId == Core->PlayerId() || Event.PlayerId == Core->PlayerId();
			KillFeed.Add(Entry);
			if (KillFeed.Num() > 6)
			{
				KillFeed.RemoveAt(0);
			}
			break;
		}
		case tfps::ClientEvent::EType::Disconnected:
		{
			FString Reason;
			switch (Event.Cause)
			{
			case tfps::EDisconnectCause::Rejected: Reason = FString::Printf(TEXT("rejected: %s"), UTF8_TO_TCHAR(tfps::RejectReasonName(Event.RejectReason))); break;
			case tfps::EDisconnectCause::HandshakeTimeout: Reason = TEXT("server did not answer"); break;
			case tfps::EDisconnectCause::Timeout: Reason = TEXT("connection lost"); break;
			case tfps::EDisconnectCause::MapMismatch: Reason = TEXT("map data differs from the server's"); break;
			case tfps::EDisconnectCause::WeaponsMismatch: Reason = TEXT("weapon data differs from the server's"); break;
			case tfps::EDisconnectCause::ServerDisconnect: Reason = TEXT("disconnected by server"); break;
			default: Reason = TEXT("disconnected"); break;
			}
			Status = Event.Cause == tfps::EDisconnectCause::Local ? ETfpsConnectionStatus::Offline : ETfpsConnectionStatus::Failed;
			StatusText = Reason;
			PushEvent(Reason);
			break;
		}
		}
	}
}

FString UTfpsNetSubsystem::ItemName(uint8 ItemId) const
{
	if (ItemId & 0x80)
	{
		const tfps::GadgetDef* G = Gadgets.Get(static_cast<uint8_t>(ItemId & 0x7F));
		return G != nullptr ? FString(UTF8_TO_TCHAR(G->Name.c_str())) : FString(TEXT("Gadget"));
	}
	if (ItemId == 0)
	{
		return TEXT("Explosion");
	}
	const tfps::WeaponDef* W = Weapons.Get(ItemId);
	return W != nullptr ? FString(UTF8_TO_TCHAR(W->Name.c_str())) : FString(TEXT("?"));
}

const tfps::GadgetEntityState* UTfpsNetSubsystem::GetMyDrone() const
{
	if (!Core)
	{
		return nullptr;
	}
	const tfps::GadgetEntityState* Best = nullptr;
	for (const tfps::GadgetEntityState& G : Core->Gadgets())
	{
		const tfps::GadgetDef* Def = Gadgets.Get(G.Gadget);
		if (G.Owner == Core->PlayerId() && Def != nullptr && Def->Kind == tfps::EGadgetKind::Drone && (Best == nullptr || G.Id > Best->Id))
		{
			Best = &G;
		}
	}
	return Best;
}
