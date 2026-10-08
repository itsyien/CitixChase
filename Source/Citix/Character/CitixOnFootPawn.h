// Copyright Epic Games, Inc. All Rights Reserved.
// Third-person on-foot player pawn with a low-poly body and a procedural walk cycle.
// This is what you get when you step out of the car.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Character/CitixCharacterLibrary.h"
#include "CitixOnFootPawn.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

UCLASS(Blueprintable)
class CITIX_API ACitixOnFootPawn : public ACharacter
{
	GENERATED_BODY()

public:
	ACitixOnFootPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void UnPossessed() override;
	virtual void OnRep_Controller() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "Citix|Character")
	ECitixCharacterStyle Style = ECitixCharacterStyle::Casual;

	/** Random seed used when the style/appearance is auto-generated. */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "Citix|Character")
	int32 AppearanceSeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Character")
	float WalkSpeed = 340.f;

	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_SprintSpeed, BlueprintReadWrite, Category = "Citix|Character")
	float SprintSpeed = 700.f;

	/** Distance covered per half walk cycle, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Citix|Character")
	float StrideLength = 150.f;
	void BeginShockwaveRecovery(const FVector& Impulse);
	bool IsInShockwaveRecovery() const { return RecoveryEndsAt > 0.f; }

	/** The vehicle the player is currently standing next to. */
	UFUNCTION(BlueprintPure, Category = "Citix|Character")
	class ACitixVehiclePawn* FindNearestVehicle(float MaxDistance = 400.f) const;

	/** Nearest parked or traffic car the player could get into. */
	UFUNCTION(BlueprintPure, Category = "Citix|Character")
	class ACitixTrafficVehicle* FindNearestTrafficVehicle(float MaxDistance = 400.f) const;

protected:
	void HandleMoveForward(const FInputActionValue& Value);
	void HandleMoveRight(const FInputActionValue& Value);
 public: FVector GetChaseMuzzle() const;
 protected:
	void HandleLook(const FInputActionValue& Value);
	void HandleJumpStart(const FInputActionValue& Value);
	void HandleJumpEnd(const FInputActionValue& Value);
	void HandleSprintStart(const FInputActionValue& Value);
	void HandleSprintEnd(const FInputActionValue& Value);
	void HandleInteract(const FInputActionValue& Value);
	void HandleInteractEnd(const FInputActionValue& Value);

	void AddMappingContext();
	void RemoveMappingContext();

	/** Sprint intent, replicated: the server must run the same MaxWalkSpeed or
	 * the owner's prediction (700) diverges from server truth (340) and every
	 * remote sees walking while the owner runs. */
	UPROPERTY(ReplicatedUsing = OnRep_Sprinting)
	bool bSprinting = false;

	UFUNCTION(Server, Reliable)
	void ServerSetSprinting(bool bNewSprinting);

	UFUNCTION()
	void OnRep_Sprinting();
	UFUNCTION()
	void OnRep_SprintSpeed();

	void ApplySprintSpeed();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Citix|Components")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Citix|Components")
	TObjectPtr<UCameraComponent> FollowCamera;

	/** Runtime-built low-poly body. Components are actor-owned, so this is GC-safe. */
	FCitixHumanoidRig Rig;

	/** Offsets the body so the feet sit at the capsule bottom (ground). */
	UPROPERTY()
	TObjectPtr<USceneComponent> BodyRoot;

	/** Client-only gun prop (rank 18: remotes show the resolved kind). */
	UPROPERTY()
	TObjectPtr<class ACitixWeaponProp> LocalWeaponProp;

	class ACitixSandboxDirector* FindSandbox() const;

	UPROPERTY() TObjectPtr<UInputMappingContext> CharacterContext;
	// Controller may already be cleared when a remote pawn is destroyed.
	TWeakObjectPtr<class UEnhancedInputLocalPlayerSubsystem> MappingSubsystem;
 float GatePulseTimer=0.f;
	UPROPERTY() TObjectPtr<UInputAction> MoveForwardAction;
	UPROPERTY() TObjectPtr<UInputAction> MoveRightAction;
	UPROPERTY() TObjectPtr<UInputAction> LookAction;
	UPROPERTY() TObjectPtr<UInputAction> JumpAction;
	UPROPERTY() TObjectPtr<UInputAction> SprintAction;
	UPROPERTY() TObjectPtr<UInputAction> InteractAction;

private:
	UPROPERTY(Replicated) FVector ShockwaveImpulse = FVector::ZeroVector;
	UPROPERTY(Replicated) float RecoveryEndsAt = 0.f;
	UPROPERTY() TObjectPtr<class ACitixPedestrian> LocalRagdoll;
	bool bRecoveryPoseFinished = false;
	void UpdateShockwaveRecovery(float DeltaSeconds);
	bool FindRecoveryPosition(FVector& OutPosition) const;
	float WalkPhase = 0.f;
};
