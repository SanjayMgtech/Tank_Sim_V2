// HMD/controllers, local VR interaction and Enhanced Input (Section 4/12). Represents the player's
// physical presence (head + hands) while seated in the tank; the Tank Actor is the vehicle itself.
// Converts local input into gameplay requests via ATSTankPlayerController - this pawn holds no
// authoritative role permissions of its own (Section 12).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Core/TSTypes.h"
#include "TSVRPawn.generated.h"

class UCameraComponent;
class UMotionControllerComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;

UCLASS()
class ATSVRPawn : public APawn
{
	GENERATED_BODY()

public:
	ATSVRPawn();

	virtual void PossessedBy(AController* NewController) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// Called by ATSTankPlayerController (directly, or via PlayerState's OnAssignmentChanged) whenever
	// this player's CrewRole changes, so the correct role-specific Input Mapping Context is applied.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|VR")
	void ApplyRoleMappingContext(ETSCrewRole NewRole);

	// How far the Gunner's look-ray is traced when converting an aim gesture into a world point.
	// Beyond this the aim point is simply the end of the ray, which is fine - at long range the
	// direction is what matters and the gun elevation difference is negligible.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|VR")
	float AimTraceDistance = 100000.f;

	UFUNCTION()
	void ApplyRoleMappingContext_FromPlayerState();

	// ---------------------------------------------------------------------
	// Crew station placement - "three players, one tank".
	//
	// Each crew member possesses their own VR pawn (nobody possesses the tank), so
	// without this they spawn at a PlayerStart and stay there while the tank drives
	// away. Attaching them to the hull is what makes the three of them actually ride
	// the same vehicle.
	//
	// Seats are SCENE COMPONENTS on the tank Blueprint, not offsets typed in here.
	// A designer drags them in the viewport to place a crew station, sees exactly
	// where the player's head will be, and needs no code change or rebuild. That also
	// keeps the placement where RULE 1 says it belongs - as Blueprint data - and lets
	// each tank position its own crew differently.
	//
	// These are just the component NAMES to look for on the assigned tank.
	// ---------------------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew Station")
	FName DriverSeatComponent = TEXT("DriverSeat");

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew Station")
	FName GunnerSeatComponent = TEXT("GunnerSeat");

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew Station")
	FName CommanderSeatComponent = TEXT("CommanderSeat");

	// Server-authoritative. Attaches this pawn to its assigned tank at the station for
	// its crew role, or detaches when it no longer has one.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Crew Station")
	void UpdateCrewStationAttachment();

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Crew Station")
	bool IsSeatedInTank() const;

	// Parameter is InRole, not Role: AActor declares a (deprecated) member called Role
	// (legacy ENetRole) and UHT builds with -WarningsAsErrors, so the shadow is a hard error.
	FName GetSeatComponentNameForRole(ETSCrewRole InRole) const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation|VR")
	TObjectPtr<USceneComponent> VROrigin;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation|VR")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation|VR")
	TObjectPtr<UMotionControllerComponent> LeftHand;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation|VR")
	TObjectPtr<UMotionControllerComponent> RightHand;

	// --- Enhanced Input assets - assign in a Blueprint subclass or the C++ defaults (Section 12) ---

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputMappingContext> SharedMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputMappingContext> DriverMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputMappingContext> GunnerMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputMappingContext> CommanderMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_Interact;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_Grab;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_Primary;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_Secondary;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_Menu;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_Recenter;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_Drive;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_AimTurret;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_FireMainCannon;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_FireMachineGun;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_ReloadWeapon;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_RequestIntel;

	// Extension point for hand-interaction with cockpit levers/switches - not prescribed by the doc.
	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|VR")
	void OnInteractPressed();

	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|VR")
	void OnGrabPressed();

	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|VR")
	void OnPrimaryPressed();

	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|VR")
	void OnSecondaryPressed();

	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|VR")
	void OnMenuPressed();

private:
	void Input_Recenter(const FInputActionValue& Value);
	void Input_Interact(const FInputActionValue& Value);
	void Input_Grab(const FInputActionValue& Value);
	void Input_Primary(const FInputActionValue& Value);
	void Input_Secondary(const FInputActionValue& Value);
	void Input_Menu(const FInputActionValue& Value);
	void Input_Drive(const FInputActionValue& Value);
	void Input_AimTurret(const FInputActionValue& Value);
	void Input_FireMainCannon(const FInputActionValue& Value);
	void Input_FireMachineGun(const FInputActionValue& Value);
	void Input_ReloadWeapon(const FInputActionValue& Value);
	void Input_RequestIntel(const FInputActionValue& Value);

	class ATSTankPlayerController* GetTankController() const;
};
