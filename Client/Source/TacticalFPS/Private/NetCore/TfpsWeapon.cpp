// Copyright TacticalFPS project. Engine-independent NetCore.
// Line-by-line mirror of Shared/Simulation/rust/src/weapon.rs.
#include "NetCore/TfpsWeapon.h"
#include "NetCore/TfpsJson.h"
#include "NetCore/TfpsFloatDeterminism.h"

#include <cmath>

namespace tfps
{
	namespace
	{
		uint32_t WeaponSeed(uint32_t Counter, uint32_t Pellet, uint32_t K)
		{
			return Hash32((Counter * 0x9E3779B1u) ^ (Pellet * 0x85EBCA77u) ^ (K * 0xC2B2AE3Du));
		}

		float WeaponStanceMult(const MoveState& Ms, float Crouch, float Prone)
		{
			return Ms.bProne ? Prone : (Ms.bCrouched ? Crouch : 1.0f);
		}

		void WeaponFinishReload(SlotAmmo& Slot, const WeaponDef& Def)
		{
			const uint16_t Need = Def.MagSize > Slot.Ammo ? static_cast<uint16_t>(Def.MagSize - Slot.Ammo) : 0;
			const uint16_t Take = Need < Slot.Reserve ? Need : Slot.Reserve;
			Slot.Ammo = static_cast<uint16_t>(Slot.Ammo + Take);
			Slot.Reserve = static_cast<uint16_t>(Slot.Reserve - Take);
			if (!Slot.bChambered && Slot.Ammo > 0)
			{
				Slot.Ammo = static_cast<uint16_t>(Slot.Ammo - 1);
				Slot.bChambered = true;
			}
		}

		void WeaponStartReload(WeaponState& Ws, const WeaponDef& Def)
		{
			const SlotAmmo& Slot = Ws.Slots[Ws.Active];
			Ws.bReloadEmpty = !Slot.bChambered;
			Ws.ReloadTimer = Ws.bReloadEmpty ? Def.ReloadEmptyTime : Def.ReloadTime;
			Ws.BurstLeft = 0;
		}

		float JF(const JsonValue& O, const char* K) { return static_cast<float>(JsonNumber(O, K, 0.0)); }
	}

	uint32_t Hash32(uint32_t X)
	{
		X ^= X >> 16;
		X *= 0x7feb352du;
		X ^= X >> 15;
		X *= 0x846ca68bu;
		X ^= X >> 16;
		return X;
	}

	float RandSigned(uint32_t H)
	{
		return static_cast<float>(H >> 8) * (1.0f / 8388608.0f) - 1.0f;
	}

	const WeaponDef* WeaponSet::Get(uint8_t Id) const
	{
		for (const WeaponDef& W : Weapons)
		{
			if (W.Id == Id) { return &W; }
		}
		return nullptr;
	}

	uint64_t WeaponSet::Hash() const
	{
		Fnv1a64 H;
		H.WriteU32(static_cast<uint32_t>(Weapons.size()));
		for (const WeaponDef& W : Weapons)
		{
			const uint8_t B[3] = {W.Id, static_cast<uint8_t>(W.FireMode), W.Pellets};
			H.Write(B, 3);
			const uint8_t M[4] = {static_cast<uint8_t>(W.MagSize), static_cast<uint8_t>(W.MagSize >> 8), static_cast<uint8_t>(W.Reserve), static_cast<uint8_t>(W.Reserve >> 8)};
			H.Write(M, 4);
			const float Vals[] = {W.Rpm, W.Damage, W.DamageMin, W.FalloffStart, W.FalloffEnd, W.Range, W.Penetration, W.HeadMult, W.LimbMult, W.ReloadTime,
				W.ReloadEmptyTime, W.AdsTime, W.EquipTime, W.SpreadHip, W.SpreadAds, W.SpreadMove, W.SpreadJump, W.SpreadCrouchMult, W.SpreadProneMult};
			for (float V : Vals) { H.WriteF32(V); }
			const RecoilDef& R = W.Recoil;
			H.WriteU32(static_cast<uint32_t>(R.Pattern.size()));
			for (const auto& P : R.Pattern) { H.WriteF32(P.first); H.WriteF32(P.second); }
			const float RV[] = {R.RandomYaw, R.RandomPitch, R.AdsMult, R.CrouchMult, R.ProneMult, R.RecoveryRate, R.RecoveryDelay, R.MaxPitch};
			for (float V : RV) { H.WriteF32(V); }
		}
		H.Write(DefaultLoadout, kWeaponSlots);
		return H.Finish();
	}

	bool ParseWeaponsJson(const std::string& Text, WeaponSet& Out, std::string& OutError)
	{
		JsonValue Root;
		if (!ParseJson(Text, Root, OutError)) { return false; }
		if (JsonNumber(Root, "version", 0) != 1.0) { OutError = "unsupported weapons file version"; return false; }
		const JsonValue* List = Root.Find("weapons");
		const JsonValue* Lo = Root.Find("default_loadout");
		if (List == nullptr || List->Kind != JsonValue::EKind::Array || Lo == nullptr || Lo->Items.size() != kWeaponSlots)
		{
			OutError = "weapons file needs 'weapons' and a 2-entry 'default_loadout'";
			return false;
		}
		WeaponSet Set;
		for (const JsonValue& J : List->Items)
		{
			WeaponDef W;
			W.Id = static_cast<uint8_t>(JsonNumber(J, "id", 0));
			if (const JsonValue* N = J.Find("name")) { W.Name = N->String; }
			if (const JsonValue* C = J.Find("class")) { W.Class = C->String; }
			const JsonValue* Mode = J.Find("fire_mode");
			const std::string ModeName = Mode != nullptr ? Mode->String : "";
			if (ModeName == "auto") { W.FireMode = EFireMode::Auto; }
			else if (ModeName == "semi") { W.FireMode = EFireMode::Semi; }
			else if (ModeName == "burst3") { W.FireMode = EFireMode::Burst3; }
			else { OutError = "unknown fire_mode '" + ModeName + "'"; return false; }
			W.Rpm = JF(J, "rpm");
			W.MagSize = static_cast<uint16_t>(JsonNumber(J, "mag_size", 0));
			W.Reserve = static_cast<uint16_t>(JsonNumber(J, "reserve", 0));
			W.Pellets = static_cast<uint8_t>(JsonNumber(J, "pellets", 1));
			W.Damage = JF(J, "damage"); W.DamageMin = JF(J, "damage_min");
			W.FalloffStart = JF(J, "falloff_start"); W.FalloffEnd = JF(J, "falloff_end"); W.Range = JF(J, "range"); W.Penetration = JF(J, "penetration");
			W.HeadMult = JF(J, "head_mult"); W.LimbMult = JF(J, "limb_mult");
			W.ReloadTime = JF(J, "reload_time"); W.ReloadEmptyTime = JF(J, "reload_empty_time");
			W.AdsTime = JF(J, "ads_time"); W.EquipTime = JF(J, "equip_time");
			W.SpreadHip = JF(J, "spread_hip"); W.SpreadAds = JF(J, "spread_ads"); W.SpreadMove = JF(J, "spread_move");
			W.SpreadJump = JF(J, "spread_jump"); W.SpreadCrouchMult = JF(J, "spread_crouch_mult"); W.SpreadProneMult = JF(J, "spread_prone_mult");
			const JsonValue* R = J.Find("recoil");
			if (R == nullptr) { OutError = "weapon without recoil"; return false; }
			if (const JsonValue* P = R->Find("pattern"))
			{
				for (const JsonValue& E : P->Items)
				{
					if (E.Items.size() != 2) { OutError = "recoil pattern entries must be [pitch, yaw]"; return false; }
					W.Recoil.Pattern.emplace_back(static_cast<float>(E.Items[0].Number), static_cast<float>(E.Items[1].Number));
				}
			}
			W.Recoil.RandomYaw = JF(*R, "random_yaw"); W.Recoil.RandomPitch = JF(*R, "random_pitch");
			W.Recoil.AdsMult = JF(*R, "ads_mult"); W.Recoil.CrouchMult = JF(*R, "crouch_mult"); W.Recoil.ProneMult = JF(*R, "prone_mult");
			W.Recoil.RecoveryRate = JF(*R, "recovery_rate"); W.Recoil.RecoveryDelay = JF(*R, "recovery_delay"); W.Recoil.MaxPitch = JF(*R, "max_pitch");
			const bool bOk = W.Id != 0 && W.Rpm > 0.0f && W.MagSize > 0 && W.Pellets >= 1 && W.Pellets <= kMaxPellets && W.AdsTime > 0.0f
				&& W.FalloffEnd > W.FalloffStart && !W.Recoil.Pattern.empty();
			if (!bOk) { OutError = "weapon '" + W.Name + "' has invalid values"; return false; }
			Set.Weapons.push_back(W);
		}
		for (int I = 0; I < kWeaponSlots; ++I)
		{
			Set.DefaultLoadout[I] = static_cast<uint8_t>(Lo->Items[static_cast<size_t>(I)].Number);
			if (Set.Get(Set.DefaultLoadout[I]) == nullptr) { OutError = "default loadout uses an unknown weapon"; return false; }
		}
		Out = std::move(Set);
		return true;
	}

	WeaponState WeaponState::New(const WeaponDef* const Loadout[kWeaponSlots])
	{
		WeaponState S;
		for (int I = 0; I < kWeaponSlots; ++I)
		{
			S.Slots[I].Ammo = static_cast<uint16_t>(Loadout[I]->MagSize - 1);
			S.Slots[I].bChambered = true;
			S.Slots[I].Reserve = Loadout[I]->Reserve;
		}
		return S;
	}

	bool WeaponState::operator==(const WeaponState& O) const
	{
		return Active == O.Active && Slots[0] == O.Slots[0] && Slots[1] == O.Slots[1] && Cooldown == O.Cooldown && ReloadTimer == O.ReloadTimer
			&& bReloadEmpty == O.bReloadEmpty && EquipTimer == O.EquipTimer && BurstLeft == O.BurstLeft && bTriggerPrev == O.bTriggerPrev
			&& ShotsFired == O.ShotsFired && RecoilPitch == O.RecoilPitch && RecoilYaw == O.RecoilYaw && RecoverTimer == O.RecoverTimer
			&& ShotCounter == O.ShotCounter;
	}

	namespace
	{
		/** A tick with a gadget in hand: no aiming, reloading or firing; movement, timers and recoil recovery continue. */
		bool SimulateHolstered(MoveState& Ms, WeaponState& Ws, const MoveInput& Input, float Dt, const CollisionWorld& World, const WeaponDef& RecoveryDef)
		{
			Ms.Aim = ApproachScalar(Ms.Aim, 0.0f, Dt / kGadgetAimTime);
			const bool bSprinted = Simulate(Ms, Input, Dt, World);
			if (Ws.EquipTimer > 0.0f)
			{
				Ws.EquipTimer -= Dt;
				if (Ws.EquipTimer < 0.0f) { Ws.EquipTimer = 0.0f; }
			}
			Ws.Cooldown -= Dt;
			if (Ws.Cooldown < 0.0f) { Ws.Cooldown = 0.0f; }
			Ws.ShotsFired = 0;
			Ws.bTriggerPrev = (Input.Buttons & Buttons::Fire) != 0;
			if (Ws.RecoverTimer > 0.0f)
			{
				Ws.RecoverTimer -= Dt;
				if (Ws.RecoverTimer < 0.0f) { Ws.RecoverTimer = 0.0f; }
			}
			else
			{
				const float Step = RecoveryDef.Recoil.RecoveryRate * Dt;
				Ws.RecoilPitch = ApproachScalar(Ws.RecoilPitch, 0.0f, Step);
				Ws.RecoilYaw = ApproachScalar(Ws.RecoilYaw, 0.0f, Step);
			}
			return bSprinted;
		}
	}

	bool SimulatePlayer(MoveState& Ms, WeaponState& Ws, const MoveInput& Input, float Dt, const CollisionWorld& World,
		const WeaponDef* const Loadout[kWeaponSlots], ShotBuffer& OutShots)
	{
		const uint16_t B = Input.Buttons;

		// 0. Weapon / gadget switching.
		const uint8_t Want = Input.Slot < kWeaponSlots + kGadgetSlots ? Input.Slot : 0;
		if (Want != Ws.Active)
		{
			Ws.Active = Want;
			Ws.EquipTimer = Want < kWeaponSlots ? Loadout[Want]->EquipTime : kGadgetEquipTime;
			Ws.ReloadTimer = 0.0f;
			Ws.BurstLeft = 0;
			Ws.ShotsFired = 0;
			if (Ws.Cooldown < 0.0f) { Ws.Cooldown = 0.0f; }
		}
		if (IsGadgetSlot(Ws.Active))
		{
			return SimulateHolstered(Ms, Ws, Input, Dt, World, *Loadout[0]);
		}
		const WeaponDef& Def = *Loadout[Ws.Active];

		// 1. ADS.
		const bool bSprintWanted = SprintIntent(Ms, Input);
		const float AimTarget = ((B & Buttons::Aim) != 0 && !bSprintWanted && Ms.StanceTimer <= 0.0f && Ws.EquipTimer <= 0.0f) ? 1.0f : 0.0f;
		Ms.Aim = ApproachScalar(Ms.Aim, AimTarget, Dt / Def.AdsTime);

		// 2. Movement.
		const bool bSprinted = Simulate(Ms, Input, Dt, World);

		// 3. Timers.
		if (Ws.EquipTimer > 0.0f)
		{
			Ws.EquipTimer -= Dt;
			if (Ws.EquipTimer < 0.0f) { Ws.EquipTimer = 0.0f; }
		}
		if (Ws.ReloadTimer > 0.0f)
		{
			Ws.ReloadTimer -= Dt;
			if (Ws.ReloadTimer <= 0.0f)
			{
				Ws.ReloadTimer = 0.0f;
				WeaponFinishReload(Ws.Slots[Ws.Active], Def);
			}
		}

		// 4. Reloading.
		const bool bTrigger = (B & Buttons::Fire) != 0;
		const bool bPressed = bTrigger && !Ws.bTriggerPrev;
		{
			const SlotAmmo Slot = Ws.Slots[Ws.Active];
			const bool bCanStart = Ws.ReloadTimer <= 0.0f && Ws.EquipTimer <= 0.0f && Slot.Reserve > 0;
			const bool bManual = (B & Buttons::Reload) != 0 && Slot.Ammo < Def.MagSize;
			const bool bEmpty = bPressed && !Slot.bChambered && Slot.Ammo == 0;
			if (bCanStart && (bManual || bEmpty))
			{
				WeaponStartReload(Ws, Def);
			}
		}

		// 5. Firing.
		const bool bCanFire = Ws.EquipTimer <= 0.0f && Ws.ReloadTimer <= 0.0f && Ms.StanceTimer <= 0.0f && !bSprinted;
		if (Def.FireMode == EFireMode::Burst3 && bPressed && Ws.BurstLeft == 0)
		{
			Ws.BurstLeft = 3;
		}
		bool bWants = false;
		switch (Def.FireMode)
		{
		case EFireMode::Auto: bWants = bTrigger; break;
		case EFireMode::Semi: bWants = bPressed; break;
		case EFireMode::Burst3: bWants = Ws.BurstLeft > 0; break;
		}
		Ws.Cooldown -= Dt;
		int Fired = 0;
		const float Interval = 60.0f / Def.Rpm;
		while (bWants && bCanFire && Ws.Slots[Ws.Active].bChambered && Ws.Cooldown <= 0.0f && Fired < kMaxShotsPerTick)
		{
			const float ViewYaw = DequantizeYawDeg(Input.YawQ);
			const float ViewPitch = DequantizePitchDeg(Input.PitchQ);
			const float Speed = std::sqrt(Ms.Vel.X * Ms.Vel.X + Ms.Vel.Y * Ms.Vel.Y);
			float Cone = (Def.SpreadHip + (Def.SpreadAds - Def.SpreadHip) * Ms.Aim) * WeaponStanceMult(Ms, Def.SpreadCrouchMult, Def.SpreadProneMult)
				+ Def.SpreadMove * Speed;
			if (!Ms.bOnGround)
			{
				Cone += Def.SpreadJump;
			}
			const Vec3 Origin = Ms.EyePosition(Input.YawQ);
			Ws.ShotCounter += 1;
			for (uint32_t P = 0; P < Def.Pellets; ++P)
			{
				float HX = RandSigned(WeaponSeed(Ws.ShotCounter, P, 1));
				float HY = RandSigned(WeaponSeed(Ws.ShotCounter, P, 2));
				const float R2 = HX * HX + HY * HY;
				if (R2 > 1.0f)
				{
					const float K = 1.0f / std::sqrt(R2);
					HX *= K;
					HY *= K;
				}
				if (OutShots.Len < kMaxShotsPerTick * kMaxPellets)
				{
					Shot& S = OutShots.Shots[OutShots.Len++];
					S.Origin = Origin;
					S.YawDeg = ViewYaw + Ws.RecoilYaw + HX * Cone;
					S.PitchDeg = ViewPitch + Ws.RecoilPitch + HY * Cone;
					S.WeaponId = Def.Id;
					S.Pellet = static_cast<uint8_t>(P);
					S.ShotCounter = Ws.ShotCounter;
				}
			}

			Ws.Cooldown += Interval;
			SlotAmmo& Slot = Ws.Slots[Ws.Active];
			Slot.bChambered = false;
			if (Slot.Ammo > 0)
			{
				Slot.Ammo = static_cast<uint16_t>(Slot.Ammo - 1);
				Slot.bChambered = true;
			}
			if (Ws.ShotsFired < 0xFFFF) { Ws.ShotsFired = static_cast<uint16_t>(Ws.ShotsFired + 1); }
			const RecoilDef& R = Def.Recoil;
			size_t Idx = static_cast<size_t>(Ws.ShotsFired - 1);
			if (Idx > R.Pattern.size() - 1) { Idx = R.Pattern.size() - 1; }
			const float Mult = (1.0f + (R.AdsMult - 1.0f) * Ms.Aim) * WeaponStanceMult(Ms, R.CrouchMult, R.ProneMult);
			const float KickPitch = R.Pattern[Idx].first * Mult + RandSigned(WeaponSeed(Ws.ShotCounter, 0, 3)) * R.RandomPitch;
			const float KickYaw = R.Pattern[Idx].second * Mult + RandSigned(WeaponSeed(Ws.ShotCounter, 0, 4)) * R.RandomYaw;
			Ws.RecoilPitch += KickPitch;
			if (Ws.RecoilPitch > R.MaxPitch) { Ws.RecoilPitch = R.MaxPitch; }
			Ws.RecoilYaw += KickYaw;
			Ws.RecoverTimer = R.RecoveryDelay;
			++Fired;
			if (Def.FireMode == EFireMode::Semi) { break; }
			if (Def.FireMode == EFireMode::Burst3)
			{
				Ws.BurstLeft = static_cast<uint8_t>(Ws.BurstLeft - 1);
				if (Ws.BurstLeft == 0) { break; }
			}
		}
		if (Def.FireMode == EFireMode::Burst3 && (!bCanFire || !Ws.Slots[Ws.Active].bChambered))
		{
			Ws.BurstLeft = 0;
		}
		if (Ws.Cooldown < 0.0f && Fired == 0) { Ws.Cooldown = 0.0f; }
		if (!bTrigger && Ws.BurstLeft == 0) { Ws.ShotsFired = 0; }
		Ws.bTriggerPrev = bTrigger;

		// 6. Recoil recovery.
		if (Fired == 0)
		{
			if (Ws.RecoverTimer > 0.0f)
			{
				Ws.RecoverTimer -= Dt;
				if (Ws.RecoverTimer < 0.0f) { Ws.RecoverTimer = 0.0f; }
			}
			else
			{
				const float Step = Def.Recoil.RecoveryRate * Dt;
				Ws.RecoilPitch = ApproachScalar(Ws.RecoilPitch, 0.0f, Step);
				Ws.RecoilYaw = ApproachScalar(Ws.RecoilYaw, 0.0f, Step);
			}
		}
		return bSprinted;
	}
}

#include "NetCore/TfpsFloatDeterminismEnd.h"
