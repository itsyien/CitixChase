// Copyright Epic Games, Inc. All Rights Reserved.
// Editor-only commandlet that authors the Citix night-lighting materials.
//
// Why a commandlet: the project deliberately ships no binary assets in source control,
// and materials cannot be authored from a text editor. This tool builds the two
// emissive materials in the editor and saves them, so the runtime can just load them.
//
//   UnrealEditor-Cmd.exe <project>.uproject -run=CitixMaterialSetup -unattended -nosplash
//
// Produces:
//   /Game/Citix/Materials/M_CitixWindow    opaque unlit emissive, per-instance random
//                                          brightness variation (window bands)
//   /Game/Citix/Materials/M_CitixEmissive  opaque unlit emissive (lamps, vehicle lights,
//                                          signs)

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CitixMaterialSetupCommandlet.generated.h"

UCLASS()
class UCitixMaterialSetupCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCitixMaterialSetupCommandlet();

	virtual int32 Main(const FString& Params) override;
};
