// HMD/controllers, local VR interaction and Enhanced Input (Section 4/12). Represents the player's
// physical presence (head + hands) while seated in the tank; the Tank Actor is the vehicle itself.
// Converts local input into gameplay requests via ATSTankPlayerController - this pawn holds no
// authoritative role permissions of its own (Section 12).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Core/TSTypes.h"
#include "HeadMountedDisplayTypes.h"
#include "TSVRPawn.generated.h"

class UCameraComponent;
class UMotionControllerComponent;
class UWidgetInteractionComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;

UCLASS()
class ATSVRPawn : public APawn
{
	GENERATED_BODY()

public:
	ATSVRPawn();

	// Both of these end up in RefreshCrewBinding. PossessedBy is deliberately NOT used: it runs on
	// the server only, so binding there left every CLIENT with no role mapping context and no seat.
	// NotifyControllerChanged fires from PossessedBy, OnRep_Controller and UnPossessed, i.e. on both
	// sides; OnRep_PlayerState covers the client ordering where the controller arrives first and the
	// PlayerState (which carries the role) replicates a moment later.
	virtual void NotifyControllerChanged() override;
	virtual void OnRep_PlayerState() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void Tick(float DeltaSeconds) override;

	// ---------------------------------------------------------------------
	// VR vs desktop - decided at runtime, per client, never at build time.
	//
	// The same build runs both ways. On possession this pawn asks whether a headset is connected
	// and, if so, switches stereo on; with nothing plugged in it stays on the flat screen and the
	// mouse drives the view instead. Nothing here is replicated - stereo is a property of the
	// local viewport, so the server neither knows nor cares.
	// ---------------------------------------------------------------------

	// True when this pawn has stereo running. False on a desktop client, on every remote copy of
	// this pawn, and on the server's copy of a client's pawn.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|VR")
	bool IsVRCrewMode() const;

	// Switch stereo on if a headset is present. Called automatically on possession; exposed so a
	// Blueprint or a console-driven test can re-run the decision (e.g. headset plugged in late).
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|VR")
	void ApplyVRMode();

	// Turn stereo on only when a headset is actually connected. Off makes this pawn stay flat
	// even in a headset, which is useful for a spectator/debug build.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|VR")
	bool bAutoEnableVRWhenHMDPresent = true;

	// Seated origin (Local) is correct for a tank crew: tracking is centred on where the headset
	// was at start, so the player's head sits at the seat component rather than on the tank floor.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|VR")
	TEnumAsByte<EHMDTrackingOrigin::Type> VRTrackingOrigin = EHMDTrackingOrigin::Local;

	// Called by ATSTankPlayerController (directly, or via PlayerState's OnAssignmentChanged) whenever
	// this player's CrewRole changes, so the correct role-specific Input Mapping Context is applied.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|VR")
	void ApplyRoleMappingContext(ETSCrewRole NewRole);

	// How far the Gunner's look-ray is traced when converting an aim gesture into a world point.
	// Beyond this the aim point is simply the end of the ray, which is fine - at long range the
	// direction is what matters and the gun elevation difference is negligible.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|VR")
	float AimTraceDistance = 100000.f;

	// --- Gunner mouse aim (desktop) -------------------------------------------------------------
	// In VR the Gunner aims by looking and the HMD drives the camera. On a desktop nothing moves the
	// camera at all, so the aim trace fired straight out of the hull for ever and the mouse appeared
	// to do nothing. These turn IA_AimTurret's 2D value into a seat-relative view rotation, which the
	// same trace then reads - one action, one code path, both devices.
	//
	// Degrees of view rotation per unit of mouse delta.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input", meta = (ClampMin = "0.01"))
	float MouseAimSensitivity = 1.f;

	// Pitch clamp for the seated view, in degrees. Negative looks down.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input", meta = (ClampMin = "-89.0", ClampMax = "0.0"))
	float MinAimPitch = -35.f;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float MaxAimPitch = 25.f;

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

	// ---------------------------------------------------------------------
	// VR widget interaction (scaffold - no crew widgets exist yet).
	//
	// Call this when a role widget is shown or hidden. It points the laser, and swaps in
	// VRWidgetMappingContext at a higher priority so the trigger clicks rather than fires.
	// Deliberately manual: nothing should guess when a widget is up.
	// ---------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|VR")
	void SetVRWidgetInteractionEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|VR")
	bool IsVRWidgetInteractionEnabled() const { return bVRWidgetInteractionEnabled; }

	// Traces from the eye and sends the world point the Gunner is looking at to the server.
	// Driven by IA_AimTurret on a desktop and by Tick in VR - in a headset the player aims by
	// turning their head, which fires no input action at all, so an input-only path would leave
	// the turret frozen for the entire session.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|VR")
	void UpdateGunnerAim();

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

	// Laser pointer for 3D widgets, on the right hand. Deactivated by default: there are no crew
	// widgets yet, and an always-on pointer both costs a trace every frame and puts a visible
	// beam through the cockpit. SetVRWidgetInteractionEnabled turns it on when a widget appears.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation|VR")
	TObjectPtr<UWidgetInteractionComponent> WidgetInteraction;

	// --- Enhanced Input assets - assign in a Blueprint subclass or the C++ defaults (Section 12) ---

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputMappingContext> SharedMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputMappingContext> DriverMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputMappingContext> GunnerMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputMappingContext> CommanderMappingContext;

	// Applied on top of the role context ONLY while stereo is on. The role contexts already carry
	// the motion-controller keys, so this is for bindings that would be actively wrong on a
	// desktop (snap turn, height recentre, hand poses) rather than a duplicate of them. Optional.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputMappingContext> VRMappingContext;

	// Added at a HIGHER priority than the role context while a VR widget is up, so the trigger
	// clicks the widget instead of firing the gun. Removed again when the widget closes.
	// See SetVRWidgetInteractionEnabled.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputMappingContext> VRWidgetMappingContext;

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

	// Re-applies the role mapping context, re-subscribes to the PlayerState's assignment delegate and
	// re-seats this pawn. Safe to call repeatedly - it unbinds the previous PlayerState first.
	void RefreshCrewBinding();

	// The PlayerState we currently hold an OnAssignmentChanged binding on. Weak so a PlayerState
	// destroyed on travel or disconnect cannot be dereferenced while unbinding.
	TWeakObjectPtr<class ATSTankPlayerState> BoundPlayerState;

	// Accumulated seat-relative view rotation driven by the mouse. Not the pawn's rotation: the pawn
	// is attached to a seat component on a moving hull, so the view has to turn WITH the tank, which
	// a controller/actor rotation would not.
	float SeatViewYaw = 0.f;
	float SeatViewPitch = 0.f;

	// Desktop only. Turns a mouse/stick delta into the seated view rotation; a no-op while the
	// headset drives the camera, where writing a relative rotation would fight the tracked pose.
	void ApplySeatViewDelta(const FVector2D& LookDelta);

	// True only for the local player who currently holds the Gunner seat.
	bool IsLocalGunner() const;

	// Ticking exists solely for the VR Gunner's head aim, so it is switched on and off with the
	// role rather than left running on every crew pawn in the level.
	void UpdateAimTickEnabled();

	bool bVRWidgetInteractionEnabled = false;
};
