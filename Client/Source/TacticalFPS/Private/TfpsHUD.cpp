// Copyright TacticalFPS project.
#include "TfpsHUD.h"
#include "TacticalFPS.h"
#include "TfpsNetSubsystem.h"
#include "TfpsPlayerController.h"
#include "TfpsLobbySubsystem.h"

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
	// Front-end menu (main menu / lobby) while not in a match.
	const UTfpsLobbySubsystem* Lobby = GetGameInstance()->GetSubsystem<UTfpsLobbySubsystem>();
	const ATfpsPlayerController* MenuPC = Cast<ATfpsPlayerController>(GetOwningPlayerController());
	const bool bMenuOpen = MenuPC != nullptr && MenuPC->GetMenu().IsOpen();
	if (bMenuOpen && Net->GetStatus() == ETfpsConnectionStatus::Offline)
	{
		DrawMenu(MenuPC->GetMenu(), Lobby, false);
		return;
	}
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

	// Pause menu over the match.
	if (bMenuOpen)
	{
		DrawMenu(MenuPC->GetMenu(), Lobby, true);
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
		const ATfpsPlayerController* FovPC = Cast<ATfpsPlayerController>(GetOwningPlayerController());
		const float BaseFov = FovPC != nullptr ? FovPC->GetFieldOfView() : 90.0f;
		const float FovDeg = BaseFov * (Drone != nullptr ? 1.1f : FMath::Lerp(1.0f, 0.78f, Core.Predicted().Aim));
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

void ATfpsHUD::DrawMenu(const tfps::ui::Menu& Menu, const UTfpsLobbySubsystem* Lobby, bool bOverGame)
{
	UFont* Font = GEngine->GetMediumFont();
	UFont* Small = GEngine->GetSmallFont();
	// The menu lays out in a virtual 1920x1080 space, scaled to fit and centred.
	const float S = FMath::Min(Canvas->ClipX / tfps::ui::kVirtualWidth, Canvas->ClipY / tfps::ui::kVirtualHeight);
	const float OX = (Canvas->ClipX - tfps::ui::kVirtualWidth * S) * 0.5f;
	const float OY = (Canvas->ClipY - tfps::ui::kVirtualHeight * S) * 0.5f;
	auto PX = [&](float X) { return OX + X * S; };
	auto PY = [&](float Y) { return OY + Y * S; };
	const FLinearColor Text(0.92f, 0.93f, 0.90f, 0.97f);
	const FLinearColor Dim(0.55f, 0.58f, 0.56f, 0.9f);
	const FLinearColor Accent(0.95f, 0.66f, 0.18f, 1.0f);
	const FLinearColor Ours(0.35f, 0.7f, 1.0f, 0.95f);
	const FLinearColor Theirs(1.0f, 0.45f, 0.3f, 0.95f);
	const FLinearColor Error(1.0f, 0.4f, 0.3f, 1.0f);

	// Backdrop: opaque over the empty front-end world, dimmed over a match.
	DrawRect(bOverGame ? FLinearColor(0.0f, 0.0f, 0.0f, 0.55f) : FLinearColor(0.035f, 0.04f, 0.045f, 1.0f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);
	if (!bOverGame)
	{
		// A thin accent rule and the game name, top left.
		DrawRect(Accent, PX(120.0f), PY(96.0f), 6.0f * S, 40.0f * S);
		DrawText(TEXT("TACTICAL FPS"), Text, PX(140.0f), PY(98.0f), Font, S * 1.6f);
	}

	const tfps::ui::Rect P = Menu.PanelRect();
	DrawRect(FLinearColor(0.06f, 0.07f, 0.08f, 0.92f), PX(P.X), PY(P.Y), P.W * S, P.H * S);
	DrawRect(Accent, PX(P.X), PY(P.Y), P.W * S, 3.0f * S);
	DrawText(UTF8_TO_TCHAR(Menu.Title()), Text, PX(P.X + 24.0f), PY(P.Y + 26.0f), Font, S * 1.3f);

	const std::vector<tfps::ui::Item>& Items = Menu.Items();
	for (int32 I = 0; I < static_cast<int32>(Items.size()); ++I)
	{
		const tfps::ui::Item& It = Items[static_cast<size_t>(I)];
		const tfps::ui::Rect R = Menu.ItemRect(I);
		const bool bFocus = I == Menu.Focus();
		const float TextScale = S * FMath::Clamp(R.H / 42.0f, 0.75f, 1.0f);
		const float TY = PY(R.Y + R.H * 0.5f - 9.0f * (R.H / 42.0f));
		if (bFocus)
		{
			DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.08f), PX(R.X), PY(R.Y), R.W * S, R.H * S);
			DrawRect(Accent, PX(R.X), PY(R.Y), 4.0f * S, R.H * S);
		}
		const FLinearColor LabelColor = !It.bEnabled ? Dim : (bFocus ? Text : FLinearColor(0.8f, 0.82f, 0.8f, 0.95f));
		DrawText(UTF8_TO_TCHAR(It.Label.c_str()), LabelColor, PX(R.X + 16.0f), TY, Small, TextScale);
		const float VX = R.X + R.W * 0.5f;
		switch (It.Kind)
		{
		case tfps::ui::ItemKind::Button:
			break;
		case tfps::ui::ItemKind::Slider:
		{
			const float BarW = R.W * 0.5f - 90.0f;
			const float BarY = R.Y + R.H * 0.5f - 3.0f;
			DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.15f), PX(VX), PY(BarY), BarW * S, 6.0f * S);
			DrawRect(bFocus ? Accent : Text, PX(VX), PY(BarY), BarW * FMath::Clamp(It.Fraction, 0.0f, 1.0f) * S, 6.0f * S);
			DrawText(UTF8_TO_TCHAR(It.Value.c_str()), LabelColor, PX(VX + BarW + 14.0f), TY, Small, TextScale);
			break;
		}
		case tfps::ui::ItemKind::Text:
		{
			DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), PX(VX - 8.0f), PY(R.Y + 4.0f), (R.W * 0.5f) * S, (R.H - 8.0f) * S);
			// Blinking caret while typing.
			const bool bCaret = bFocus && FMath::Fmod(static_cast<float>(FPlatformTime::Seconds()), 1.0f) < 0.55f;
			const FString Value = FString(UTF8_TO_TCHAR(It.Value.c_str())) + (bCaret ? TEXT("_") : TEXT(""));
			DrawText(Value, LabelColor, PX(VX), TY, Small, TextScale);
			break;
		}
		case tfps::ui::ItemKind::Choice:
		{
			const FString Value = It.bEnabled ? FString::Printf(TEXT("<  %s  >"), UTF8_TO_TCHAR(It.Value.c_str())) : FString(UTF8_TO_TCHAR(It.Value.c_str()));
			DrawText(Value, It.bEnabled && bFocus ? Accent : LabelColor, PX(VX), TY, Small, TextScale);
			break;
		}
		case tfps::ui::ItemKind::Toggle:
		case tfps::ui::ItemKind::KeyBind:
			DrawText(UTF8_TO_TCHAR(It.Value.c_str()), It.bEnabled && bFocus ? Accent : LabelColor, PX(VX), TY, Small, TextScale);
			break;
		}
	}

	// Hints along the bottom of the panel.
	const TCHAR* Hint = Menu.IsCapturingKey() ? TEXT("Press the new key  -  Esc cancels")
		: Menu.IsEditingText()               ? TEXT("Type  -  Enter confirms  -  Up/Down moves")
											 : TEXT("Arrows / mouse select  -  Left/Right change  -  Enter confirms  -  Esc back");
	DrawText(Hint, Dim, PX(P.X + 24.0f), PY(P.Y + P.H + 14.0f), Small, S);

	// Lobby screen: invite code, squads and status on the left.
	if (Menu.Current() == tfps::ui::Screen::Lobby && Lobby != nullptr)
	{
		const UTfpsLobbySubsystem::FView& V = Lobby->GetView();
		const float LX = 240.0f;
		float LY = P.Y;
		DrawRect(FLinearColor(0.06f, 0.07f, 0.08f, 0.92f), PX(LX), PY(LY), 700.0f * S, P.H * S);
		DrawRect(Accent, PX(LX), PY(LY), 700.0f * S, 3.0f * S);
		LY += 26.0f;
		DrawText(TEXT("INVITE CODE"), Dim, PX(LX + 24.0f), PY(LY), Small, S);
		LY += 28.0f;
		DrawText(V.Code, Accent, PX(LX + 24.0f), PY(LY), Font, S * 2.2f);
		LY += 80.0f;
		DrawText(TEXT("Share this code; friends join with \"Join with invite code\"."), Dim, PX(LX + 24.0f), PY(LY), Small, S);
		LY += 50.0f;
		float RowY[2] = {LY + 34.0f, LY + 34.0f};
		for (int32 Squad = 0; Squad < 2; ++Squad)
		{
			DrawText(Squad == 0 ? TEXT("SQUAD A") : TEXT("SQUAD B"), Squad == 0 ? Ours : Theirs, PX(LX + 24.0f + Squad * 330.0f), PY(LY), Small, S * 1.1f);
		}
		for (const UTfpsLobbySubsystem::FMember& M : V.Members)
		{
			const int32 Sq = FMath::Clamp(M.Squad, 0, 1);
			const float RX = LX + 24.0f + Sq * 330.0f;
			DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, M.Id == V.You ? 0.08f : 0.03f), PX(RX - 6.0f), PY(RowY[Sq] - 4.0f), 310.0f * S, 30.0f * S);
			DrawText(M.Name, M.Id == V.You ? Text : FLinearColor(0.8f, 0.82f, 0.8f, 0.95f), PX(RX), PY(RowY[Sq]), Small, S);
			const FString Tag = M.Id == V.Host ? TEXT("HOST") : (M.bReady ? TEXT("READY") : TEXT("-"));
			DrawText(Tag, M.bReady || M.Id == V.Host ? Accent : Dim, PX(RX + 230.0f), PY(RowY[Sq]), Small, S);
			RowY[Sq] += 36.0f;
		}
		LY = FMath::Max(RowY[0], RowY[1]) + 20.0f;
		const FString Status = V.Status == TEXT("in_game") ? FString::Printf(TEXT("Match running on %s"), *V.GameAddress)
			: Lobby->IsHost() ? FString(TEXT("Start the match when everyone is ready."))
							  : FString(TEXT("Waiting for the host to start."));
		DrawText(Status, Dim, PX(LX + 24.0f), PY(LY), Small, S);
	}
	if (Lobby != nullptr && !Lobby->GetLastError().IsEmpty())
	{
		DrawText(Lobby->GetLastError(), Error, PX(P.X + 24.0f), PY(P.Y + P.H + 40.0f), Small, S);
	}
	if (!bOverGame && Lobby != nullptr && !Lobby->IsConfigured() && Menu.Current() == tfps::ui::Screen::Main)
	{
		DrawText(TEXT("No lobby service set (Settings > Lobby service): hosting and invite codes are off."), Dim,
			PX(P.X + 24.0f), PY(P.Y + P.H + 40.0f), Small, S);
	}
}
