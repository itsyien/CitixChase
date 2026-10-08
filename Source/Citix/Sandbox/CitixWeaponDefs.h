// Copyright Epic Games, Inc. All Rights Reserved.
// Weapon catalogue: four hitscan guns. Pure data; the director owns behaviour.
// The player starts with nothing - every gun and every bullet comes from a shop.

#pragma once

#include "CoreMinimal.h"
#include "CitixWeaponDefs.generated.h"

UENUM(BlueprintType)
enum class ECitixWeapon : uint8
{
	Pistol,
	SMG,
	Rifle,
	Sniper,

	Count
};

USTRUCT(BlueprintType)
struct FCitixWeaponDef
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Citix|Weapons")
	FString Name = TEXT("Pistol");

	/** Shop price, $. */
	UPROPERTY(BlueprintReadOnly, Category = "Citix|Weapons")
	int32 Price = 250;

	/** Seconds between shots. */
	UPROPERTY(BlueprintReadOnly, Category = "Citix|Weapons")
	float FireInterval = 0.25f;

	/** True = hold trigger to keep firing. */
	UPROPERTY(BlueprintReadOnly, Category = "Citix|Weapons")
	bool bFullAuto = false;

	UPROPERTY(BlueprintReadOnly, Category = "Citix|Weapons")
	int32 MagSize = 12;

	/** Reserve ammo a full refill buys. */
	UPROPERTY(BlueprintReadOnly, Category = "Citix|Weapons")
	int32 MaxReserve = 60;

	/** Base spread, degrees (hip fire). ADS multiplies it down. */
	UPROPERTY(BlueprintReadOnly, Category = "Citix|Weapons")
	float SpreadDegrees = 1.2f;

	/** Camera FOV multiplier at full ADS. */
	UPROPERTY(BlueprintReadOnly, Category = "Citix|Weapons")
	float AdsZoom = 0.65f;

	/** Sniper: fullscreen scope overlay at full ADS. */
	UPROPERTY(BlueprintReadOnly, Category = "Citix|Weapons")
	bool bScoped = false;

	/** Price of one full reserve refill, $. */
	UPROPERTY(BlueprintReadOnly, Category = "Citix|Weapons")
	int32 AmmoPrice = 80;

	/** Flat damage per landed hit (pedestrian health is 100). */
	UPROPERTY(BlueprintReadOnly, Category = "Citix|Weapons")
	float DamagePerHit = 34.f;

	FLinearColor TracerColor = FLinearColor(0.4f, 0.8f, 1.f);
};

/** Shared catalogue. Index with ECitixWeapon (Pistol=0 .. Sniper=3). */
inline const TArray<FCitixWeaponDef>& CitixWeaponCatalogue()
{
	static const TArray<FCitixWeaponDef> Catalogue = []()
	{
		TArray<FCitixWeaponDef> List;

		FCitixWeaponDef Pistol;
		Pistol.Name = TEXT("Pistol");
		Pistol.Price = 250;
		Pistol.FireInterval = 0.28f;
		Pistol.bFullAuto = false;
		Pistol.MagSize = 12;
		Pistol.MaxReserve = 60;
		Pistol.SpreadDegrees = 1.2f;
		Pistol.AdsZoom = 0.7f;
		Pistol.bScoped = false;
		Pistol.AmmoPrice = 60;
		Pistol.DamagePerHit = 34.f; // 3 hits (100 health)
		Pistol.TracerColor = FLinearColor(1.f, 0.85f, 0.4f);
		List.Add(Pistol);

		FCitixWeaponDef SMG;
		SMG.Name = TEXT("SMG");
		SMG.Price = 600;
		SMG.FireInterval = 0.1f;
		SMG.bFullAuto = true;
		SMG.MagSize = 30;
		SMG.MaxReserve = 120;
		SMG.SpreadDegrees = 2.6f;
		SMG.AdsZoom = 0.7f;
		SMG.bScoped = false;
		SMG.AmmoPrice = 90;
		SMG.DamagePerHit = 26.f; // 4 hits
		SMG.TracerColor = FLinearColor(1.f, 0.55f, 0.25f);
		List.Add(SMG);

		FCitixWeaponDef Rifle;
		Rifle.Name = TEXT("Rifle");
		Rifle.FireInterval = 0.14f;
		Rifle.Price = 1200;
		Rifle.bFullAuto = true;
		Rifle.MagSize = 30;
		Rifle.MaxReserve = 120;
		Rifle.SpreadDegrees = 1.0f;
		Rifle.AdsZoom = 0.55f;
		Rifle.bScoped = false;
		Rifle.AmmoPrice = 100;
		Rifle.DamagePerHit = 51.f; // 2 hits
		Rifle.TracerColor = FLinearColor(0.5f, 1.f, 0.55f);
		List.Add(Rifle);

		FCitixWeaponDef Sniper;
		Sniper.Name = TEXT("Sniper");
		Sniper.Price = 2500;
		Sniper.FireInterval = 1.1f;
		Sniper.bFullAuto = false;
		Sniper.MagSize = 5;
		Sniper.MaxReserve = 25;
		Sniper.SpreadDegrees = 0.15f;
		Sniper.AdsZoom = 0.22f;
		Sniper.bScoped = true;
		Sniper.AmmoPrice = 120;
		Sniper.DamagePerHit = 101.f; // 1 hit
		Sniper.TracerColor = FLinearColor(0.7f, 0.9f, 1.f);
		List.Add(Sniper);

		return List;
	}();
	return Catalogue;
}
