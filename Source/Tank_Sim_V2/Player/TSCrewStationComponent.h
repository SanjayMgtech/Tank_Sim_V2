// Everything a pawn needs to be a tank crew member: the role's Input Mapping Context, the seat it
// rides on, and the routing of local input into ATSTankPlayerController's validated Server RPCs.
//
// WHY A COMPONENT AND NOT A PAWN CLASS
// This logic used to live directly on ATSVRPawn. That made the crew model unusable with any other
// pawn - notably the Unreal VR template's BP_XRPawn, which brings its own fully built VR rig
// (VROrigin, Camera, four motion controllers, hands, teleport, widget interaction). Reparenting
// BP_XRPawn onto ATSVRPawn would have collided name-for-name with ATSVRPawn's native VROrigin and
// Camera, leaving a dead camera and duplicate controllers - the exact component-recreation trap
// that sank the first port attempt (CLAUDE.md RULE 1).
//
// As a component, the crew behaviour drops onto ANY pawn without touching its components: add it to
// BP_XRPawn and that pawn is crew-capable, template untouched.
//
// It self-wires from APawn's ReceiveRestartedDelegate / ReceiveControllerChangedDelegate, so a
// Blueprint host needs no graph nodes at all - just the component and its data.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/TSTypes.h"
#include "TSCrewStationComponent.generated.h"

class ATSTankPlayerController;
class ATSTankPlayerState;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USceneComponent;
struct FInputActionValue;

UCLASS(ClassGroup = (TankSimulation), meta = (BlueprintSpawnableComponent))
class TANK_SIM_V2_API UTSCrewStationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTSCrewStationComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Applies the mapping context for a crew role and drops the other two. Safe to call repeatedly.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Crew Station")
	void ApplyRoleMappingContext(ETSCrewRole NewRole);

	// Server-authoritative. Attaches the owning pawn to its assigned tank at the seat for its role,
	// or detaches when it no longer holds one.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Crew Station")
	void UpdateCrewStationAttachment();

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Crew Station")
	bool IsSeatedInTank() const;

	// Binds the tank input actions onto the owner's input component. Called automatically when the
	// pawn is restarted; exposed for a host that wants to bind at a different moment.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Crew Station")
	void SetupCrewInput();

	// InRole, never Role: AActor declares a deprecated member of that name and UHT builds with
	// -WarningsAsErrors, so the shadow is a hard error (CLAUDE.md).
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Crew Station")
	FName GetSeatComponentNameForRole(ETSCrewRole InRole) const;

protected:
	// --- Seats ------------------------------------------------------------------------------------
	// Names only. The seats themselves are scene components on the tank Blueprint, placed by dragging
	// a gizmo in the viewport, so each tank positions its own crew and no rebuild is needed (RULE 8).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Crew Station")
	FName DriverSeatComponent = TEXT("DriverSeat");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Crew Station")
	FName GunnerSeatComponent = TEXT("GunnerSeat");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Crew Station")
	FName CommanderSeatComponent = TEXT("CommanderSeat");

	// Which component on the OWNING pawn is the player's viewpoint. Looked up by name so this works
	// unchanged on ATSVRPawn and on the VR template's BP_XRPawn - both call theirs "Camera".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Crew Station")
	FName ViewCameraComponent = TEXT("Camera");

	// --- Input ------------------------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Input")
	TObjectPtr<UInputMappingContext> SharedMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Input")
	TObjectPtr<UInputMappingContext> DriverMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Input")
	TObjectPtr<UInputMappingContext> GunnerMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Input")
	TObjectPtr<UInputMappingContext> CommanderMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_Drive;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_AimTurret;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_FireMainCannon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_FireMachineGun;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_ReloadWeapon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Input")
	TObjectPtr<UInputAction> IA_RequestIntel;

	// Priority for the role context. Above the shared context so a role binding wins a key clash.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Input")
	int32 RoleContextPriority = 1;

	// --- Gunner aim -------------------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Input")
	float AimTraceDistance = 100000.f;

	// In VR the head IS the aim, so the tracked pose is left alone. On desktop nothing rotates the
	// camera, so IA_AimTurret's 2D value turns a seat-relative view rotation that the same trace
	// reads - one action, one path, both devices.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Input", meta = (ClampMin = "0.01"))
	float MouseAimSensitivity = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Input", meta = (ClampMin = "-89.0", ClampMax = "0.0"))
	float MinAimPitch = -35.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Input", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxAimPitch = 25.f;

private:
	APawn* GetOwningPawn() const;
	ATSTankPlayerController* GetTankController() const;
	USceneComponent* FindViewCamera() const;

	// Re-reads the role from the PlayerState and re-applies context + seat. Both the controller and
	// the PlayerState have to lead here: on a client they arrive in either order, and the role is
	// only readable once both are in.
	void RefreshCrewBinding();

	UFUNCTION()
	void HandleAssignmentChanged();

	UFUNCTION()
	void HandlePawnRestarted(APawn* Pawn);

	UFUNCTION()
	void HandleControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);

	void Input_Drive(const FInputActionValue& Value);
	void Input_AimTurret(const FInputActionValue& Value);
	void Input_FireMainCannon(const FInputActionValue& Value);
	void Input_FireMachineGun(const FInputActionValue& Value);
	void Input_ReloadWeapon(const FInputActionValue& Value);
	void Input_RequestIntel(const FInputActionValue& Value);

	// Seat-relative view rotation driven by the desktop aim gesture. Relative, NOT world and NOT the
	// controller's rotation: the seat rides the hull, so the view has to turn with the tank.
	FRotator SeatRelativeAimRotation = FRotator::ZeroRotator;

	UPROPERTY()
	TWeakObjectPtr<ATSTankPlayerState> BoundPlayerState;

	bool bInputBound = false;
};
