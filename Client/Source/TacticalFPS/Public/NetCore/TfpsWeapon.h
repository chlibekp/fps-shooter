// Copyright TacticalFPS project. Engine-independent NetCore.
//
// Bit-exact C++ mirror of Shared/Simulation/rust/src/weapon.rs: fire modes and
// rate, magazine + chambered round, reloads, switching, ADS, deterministic
// recoil and spread. Verified by the player conformance vectors.
#pragma once

#include "NetCore/TfpsMap.h"
#include "NetCore/TfpsMovement.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tfps
{
	constexpr int kWeaponSlots = 2;
	/** Gadget slots follow the weapon slots (input slot kWeaponSlots + i = gadget i, weapon holstered). */
	constexpr int kGadgetSlots = 3;
	/** Flag in MoveInput::Slot: remote-controlling a device (drone); the body keeps only its stance. */
	constexpr uint8_t kRemoteControl = 0x80;
	constexpr float kGadgetEquipTime = 0.4f;
	constexpr float kGadgetAimTime = 0.25f;
	inline bool IsGadgetSlot(uint8_t Active) { return Active >= kWeaponSlots && Active < kWeaponSlots + kGadgetSlots; }
	constexpr int kMaxShotsPerTick = 4;
	constexpr int kMaxPellets = 12;

	enum class EFireMode : uint8_t { Auto = 0, Semi = 1, Burst3 = 2 };

	struct RecoilDef
	{
		std::vector<std::pair<float, float>> Pattern; // (pitch up, yaw right) degrees
		float RandomYaw = 0, RandomPitch = 0, AdsMult = 1, CrouchMult = 1, ProneMult = 1;
		float RecoveryRate = 0, RecoveryDelay = 0, MaxPitch = 0;
	};

	struct WeaponDef
	{
		uint8_t Id = 0;
		std::string Name;
		std::string Class;
		EFireMode FireMode = EFireMode::Auto;
		float Rpm = 600;
		uint16_t MagSize = 30;
		uint16_t Reserve = 90;
		uint8_t Pellets = 1;
		float Damage = 30, DamageMin = 20, FalloffStart = 20, FalloffEnd = 40, Range = 100, Penetration = 1;
		float HeadMult = 3, LimbMult = 0.75f;
		float ReloadTime = 2, ReloadEmptyTime = 2.5f, AdsTime = 0.25f, EquipTime = 0.5f;
		float SpreadHip = 2, SpreadAds = 0.1f, SpreadMove = 0.5f, SpreadJump = 4, SpreadCrouchMult = 1, SpreadProneMult = 1;
		RecoilDef Recoil;
	};

	struct WeaponSet
	{
		std::vector<WeaponDef> Weapons;
		uint8_t DefaultLoadout[kWeaponSlots] = {0, 0};

		const WeaponDef* Get(uint8_t Id) const;
		/** Same value as WeaponSet::hash() in Rust. */
		uint64_t Hash() const;
	};

	/** Parses Shared/Definitions/weapons.json. */
	bool ParseWeaponsJson(const std::string& Text, WeaponSet& Out, std::string& OutError);

	struct SlotAmmo
	{
		uint16_t Ammo = 0;
		bool bChambered = false;
		uint16_t Reserve = 0;
		bool operator==(const SlotAmmo& O) const { return Ammo == O.Ammo && bChambered == O.bChambered && Reserve == O.Reserve; }
	};

	struct WeaponState
	{
		uint8_t Active = 0;
		SlotAmmo Slots[kWeaponSlots];
		float Cooldown = 0;
		float ReloadTimer = 0;
		bool bReloadEmpty = false;
		float EquipTimer = 0;
		uint8_t BurstLeft = 0;
		bool bTriggerPrev = false;
		uint16_t ShotsFired = 0;
		float RecoilPitch = 0;
		float RecoilYaw = 0;
		float RecoverTimer = 0;
		uint32_t ShotCounter = 0;

		static WeaponState New(const WeaponDef* const Loadout[kWeaponSlots]);
		bool IsReloading() const { return ReloadTimer > 0.0f; }
		bool operator==(const WeaponState& O) const;
	};

	struct Shot
	{
		Vec3 Origin;
		float YawDeg = 0;
		float PitchDeg = 0;
		uint8_t WeaponId = 0;
		uint8_t Pellet = 0;
		uint32_t ShotCounter = 0;
	};

	struct ShotBuffer
	{
		Shot Shots[kMaxShotsPerTick * kMaxPellets];
		int Len = 0;
		void Clear() { Len = 0; }
	};

	uint32_t Hash32(uint32_t X);
	float RandSigned(uint32_t H);

	/** Advances movement and weapon state one fixed step. Returns true if the player sprinted. */
	bool SimulatePlayer(MoveState& Ms, WeaponState& Ws, const MoveInput& Input, float Dt, const CollisionWorld& World,
		const WeaponDef* const Loadout[kWeaponSlots], ShotBuffer& OutShots);
}
