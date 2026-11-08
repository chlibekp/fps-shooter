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

	// Weapon and ammo, or the gadget in hand (bottom right).
	const tfps::WeaponState& Ws = Core.Weapon();
	const float AX = W - 290.0f * Scale;
	const float AY = H - 86.0f * Scale;
	DrawRect(Panel, AX - 10.0f * Scale, AY - 6.0f * Scale, 276.0f * Scale, 72.0f * Scale);
	// Gadget slots (above the weapon panel): [3] name xN  [4] name xN.
	for (int32 G = 0; G < tfps::kGadgetSlots; ++G)
	{
		const tfps::GadgetStock Stock = Core.LocalGadget(G);
		const tfps::GadgetDef* GDef = Net.GetGadgets().Get(Stock.Id);
		if (GDef == nullptr)
		{
			continue;
		}
		const bool bHeld = Ws.Active == tfps::kWeaponSlots + G;
		DrawText(FString::Printf(TEXT("[%d] %s x%d"), tfps::kWeaponSlots + G + 1, UTF8_TO_TCHAR(GDef->Name.c_str()), static_cast<int32>(Stock.Count)),
			bHeld ? Text : (Stock.Count == 0 ? FLinearColor(0.5f, 0.3f, 0.3f, 0.8f) : Dim), AX, AY - (28.0f + 18.0f * (tfps::kGadgetSlots - 1 - G)) * Scale, Small, Scale);
	}
	if (tfps::IsGadgetSlot(Ws.Active))
	{
		const tfps::GadgetStock Stock = Core.LocalGadget(Ws.Active - tfps::kWeaponSlots);
		const tfps::GadgetDef* GDef = Net.GetGadgets().Get(Stock.Id);
		DrawText(FString::Printf(TEXT("x%d"), static_cast<int32>(Stock.Count)), Stock.Count == 0 ? FLinearColor(0.9f, 0.3f, 0.2f) : Text, AX, AY, Font, Scale * 1.6f);
		if (GDef != nullptr)
		{
			const TCHAR* Hint = TEXT("[LMB] throw   [RMB+LMB] lob");
			switch (GDef->Kind)
			{
			case tfps::EGadgetKind::Breach: Hint = TEXT("[LMB] place   [RMB+LMB] detonate"); break;
			case tfps::EGadgetKind::Drone: Hint = TEXT("[LMB] deploy (then drive it)"); break;
			case tfps::EGadgetKind::Camera: Hint = TEXT("[LMB] place on a wall (then view it)"); break;
			case tfps::EGadgetKind::MotionSensor:
			case tfps::EGadgetKind::Jammer: Hint = TEXT("[LMB] place"); break;
			default: break;
			}
			DrawText(UTF8_TO_TCHAR(GDef->Name.c_str()), Text, AX + 70.0f * Scale, AY + 12.0f * Scale, Font, Scale);
			DrawText(Hint, Dim, AX, AY + 42.0f * Scale, Small, Scale);
		}
	}
	else
	{
	const tfps::WeaponDef* Def = Core.LoadoutWeapon(Ws.Active);
	const tfps::SlotAmmo& Ammo = Ws.Slots[Ws.Active];
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
	}

	// Spotted enemies: diamond markers at the spotted position (projected with
	// the current view; the pose is the pinned spot, not live tracking).
	const ATfpsPlayerController* SpotPC = Cast<ATfpsPlayerController>(GetOwningPlayerController());
	if (SpotPC != nullptr)
	{
		const tfps::GadgetEntityState* Drone = Net.GetControlledDevice();
		const FRotator ViewRot = SpotPC->GetControlRotation();
		FVector Origin;
		if (Drone != nullptr)
		{
			const tfps::Vec3 D = Drone->Position();
			Origin = FVector(D.X, D.Y, D.Z + (Net.KindOf(*Drone) == tfps::EGadgetKind::Drone ? 0.1f : 0.0f));
		}
		else
		{
			const tfps::Vec3 E = Core.Predicted().EyePosition(tfps::QuantizeYawDeg(static_cast<float>(ViewRot.Yaw)));
			Origin = FVector(E.X, E.Y, E.Z);
		}
		const float FovDeg = 90.0f * (Drone != nullptr ? 1.1f : FMath::Lerp(1.0f, 0.78f, Core.Predicted().Aim));
		const float Yaw = FMath::DegreesToRadians(static_cast<float>(ViewRot.Yaw));
		const float Pitch = FMath::DegreesToRadians(static_cast<float>(FRotator::NormalizeAxis(ViewRot.Pitch)));
		const FVector Fwd(FMath::Cos(Pitch) * FMath::Cos(Yaw), FMath::Cos(Pitch) * FMath::Sin(Yaw), FMath::Sin(Pitch));
		const FVector Right(-FMath::Sin(Yaw), FMath::Cos(Yaw), 0.0f);
		const FVector Up(-FMath::Sin(Pitch) * FMath::Cos(Yaw), -FMath::Sin(Pitch) * FMath::Sin(Yaw), FMath::Cos(Pitch));
		const float Focal = CX / FMath::Tan(FMath::DegreesToRadians(FovDeg * 0.5f));
		for (const UTfpsNetSubsystem::FSpotMarker& M : Net.GetSpotMarkers())
		{
			const float Left = static_cast<float>(M.Until - Now);
			if (Left <= 0.0f)
			{
				continue;
			}
			const FVector Rel = (M.PosMeters + FVector(0.0f, 0.0f, 1.2f)) - Origin;
			const float Z = static_cast<float>(FVector::DotProduct(Rel, Fwd));
			if (Z < 0.2f)
			{
				continue; // behind the camera
			}
			const float SX = CX + static_cast<float>(FVector::DotProduct(Rel, Right)) / Z * Focal;
			const float SY = CY - static_cast<float>(FVector::DotProduct(Rel, Up)) / Z * Focal;
			const float R = 9.0f * Scale;
			const FLinearColor C(1.0f, 0.55f, 0.1f, FMath::Clamp(Left, 0.0f, 1.0f));
			DrawLine(SX, SY - R, SX + R, SY, C, 2.0f * Scale);
			DrawLine(SX + R, SY, SX, SY + R, C, 2.0f * Scale);
			DrawLine(SX, SY + R, SX - R, SY, C, 2.0f * Scale);
			DrawLine(SX - R, SY, SX, SY - R, C, 2.0f * Scale);
			DrawText(Net.PlayerName(M.Target), C, SX + R + 4.0f * Scale, SY - R, Small, Scale);
		}
		// Objective sites (round modes): name and distance at the site's centre;
		// the planted device blinks red.
		const tfps::MapDef* Map = Net.GetMap();
		const tfps::MatchInfo& MI = Core.Match();
		if (Map != nullptr && Core.HasMatch() && MI.Mode == 2 && MI.Phase != tfps::MatchPhase::Warmup)
		{
			auto Project = [&](const FVector& P, float& OX, float& OY) -> bool
			{
				const FVector Rel = P - Origin;
				const float Z = static_cast<float>(FVector::DotProduct(Rel, Fwd));
				if (Z < 0.2f) { return false; }
				OX = CX + static_cast<float>(FVector::DotProduct(Rel, Right)) / Z * Focal;
				OY = CY - static_cast<float>(FVector::DotProduct(Rel, Up)) / Z * Focal;
				return true;
			};
			for (size_t I = 0; I < Map->Objectives.size(); ++I)
			{
				const tfps::Aabb& B = Map->Objectives[I].Bounds;
				const FVector Centre((B.Min.X + B.Max.X) * 0.5f, (B.Min.Y + B.Max.Y) * 0.5f, B.Min.Z + 1.0f);
				float SX = 0.0f, SY = 0.0f;
				if (!Project(Centre, SX, SY)) { continue; }
				const bool bDevice = MI.DeviceSite == I;
				const bool bBlink = bDevice && FMath::Fmod(static_cast<float>(Now), 0.5f) < 0.25f;
				const FLinearColor C = bDevice ? (bBlink ? FLinearColor(1.0f, 0.1f, 0.05f, 1.0f) : FLinearColor(0.6f, 0.05f, 0.03f, 1.0f))
											   : FLinearColor(0.95f, 0.8f, 0.3f, 0.85f);
				const float Dist = static_cast<float>((Centre - Origin).Size());
				DrawText(FString::Printf(TEXT("%s  %.0f m"), UTF8_TO_TCHAR(Map->Objectives[I].Name.c_str()), Dist), C, SX - 40.0f * Scale, SY, Small, Scale);
			}
		}
	}

	DrawMatchHud(Net, Core, Scale);

	// Drone / camera view overlay.
	if (const tfps::GadgetEntityState* Device = Net.GetControlledDevice())
	{
		const bool bCamera = Net.KindOf(*Device) == tfps::EGadgetKind::Camera;
		const FLinearColor Ov(0.55f, 0.95f, 0.6f, 0.85f);
		const float M = 40.0f * Scale;
		const float L = 60.0f * Scale;
		DrawLine(M, M, M + L, M, Ov, 2.0f * Scale); DrawLine(M, M, M, M + L, Ov, 2.0f * Scale);
		DrawLine(W - M, M, W - M - L, M, Ov, 2.0f * Scale); DrawLine(W - M, M, W - M, M + L, Ov, 2.0f * Scale);
		DrawLine(M, H - M, M + L, H - M, Ov, 2.0f * Scale); DrawLine(M, H - M, M, H - M - L, Ov, 2.0f * Scale);
		DrawLine(W - M, H - M, W - M - L, H - M, Ov, 2.0f * Scale); DrawLine(W - M, H - M, W - M, H - M - L, Ov, 2.0f * Scale);
		DrawText(bCamera ? TEXT("CAMERA") : TEXT("DRONE"), Ov, M + 10.0f * Scale, M + 10.0f * Scale, Font, Scale);
		DrawText(bCamera ? TEXT("[mouse] look   [LMB] spot   [1/2] back to body") : TEXT("[WASD] drive   [Space] hop   [LMB] spot   [1/2] back to body"), Ov, M + 10.0f * Scale,
			H - M - 30.0f * Scale, Small, Scale);
	}

	// Hold-USE actions: wall reinforcement count, and a progress bar while holding USE.
	if (Core.Reinforcements() > 0 || Core.ReinforceProgress() > 0.0f)
	{
		DrawText(FString::Printf(TEXT("Reinforcements x%d  [hold F on a wall]"), static_cast<int32>(Core.Reinforcements())), Dim, 24.0f * Scale,
			H - 120.0f * Scale, Small, Scale);
	}
	if (Core.ReinforceProgress() > 0.0f)
	{
		const float RW = 220.0f * Scale;
		DrawRect(FLinearColor(0.1f, 0.1f, 0.1f, 0.7f), CX - RW * 0.5f, CY + 60.0f * Scale, RW, 8.0f * Scale);
		DrawRect(FLinearColor(0.6f, 0.75f, 0.9f, 0.95f), CX - RW * 0.5f, CY + 60.0f * Scale, RW * Core.ReinforceProgress(), 8.0f * Scale);
		const uint8 Act = Core.Action();
		const TCHAR* Label = Act == 2 ? TEXT("PLANTING") : (Act == 3 ? TEXT("DEFUSING") : TEXT("REINFORCING"));
		DrawText(Label, Text, CX - 45.0f * Scale, CY + 72.0f * Scale, Small, Scale);
	}

	// Flashbang: white-out that holds, then fades (drawn under the rest of the HUD text that follows).
	const UTfpsNetSubsystem::FFlash& Fl = Net.GetFlash();
	const float FlashAge = static_cast<float>(Now - Fl.Time);
	if (FlashAge >= 0.0f && FlashAge < Fl.Duration)
	{
		const float Hold = Fl.Duration * 0.35f;
		const float Fade = FlashAge < Hold ? 1.0f : 1.0f - (FlashAge - Hold) / (Fl.Duration - Hold);
		DrawRect(FLinearColor(1.0f, 1.0f, 0.97f, FMath::Clamp(Fl.Intensity * 1.2f, 0.0f, 1.0f) * Fade), 0.0f, 0.0f, W, H);
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

void ATfpsHUD::DrawMatchHud(const UTfpsNetSubsystem& Net, const tfps::ClientCore& Core, float Scale)
{
	if (!Core.HasMatch())
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	UFont* Small = GEngine->GetSmallFont();
	const double Now = FPlatformTime::Seconds();
	const tfps::MatchInfo& M = Core.Match();
	const float W = Canvas->ClipX;
	const float CX = W * 0.5f;
	const uint8 Mine = Core.Squad();
	const FLinearColor Ours(0.35f, 0.7f, 1.0f, 0.95f);
	const FLinearColor Theirs(1.0f, 0.45f, 0.3f, 0.95f);
	const FLinearColor Text(0.92f, 0.93f, 0.90f, 0.95f);
	const float Left = Core.MatchTimeLeft(Now);
	const int32 Mins = static_cast<int32>(Left) / 60;
	const int32 Secs = static_cast<int32>(Left) % 60;
	const TCHAR* ModeName = M.Mode == 2 ? TEXT("DEFUSE") : (M.Mode == 1 ? TEXT("ELIMINATION") : TEXT("TEAM DEATHMATCH"));
	const TCHAR* PhaseName = TEXT("");
	switch (M.Phase)
	{
	case tfps::MatchPhase::Warmup: PhaseName = TEXT("WARMUP"); break;
	case tfps::MatchPhase::Prep: PhaseName = TEXT("PREPARATION"); break;
	case tfps::MatchPhase::Action: PhaseName = M.DeviceSite != 255 ? TEXT("DEVICE PLANTED") : TEXT(""); break;
	case tfps::MatchPhase::RoundEnd: PhaseName = TEXT("ROUND OVER"); break;
	default: PhaseName = TEXT("MATCH OVER"); break;
	}
	// Top centre: scores (ours left), timer, round and phase.
	const float Y = 14.0f * Scale;
	DrawRect(FLinearColor(0.02f, 0.025f, 0.03f, 0.6f), CX - 150.0f * Scale, Y - 4.0f * Scale, 300.0f * Scale, 64.0f * Scale);
	DrawText(FString::Printf(TEXT("%d"), static_cast<int32>(M.Score[Mine])), Ours, CX - 130.0f * Scale, Y, Font, Scale * 1.5f);
	DrawText(FString::Printf(TEXT("%d"), static_cast<int32>(M.Score[Mine ^ 1])), Theirs, CX + 105.0f * Scale, Y, Font, Scale * 1.5f);
	const FLinearColor TimerColor = M.DeviceSite != 255 ? FLinearColor(1.0f, 0.2f, 0.1f, 1.0f) : Text;
	DrawText(FString::Printf(TEXT("%d:%02d"), Mins, Secs), TimerColor, CX - 28.0f * Scale, Y, Font, Scale * 1.3f);
	const FString Sub = M.Mode == 0 ? FString::Printf(TEXT("%s  first to %d"), ModeName, static_cast<int32>(M.RoundsToWin))
									: FString::Printf(TEXT("%s  round %d  %s  %s"), ModeName, static_cast<int32>(M.Round), Core.Side() == 0 ? TEXT("ATTACK") : TEXT("DEFEND"), PhaseName);
	DrawText(Sub, Text, CX - 140.0f * Scale, Y + 38.0f * Scale, Small, Scale);
	// Round / match result banner.
	if ((M.Phase == tfps::MatchPhase::RoundEnd || M.Phase == tfps::MatchPhase::MatchOver) && M.LastWinner != 255)
	{
		const bool bWon = M.LastWinner == Mine;
		const TCHAR* Why = TEXT("");
		switch (M.LastReason)
		{
		case 1: Why = TEXT("enemy eliminated"); break;
		case 2: Why = TEXT("time ran out"); break;
		case 3: Why = TEXT("device detonated"); break;
		case 4: Why = TEXT("device defused"); break;
		case 5: Why = TEXT("score limit reached"); break;
		default: break;
		}
		const TCHAR* What = M.Phase == tfps::MatchPhase::MatchOver ? (bWon ? TEXT("VICTORY") : TEXT("DEFEAT")) : (bWon ? TEXT("ROUND WON") : TEXT("ROUND LOST"));
		const float BY = Canvas->ClipY * 0.28f;
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), 0.0f, BY - 10.0f * Scale, W, 70.0f * Scale);
		DrawText(What, bWon ? Ours : Theirs, CX - 90.0f * Scale, BY, Font, Scale * 1.8f);
		DrawText(Why, Text, CX - 60.0f * Scale, BY + 40.0f * Scale, Small, Scale);
	}
	(void)Net;
}
