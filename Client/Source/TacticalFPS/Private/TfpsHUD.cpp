// Copyright TacticalFPS project.
#include "TfpsHUD.h"
#include "TacticalFPS.h"
#include "TfpsNetSubsystem.h"
#include "TfpsPlayerController.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"

void ATfpsHUD::DrawHUD()
{
	Super::DrawHUD();
	if (Canvas == nullptr)
	{
		return;
	}
	const UTfpsNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UTfpsNetSubsystem>() : nullptr;
	if (Net == nullptr)
	{
		return;
	}
	UFont* Font = GEngine->GetSmallFont();
	const float Scale = FMath::Max(1.0f, Canvas->ClipY / 1080.0f);
	const FLinearColor Text(0.92f, 0.93f, 0.90f, 0.95f);
	const FLinearColor Dim(0.65f, 0.68f, 0.66f, 0.85f);
	const FLinearColor Warn(1.0f, 0.62f, 0.2f, 1.0f);

	// Crosshair: four short ticks with a centre gap.
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	const float Gap = 5.0f * Scale;
	const float Len = 7.0f * Scale;
	const FLinearColor Cross(0.95f, 0.95f, 0.95f, 0.9f);
	DrawLine(CX - Gap - Len, CY, CX - Gap, CY, Cross, Scale);
	DrawLine(CX + Gap, CY, CX + Gap + Len, CY, Cross, Scale);
	DrawLine(CX, CY - Gap - Len, CX, CY - Gap, Cross, Scale);
	DrawLine(CX, CY + Gap, CX, CY + Gap + Len, Cross, Scale);

	float Y = 24.0f * Scale;
	const float X = 24.0f * Scale;
	const float Line = 18.0f * Scale;
	const tfps::ClientCore* Core = Net->GetCore();
	const bool bConnected = Core != nullptr && Core->IsConnected();
	DrawText(Net->GetStatusText(), bConnected ? Text : Warn, X, Y, Font, Scale);
	Y += Line;
	if (!Net->DeterminismSelfTestPassed())
	{
		DrawText(TEXT("WARNING: movement determinism self-test failed (see log)"), Warn, X, Y, Font, Scale);
		Y += Line;
	}

	if (bConnected)
	{
		const tfps::ClientNetStats& S = Core->Stats();
		DrawText(FString::Printf(TEXT("Player %d  |  Health %d  |  Ping %.0f ms"), static_cast<int32>(Core->PlayerId()), static_cast<int32>(Core->Health()), S.RttMs), Text, X, Y, Font, Scale);
		Y += Line * 1.5f;
		DrawText(TEXT("PLAYERS"), Dim, X, Y, Font, Scale);
		Y += Line;
		for (const auto& KV : Core->Players())
		{
			const tfps::RemotePlayer& P = KV.second;
			const TCHAR* State = P.ConnState == tfps::EConnState::Connected ? TEXT("connected") : TEXT("interrupted");
			DrawText(FString::Printf(TEXT("%s  [team %d]  %s"), UTF8_TO_TCHAR(P.Name.c_str()), static_cast<int32>(P.Team), State),
				P.ConnState == tfps::EConnState::Connected ? Text : Warn, X, Y, Font, Scale);
			Y += Line;
		}
	}

	const ATfpsPlayerController* PC = Cast<ATfpsPlayerController>(GetOwningPlayerController());
	if (bConnected && PC != nullptr && PC->IsNetStatsVisible())
	{
		const tfps::ClientNetStats& S = Core->Stats();
		const tfps::MoveState& M = Core->Predicted();
		float RY = 24.0f * Scale;
		const float RX = Canvas->ClipX - 380.0f * Scale;
		const TArray<FString> Lines = {
			TEXT("NET (F3)"),
			FString::Printf(TEXT("tick rate %d Hz  cmd %u  snapshot %u"), static_cast<int32>(Core->TickRate()), static_cast<uint32>(Core->CommandSeq()), static_cast<uint32>(Core->LatestSnapshotTick())),
			FString::Printf(TEXT("snapshots %llu (delta %llu)"), static_cast<uint64>(S.SnapshotsReceived), static_cast<uint64>(S.DeltaSnapshots)),
			FString::Printf(TEXT("server input buffer %d  time scale %.2f"), static_cast<int32>(S.ServerInputDepth), S.TimeScale),
			FString::Printf(TEXT("corrections %llu  last %.3f m  max %.3f m"), static_cast<uint64>(S.Corrections), S.LastCorrectionM, S.MaxCorrectionM),
			FString::Printf(TEXT("pos %.2f %.2f %.2f  %s%s"), M.Pos.X, M.Pos.Y, M.Pos.Z, M.bOnGround ? TEXT("ground") : TEXT("air"), M.bCrouched ? TEXT(" crouched") : TEXT("")),
			FString::Printf(TEXT("up %.1f kB  down %.1f kB"), static_cast<double>(S.BytesSent) / 1024.0, static_cast<double>(S.BytesReceived) / 1024.0),
		};
		for (const FString& L : Lines)
		{
			DrawText(L, Dim, RX, RY, Font, Scale);
			RY += Line;
		}
	}

	// Event feed, bottom left.
	float EY = Canvas->ClipY - 40.0f * Scale - Net->GetRecentEvents().Num() * Line;
	for (const FString& E : Net->GetRecentEvents())
	{
		DrawText(E, Dim, X, EY, Font, Scale);
		EY += Line;
	}
}
