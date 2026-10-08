// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/CitixGameState.h"

#include "Net/UnrealNetwork.h"

void ACitixGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACitixGameState, SharedPOIs);
	DOREPLIFETIME(ACitixGameState, VisitedDistricts);
	DOREPLIFETIME(ACitixGameState, ShopLocations);
	DOREPLIFETIME(ACitixGameState, ShopNames);
	DOREPLIFETIME(ACitixGameState, CitySeed);
}
