// The pawn a crew member embodies while seated in a tank: head, hands, Enhanced Input and the crew
// station attachment. Converts local input into gameplay requests via ATSTankPlayerController - this
// pawn holds no authoritative role permissions of its own (Section 12). The Tank Actor is the
// vehicle itself and nobody possesses it.
//
// TWO concrete pawns derive from this, one per ETSPlayMode: ATSDesktopPawn (flat screen) and
// ATSVRPawn (headset). Everything a crew member DOES - seat, role input contexts, driving, aiming,
// firing - lives here, so the pair share one implementation and one Blueprint's worth of data: a
// designer duplicates the crew pawn Blueprint and reparents the copy to the other class, and every
// stored value still resolves, because the property that holds it is declared on THIS class.
//
// That is also why the motion controllers and the widget interaction component sit here rather than
// on ATSVRPawn. They are inert on a flat screen (nothing tracks them), and moving them down into the
// VR subclass would mean a desktop Blueprint duplicated from the VR one silently dropped them.
//
// A player owns one pawn of EACH mode at once (ATSGameMode::EnsureCrewPawnsFor spawns both) and can
// switch between them mid-match; the inactive one is hidden and unpossessed until they switch back.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Core/TSTypes.h"
#include "TSCrewPawn.generated.h"

class UCameraComponent;
class UMotionControllerComponent;
class UWidgetInteractionComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;

UCLASS(Abstract)
class ATSCrewPawn : public APawn
{
	GENERATED_BODY()

public:
	ATSCrewPawn();

	// Both of these end up in RefreshCrewBinding. PossessedBy is deliberately NOT used: it runs on
	// the server only, so binding there left every CLIENT with no role mapping context and no seat.
	// NotifyControllerChanged fires from PossessedBy, OnRep_Controller and UnPossessed, i.e. on both
	// sides; OnRep_PlayerState covers the client ordering where the controller arrives first and the
	// PlayerState (which carries the role) replicates a moment later.
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void NotifyControllerChanged() override;
	virtual void OnRep_PlayerState() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void Tick(float DeltaSeconds) override;

	// ---------------------------------------------------------------------
	// Play mode - which of the two crew pawns this class IS.
	//
	// The mode a player is assigned lives on ATSTankPlayerState; this answers the other half of the
	// question, "which pawn serves that mode", so the GameMode can slot a pawn it did not spawn
	// itself (the one AGameModeBase::RestartPlayer handed the controller) into the right place.
	// ---------------------------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Crew")
	virtual ETSPlayMode GetSupportedPlayMode() const { return ETSPlayMode::Desktop; }

	// True when this pawn has stereo running. Always false here: the flat-screen pawn never turns
	// stereo on, and neither does a remote copy or the server's copy of a client's pawn (stereo is a
	// property of ONE local viewport). ATSVRPawn overrides it.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|VR")
	virtual bool IsVRCrewMode() const { return false; }

	// Put this client's viewport into the display mode this pawn stands for. Called automatically on
	// possession and whenever the crew binding is refreshed; exposed so a Blueprint or a console
	// command can re-run the decision (a headset plugged in late, a mode reassigned mid-match).
	//
	// The work is DEFERRED BY ONE TICK, and that is not cosmetic - see the implementation.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|VR")
	void ApplyDisplayMode();

	// ---------------------------------------------------------------------
	// Active / inactive.
	//
	// Each player holds a Desktop pawn AND a VR pawn but possesses only one. The other is parked:
	// hidden, non-colliding and not ticking, so it is neither visible in the world nor paying for
	// work nobody is looking at. Server-driven (ATSGameMode::EnsureCrewPawnsFor); the hidden and
	// collision flags it sets are ordinary replicated actor state.
	// ---------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Crew")
	void SetCrewPawnActive(bool bActive);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Crew")
	bool IsCrewPawnActive() const { return bCrewPawnActive; }

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

	// --- Gunner: the mouse drives the GUN, never the view ---------------------------------------
	// The Gunner's station rides the turret basket (GunnerScene on b_Upper), so the ATTACHMENT
	// already carries the traverse, exactly once. Nothing here may rotate the player on top of that:
	// a free-look rotation stacked a second traverse onto the camera and the view came round faster
	// than the barrel, and re-pointing the camera at the gun every frame only replaced one double
	// write with another.
	//
	// So with this on, the Gunner's pawn and camera are never rotated by this class at all. The mouse
	// accumulates an aim COMMAND and the launcher turns to meet it; the player turns only insofar as
	// the basket they are sitting in turns, which is the traverse itself.
	//
	// Gunner only, and flat screen only: the Driver and Commander do not aim, and in a headset the
	// camera is the player's head - nothing may take that away from them.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	bool bGunnerMouseDrivesGun = true;

	// How far the mouse command may run ahead of where the gun has actually got to, in degrees.
	//
	// Without a cap this winds up: a long mouse sweep against a slowly traversing turret banks the
	// whole sweep, and the turret keeps spinning for seconds after the player has stopped moving the
	// mouse. Capping the lead keeps the gun responsive and makes it stop when the hand stops.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input", meta = (ClampMin = "1.0", ClampMax = "180.0"))
	float MaxGunnerAimLead = 45.f;

	UFUNCTION()
	void ApplyRoleMappingContext_FromPlayerState();

	// ---------------------------------------------------------------------
	// Crew station placement - "three players, one tank".
	//
	// Each crew member possesses their own crew pawn (nobody possesses the tank), so
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

	// GunnerScene on the VK1602 - a scene component parented to the interior mesh's turret basket
	// bone (b_Upper), beside GunnerScreen and the scene capture that feeds it. Sitting there puts the
	// player at the real gunner's station AND makes them traverse with the basket through the
	// attachment alone, which is why nothing in this class rotates them.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew Station")
	FName GunnerSeatComponent = TEXT("GunnerScene");

	// Tried if GunnerSeatComponent is not on the tank. The master Blueprint still ships the older
	// hull-mounted GunnerSeat, so tanks that have not had a gunner station authored yet keep working
	// instead of dumping the player on the tank's origin.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew Station")
	FName GunnerSeatFallbackComponent = TEXT("GunnerSeat");

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

	// Applied on top of the role context ONLY while stereo is on (IsVRCrewMode, which is false for
	// every desktop pawn). The role contexts already carry the motion-controller keys, so this is for
	// bindings that would be actively wrong on a desktop (snap turn, height recentre, hand poses)
	// rather than a duplicate of them. Optional.
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

	// The real body of ApplyDisplayMode, run one tick later. Flat screen here: stereo off and a
	// clean seat-forward camera. ATSVRPawn overrides it to switch stereo ON when the player has
	// actually been assigned VR and a headset is connected, and calls back here when they have not.
	virtual void ApplyDisplayModeDeferred();

	// The play mode this pawn's OWNER has been assigned, which is not necessarily the mode this pawn
	// class serves - during a switch the two disagree for a tick, and a single Blueprint configured
	// for both modes never spawns a second pawn at all. Desktop when there is no PlayerState to ask.
	ETSPlayMode GetAssignedPlayMode() const;

	// True while this pawn's owner is the session host. The host is a match admin on a flat screen
	// and never a VR participant, even holding a crew pawn (a test flag, a future spectate mode).
	bool IsOwnerMatchHost() const;

	// Cleared whenever the crew assignment changes so the aim command re-seeds from wherever the gun
	// happens to be pointing. Without it, sitting down mid-match would order the turret back to hull
	// forward as the first thing it did.
	bool bGunnerAimSynced = false;

	// Accumulated view rotation driven by the mouse, in the TANK's space. Not the pawn's rotation:
	// the pawn is attached to a seat component on a moving hull, so the view has to turn WITH the
	// tank, which a controller/actor rotation would not.
	//
	// For a Gunner with the sight lock on, this is not the view at all but the aim COMMAND - where
	// the player is asking the gun to point. Tank space, deliberately: a seat-relative command would
	// turn with the turret it is driving and the gun would spin without ever arriving.
	float SeatViewYaw = 0.f;
	float SeatViewPitch = 0.f;

	// Ticking exists solely for the Gunner's aim, so it is switched on and off with the role rather
	// than left running on every crew pawn in the level. Never on for a parked (inactive) pawn.
	void UpdateAimTickEnabled();

private:
	void Input_Recenter(const FInputActionValue& Value);
	void Input_Interact(const FInputActionValue& Value);
	void Input_Grab(const FInputActionValue& Value);
	void Input_Primary(const FInputActionValue& Value);
	void Input_Secondary(const FInputActionValue& Value);
	void Input_Menu(const FInputActionValue& Value);
	void Input_Drive(const FInputActionValue& Value);

	// Bound to Completed/Canceled on IA_Drive. Triggered fires ONLY while the axis is actuated,
	// so letting go of the stick or the keys produces no event at all - without this the last
	// non-zero throttle stays latched on the server and the tank drives on for ever.
	void Input_DriveReleased(const FInputActionValue& Value);
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

	// Desktop only. Turns a mouse/stick delta into the seated view rotation; a no-op while the
	// headset drives the camera, where writing a relative rotation would fight the tracked pose.
	void ApplySeatViewDelta(const FVector2D& LookDelta);

	// True only for the local player who currently holds the Gunner seat.
	bool IsLocalGunner() const;

	// --- Gunner aim command -----------------------------------------------------------------------
	// True when this pawn's mouse should be steering the launcher instead of turning the view.
	bool IsGunnerMouseDrivingGun() const;

	// Per-frame upkeep of the aim command: seed it from the gun the first time, then keep its lead
	// over the gun bounded. Deliberately does NOT touch the camera - see bGunnerMouseDrivesGun.
	void UpdateGunnerAimCommand();

	// The direction the Gunner is ASKING for, in world space: the mouse command applied in tank space.
	FRotator GetGunnerAimWorldRotation() const;

	// Stops the mouse command banking an unbounded lead over the gun. See MaxGunnerAimLead.
	void ClampGunnerAimLead();

	// The tank this pawn is crewing, or null. Cast once, here, so callers do not repeat it.
	class ATSTankControllerBase* GetAssignedTankController() const;

	// The tank we currently hold a tick prerequisite on, so it can be dropped when we leave it.
	TWeakObjectPtr<AActor> TickPrerequisiteTank;

	bool bVRWidgetInteractionEnabled = false;

	// False while this pawn is the player's OTHER embodiment: parked, hidden and unpossessed.
	// Replicated so IsCrewPawnActive answers truthfully everywhere: a client would otherwise see the
	// hidden actor (bHidden replicates) but be told by this that it was the live one.
	UPROPERTY(Replicated)
	bool bCrewPawnActive = true;
};
