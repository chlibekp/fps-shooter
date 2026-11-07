// Copyright TacticalFPS project.
#include "TfpsHUD.h"
#include "TacticalFPS.h"
#include "TfpsNetSubsystem.h"
#include "TfpsPlayerController.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"

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
		float RY = 210.0f * Scale;
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

	if (bConnected)
	{
		DrawCombatHud(*Net, *Core, Scale);
	}

	// Event feed, above the health bar.
	float EY = Canvas->ClipY - 110.0f * Scale - Net->GetRecentEvents().Num() * Line;
	for (const FString& E : Net->GetRecentEvents())
	{
		DrawText(E, Dim, X, EY, Font, Scale);
		EY += Line;
	}
}

void ATfpsHUD::DrawCombatHud(const UTfpsNetSubsystem& Net, const tfps::ClientCore& Core, float Scale)
{
	UFont* Font = GEngine->GetMediumFont();
	UFont* Small = GEngine->GetSmallFont();
	const double Now = FPlatformTime::Seconds();
	const float W = Canvas->ClipX;
	const float H = Canvas->ClipY;
	const float CX = W * 0.5f;
	const float CY = H * 0.5f;
	const FLinearColor Text(0.92f, 0.93f, 0.90f, 0.95f);
	const FLinearColor Dim(0.65f, 0.68f, 0.66f, 0.85f);
	const FLinearColor Panel(0.02f, 0.025f, 0.03f, 0.55f);

	// Health (bottom left).
	const float HealthFrac = FMath::Clamp(Core.Health() / 100.0f, 0.0f, 1.0f);
	const float BX = 24.0f * Scale;
	const float BY = H - 56.0f * Scale;
	const float BW = 260.0f * Scale;
	const float BH = 14.0f * Scale;
	DrawRect(Panel, BX - 6.0f * Scale, BY - 30.0f * Scale, BW + 12.0f * Scale, BH + 42.0f * Scale);
	DrawText(FString::Printf(TEXT("%d"), static_cast<int32>(Core.Health())), Text, BX, BY - 26.0f * Scale, Font, Scale);
	DrawRect(FLinearColor(0.15f, 0.15f, 0.15f, 0.8f), BX, BY, BW, BH);
	const FLinearColor HealthColor = HealthFrac > 0.35f ? FLinearColor(0.85f, 0.88f, 0.85f, 0.95f) : FLinearColor(0.9f, 0.25f, 0.18f, 0.95f);
	DrawRect(HealthColor, BX, BY, BW * HealthFrac, BH);

	// Weapon and ammo (bottom right).
	const tfps::WeaponState& Ws = Core.Weapon();
	const tfps::WeaponDef* Def = Core.LoadoutWeapon(Ws.Active);
	const tfps::SlotAmmo& Ammo = Ws.Slots[Ws.Active];
	const float AX = W - 290.0f * Scale;
	const float AY = H - 86.0f * Scale;
	DrawRect(Panel, AX - 10.0f * Scale, AY - 6.0f * Scale, 276.0f * Scale, 72.0f * Scale);
	const int32 InGun = Ammo.Ammo + (Ammo.bChambered ? 1 : 0);
	DrawText(FString::Printf(TEXT("%d"), InGun), InGun == 0 ? FLinearColor(0.9f, 0.3f, 0.2f) : Text, AX, AY, Font, Scale * 1.6f);
	DrawText(FString::Printf(TEXT("/ %d"), static_cast<int32>(Ammo.Reserve)), Dim, AX + 70.0f * Scale, AY + 12.0f * Scale, Font, Scale);
	if (Def != nullptr)
	{
		const TCHAR* Mode = Def->FireMode == tfps::EFireMode::Auto ? TEXT("AUTO") : (Def->FireMode == tfps::EFireMode::Semi ? TEXT("SEMI") : TEXT("BURST"));
		DrawText(FString::Printf(TEXT("%s   %s   [%d]"), UTF8_TO_TCHAR(Def->Name.c_str()), Mode, static_cast<int32>(Ws.Active) + 1), Dim, AX,
			AY + 42.0f * Scale, Small, Scale);
	}
	if (Ws.ReloadTimer > 0.0f)
	{
		DrawText(TEXT("RELOADING"), Text, CX - 40.0f * Scale, CY + 40.0f * Scale, Small, Scale);
	}
	else if (InGun == 0 && Ammo.Reserve > 0)
	{
		DrawText(TEXT("RELOAD [R]"), FLinearColor(0.9f, 0.6f, 0.2f), CX - 40.0f * Scale, CY + 40.0f * Scale, Small, Scale);
	}

	// Hit marker: diagonal ticks around the crosshair (red for kills).
	const UTfpsNetSubsystem::FHitMarker& HM = Net.GetHitMarker();
	const float HitAge = static_cast<float>(Now - HM.Time);
	if (HitAge < 0.25f)
	{
		const float A = 1.0f - HitAge / 0.25f;
		const FLinearColor C = HM.bKill ? FLinearColor(0.95f, 0.15f, 0.1f, A) : FLinearColor(1.0f, 1.0f, 1.0f, A);
		const float In = 7.0f * Scale;
		const float Out = (HM.bHeadshot ? 15.0f : 12.0f) * Scale;
		DrawLine(CX - In, CY - In, CX - Out, CY - Out, C, 2.0f * Scale);
		DrawLine(CX + In, CY - In, CX + Out, CY - Out, C, 2.0f * Scale);
		DrawLine(CX - In, CY + In, CX - Out, CY + Out, C, 2.0f * Scale);
		DrawLine(CX + In, CY + In, CX + Out, CY + Out, C, 2.0f * Scale);
	}

	// Damage direction: a red arc segment on a circle around the crosshair.
	const UTfpsNetSubsystem::FDamageIndicator& DI = Net.GetDamageIndicator();
	const float DmgAge = static_cast<float>(Now - DI.Time);
	if (DmgAge < 1.2f && GetOwningPlayerController() != nullptr)
	{
		const tfps::Vec3 Me = Core.Predicted().Pos;
		const float ToYaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(DI.FromMeters.Y - Me.Y), static_cast<float>(DI.FromMeters.X - Me.X)));
		const float Rel = FMath::DegreesToRadians(ToYaw - static_cast<float>(GetOwningPlayerController()->GetControlRotation().Yaw));
		const float R = 90.0f * Scale;
		const FLinearColor C(0.85f, 0.08f, 0.05f, 1.0f - DmgAge / 1.2f);
		for (int32 I = -3; I < 3; ++I)
		{
			const float A0 = Rel + I * 0.06f;
			const float A1 = Rel + (I + 1) * 0.06f;
			// Screen space: forward = up, right = right.
			DrawLine(CX + FMath::Sin(A0) * R, CY - FMath::Cos(A0) * R, CX + FMath::Sin(A1) * R, CY - FMath::Cos(A1) * R, C, 4.0f * Scale);
		}
	}

	// Kill feed (top right).
	float KY = 24.0f * Scale;
	for (const UTfpsNetSubsystem::FKillFeedEntry& E : Net.GetKillFeed())
	{
		const float KW = 360.0f * Scale;
		DrawRect(E.bInvolvesMe ? FLinearColor(0.35f, 0.08f, 0.06f, 0.6f) : Panel, W - KW - 24.0f * Scale, KY - 2.0f * Scale, KW, 20.0f * Scale);
		DrawText(E.Text, Text, W - KW - 16.0f * Scale, KY, Small, Scale);
		KY += 24.0f * Scale;
	}

	// Death screen.
	if (!Core.IsAlive())
	{
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f), 0.0f, 0.0f, W, H);
		DrawText(FString::Printf(TEXT("YOU WERE TAKEN DOWN  -  respawning in %.1f s"), Core.RespawnIn()), Text, CX - 220.0f * Scale, CY - 60.0f * Scale,
			Font, Scale);
	}
}
