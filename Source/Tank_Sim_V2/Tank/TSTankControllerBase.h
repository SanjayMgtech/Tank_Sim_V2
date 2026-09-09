// Native base class for BP_TankController_Chaos.
//
// PORTING CONTRACT (see CLAUDE.md):
//   - This class owns LOGIC only.
//   - Components stay in the Blueprint's construction script. Do NOT add
//     CreateDefaultSubobject calls for components that already exist in the BP.
//   - Asset references stay in the Blueprint. Do NOT use ConstructorHelpers here.
//
// Phase 1: intentionally empty. It exists only so the Blueprint can be reparented
// onto it without changing any behaviour.
#pragma once

#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"
#include "Core/TSTypes.h"
#include "Tank/TSTankInterface.h"
#include "TSTankControllerBase.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UInstancedStaticMeshComponent;
class USplineComponent;
class UChaosWheeledVehicleMovementComponent;

UCLASS(Blueprintable, BlueprintType)
class TANK_SIM_V2_API ATSTankControllerBase : public AWheeledVehiclePawn, public ITSTankInterface
{
	GENERATED_BODY()

public:
	ATSTankControllerBase();

	// ---------------------------------------------------------------------
	// Phase 4: chassis distance accumulators.
	//
	// Pure runtime scratch state, written by ChassisDistanceDefinition and read
	// by the track/spline animation. Not per-tank configuration - every child
	// Blueprint leaves these at 0, so moving them carries no default-value risk.
	//
	// Names, types and category match the Blueprint variables they replace
	// exactly, so existing Get/Set nodes rebind to these on compile (RULE 4).
	// BP type "double" -> C++ double. Not instance-editable in the BP, so
	// BlueprintReadWrite only (no EditAnywhere).
	// ---------------------------------------------------------------------
	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double ChassisDistanceR = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double ChassisDistanceL = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double ChassisDeltaDistanceR = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double ChassisDeltaDistanceL = 0.0;

	// ---------------------------------------------------------------------
	// Phase 5: remaining chassis runtime scratch.
	//
	// Same risk class as Phase 4 - default 0, written by ChassisDistanceDefinition,
	// read only by ChassisDistanceDefinition and TrackPathAnimations. Confirmed not
	// referenced by ABP_Chaos_<Tank> (which touches only its own ChassisLockedL/R),
	// so nothing outside the master Blueprint consumes these.
	//
	// Deliberately EXCLUDES SaggingDegreeR/L and the WheelRot* group: the AnimBPs
	// read those across an asset boundary, which is a different risk class and gets
	// its own phase with its own test.
	// ---------------------------------------------------------------------
	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double ChassisAccelerationR = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double ChassisAccelerationL = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double HullZRot = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double ChassisDistanceZRotComponentR = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double ChassisDistanceZRotComponentL = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double ChassisDistanceXMoveComponentR = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double ChassisDistanceXMoveComponentL = 0.0;

	// ---------------------------------------------------------------------
	// Phase 6: turret / machine gun scratch. First phase to move STRUCT types.
	//
	// All default-zero runtime scratch, no per-tank configuration. Confirmed the
	// AnimBP does not consume any of these - ABP_Chaos_T90 drives the turret bone
	// from TurretsRot (the replicated ARRAY), which is deliberately NOT in this
	// group. Replicated properties and arrays are a later phase.
	//
	// All three are written every tick via TurretsAndGunsRotCalculation / EventGraph.
	//
	// This group originally also held TurretRotation, MGRotation, TurretYaw and
	// TurretPitch, which were read only by UpdateTurretRotation_Old and
	// UpdateMachineGunRotation_Old. Those two functions were dead code and have been
	// deleted, so those four properties went with them.
	// ---------------------------------------------------------------------
	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Turret")
	FRotator MainTurretAndGunRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Turret")
	bool TurretBlocking = false;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Turret")
	bool IsTurretRotating = false;

	// ---------------------------------------------------------------------
	// Phase 18: moved so RecalculateGunAndTurretRotation can read it from C++.
	//
	// Pure runtime scratch - written once in the EventGraph (the stabilizer input)
	// and read six times. Verified false on the master AND all six per-tank
	// Blueprints before moving, so there is no override to lose.
	//
	// Category is "Hidden (Used for logic)" with no subcategory, unlike the
	// Turret-suffixed group above; carried across verbatim so the details panel
	// groups it where it was.
	// ---------------------------------------------------------------------
	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)")
	bool Stabilization = false;

	// ---------------------------------------------------------------------
	// Phase 7: antenna / UI / misc runtime scratch. First FVector, float and int32.
	//
	// Verified against all six per-tank Blueprints: every one of these matches the
	// master's default, so no child override can be dropped by the move.
	//
	// Three exact-match traps in this group - do not "tidy" any of them:
	//   * TrackSpeedModifier defaults to 1.0, NOT 0. Zeroing it would silently
	//     scale every track animation to nothing.
	//   * ForwardSpeedMPH is a Blueprint float, not a double.
	//   * CurentRPMRatio is misspelled in the Blueprint ("Curent"). The name must
	//     match exactly or the rebind orphans the data (RULE 4).
	// ---------------------------------------------------------------------
	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Antenna")
	FVector HullSpeedWorld = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Antenna")
	FVector HullAccelerationWorldInverted = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Antenna")
	FVector TurretSpeedLocalInverted = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|UI")
	double CrosshairTraceClamp = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|UI")
	double AimPointCorrectionUI = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|UI")
	int32 DamageCausedUI = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double CurrentAmplitudeMultiplierR = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double CurrentAmplitudeMultiplierL = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double FilletsCompensation = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	double HullDeltaXLocation = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	float ForwardSpeedMPH = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)")
	double DeltaSeconds = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)")
	double CurentRPMRatio = 0.0;

	// Default is 1.0 in the Blueprint - see the trap note above.
	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)")
	double TrackSpeedModifier = 1.0;

	// ---------------------------------------------------------------------
	// Phase 8: runtime scratch ARRAYS.
	//
	// Array LENGTH is the new risk. Several of these ship with a pre-sized
	// default and the graph indexes into them directly, so an empty array would
	// throw "Attempted to access index N from array of length 0" at runtime.
	// The sized ones are filled in the constructor - verified identical across
	// all six per-tank Blueprints.
	//
	// Excluded from this phase:
	//   * WheelsZOffsets, AntennaRotation - consumed by ABP_Chaos_<Tank>, so they
	//     belong with the AnimBP group and its animation-level test.
	//   * TurretsRot, GunsRot - replicated; replication is its own phase.
	//   * TracksInstances_R/L - arrays of component object pointers.
	//   * TankSplineAnim, TrackStaticMeshes, AntennaParameters, CamoVariations,
	//     PawnClassSelection - real per-tank configuration, not scratch.
	// ---------------------------------------------------------------------

	// Empty by default.
	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	TArray<double> VibrationOffset_R;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	TArray<double> VibrationOffset_L;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	TArray<FVector> SplinePointLocation;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	TArray<FVector> SplinePointPerpendicularVectors;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	TArray<int32> CopyPointIndices;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Scattering")
	TArray<double> FinalScattering;

	// Pre-sized in the constructor - see TSTankControllerBase.cpp.
	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Antenna")
	TArray<FVector> AntennaCurrentSpeed;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Turret")
	TArray<FRotator> TurretsRotUnstabilized;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Scattering")
	TArray<FRotator> TurretsRotPrevFrame;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Gun")
	TArray<FRotator> GunsRotUnstabilized;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Scattering")
	TArray<FRotator> GunsRotPrevFrame;

	// ---------------------------------------------------------------------
	// Phase 9: REPLICATED properties. First phase needing real C++, not just
	// declarations.
	//
	// A Blueprint variable with "Replicated" ticked stops replicating the moment
	// it becomes a C++ property, unless BOTH of these are present:
	//   1. the Replicated specifier on the UPROPERTY, and
	//   2. an entry in GetLifetimeReplicatedProps (see the .cpp).
	// Miss either one and the value still reads and writes correctly in a
	// single-player PIE session - the loss only shows up over the network. This
	// is the least visible failure mode in the whole port.
	//
	// Verified there are no RepNotify handlers: the Blueprint has no OnRep nodes
	// and no OnRep_* functions, so these are plain Replicated, not ReplicatedUsing.
	//
	// TurretsRot and GunsRot are read by ABP_Chaos_<Tank> through its TankPawn
	// reference (the AnimBP does NOT declare its own copies of these, unlike
	// WheelRot*/SaggingDegree*), so the exact names matter across an asset
	// boundary as well as inside the master Blueprint.
	// ---------------------------------------------------------------------
	UPROPERTY(BlueprintReadWrite, Replicated, Category = "Hidden (Used for logic)|Turret")
	TArray<FRotator> TurretsRot;

	UPROPERTY(BlueprintReadWrite, Replicated, Category = "Hidden (Used for logic)|Gun")
	TArray<FRotator> GunsRot;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---------------------------------------------------------------------
	// Phase 10: object / component REFERENCE variables.
	//
	// These are plain variables that hold references assigned at runtime - they
	// are NOT components. Verified against the Blueprint's component list: the
	// actual SCS components are TrackPath_R and BP_TankWeapon (plus the 19 others),
	// and those stay in the Blueprint untouched per RULE 1. Note the deliberate
	// asymmetry - TrackPath_R is a component, TrackPath_L is a variable.
	//
	// All seven default to null/empty on the master and on all six tanks, so
	// there is no per-tank asset reference to drop.
	//
	// TObjectPtr rather than raw pointers: UHT runs with -WarningsAsErrors here,
	// and raw object pointers in a UPROPERTY can be reported as a member-pointer
	// violation.
	//
	// NOT PORTABLE - deliberately excluded, see CLAUDE.md:
	//   HUD (W_MainHUD_C), Crosshair (W_Crosshair_C), BPC_TankWeapon (BP_TankWeapon_C)
	//   are typed as Blueprint-generated classes. C++ cannot name those types, and
	//   widening them to a native base would break the graph nodes that call
	//   Blueprint-only members on them.
	// ---------------------------------------------------------------------
	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	TObjectPtr<UMaterialInterface> BaseTrackMaterial;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	TObjectPtr<UMaterialInstanceDynamic> RightTrackMID;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	TObjectPtr<UMaterialInstanceDynamic> LeftTrackMID;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> TracksInstances_R;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Chassis")
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> TracksInstances_L;

	UPROPERTY(BlueprintReadWrite, Category = "Default")
	TObjectPtr<USplineComponent> TrackPath_L;

	UPROPERTY(BlueprintReadWrite, Category = "Default")
	TObjectPtr<UChaosWheeledVehicleMovementComponent> VehicleMovement;

	// ---------------------------------------------------------------------
	// Phase 11 (retry, MIDDLE PATH): vibration + sagging tuning.
	//
	// The first Phase 11 attempt moved the WheelRadius* group and lost 19 of 24
	// per-tank overrides. See CLAUDE.md - moving a variable to C++ discards child
	// Blueprint Class Defaults overrides. That attempt was reverted.
	//
	// The middle path: move a tuning value ONLY when a C++ function will read it,
	// and prefer values with no override risk. Every variable below is verified
	// IDENTICAL across all six tanks, so there is no override to lose. The 18
	// values that genuinely differ per tank stay in the Blueprint until a function
	// port actually needs them, and then only with the full re-application
	// procedure in CLAUDE.md.
	//
	// Why these nine: they are the vibration/sagging subsystem's tuning, ported as
	// a unit. Two are read directly by the smallest leaf functions -
	// VibrationCalculation reads TrackFrequency and nothing else; SaggingCalculation
	// reads SaggingMaxDistance and nothing else (everything else in both is passed
	// as a parameter). The remaining seven are read by their callers.
	//
	// EditDefaultsOnly: these are designer-tunable configuration, and the Blueprint
	// has Instance Editable unchecked. Without an Edit specifier they would vanish
	// from Class Defaults and could never be re-tuned.
	// ---------------------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Chassis|Vibration")
	double SpeedInfluence = 0.3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Chassis|Vibration")
	double MaxSpeedInfluence = 0.6;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Chassis|Vibration")
	double AccelerationInfluence = 0.2;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Chassis|Vibration")
	double MaxAccelerationInfluence = 1.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Chassis|Vibration")
	double TrackFrequency = 1500.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Chassis|Vibration")
	double DecayRate = 0.3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Chassis|Vibration")
	double InteractionAmplitudeMultiplier = 0.4;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Chassis")
	double SaggingMaxDistance = 20.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Chassis")
	double ProportionalCoefficient = 5.0;

	// ---------------------------------------------------------------------
	// Phase 12: FIRST FUNCTION MOVE.
	//
	// Ported 1:1 from the Blueprint graph, which was:
	//   VibrationOffset = VibrationAmplitude
	//                   * DegSin(VibrationPhase + GetTimeSeconds() * TrackFrequency)
	//
	// Signature matching rules learned here - the call site rebinds by NAME, so
	// every part of the signature has to match the Blueprint's exactly:
	//   * BlueprintCallable, NOT BlueprintPure. The Blueprint function is impure
	//     (is_pure=false) and its call site wires exec pins. A pure function has no
	//     exec pins, which would break that connection.
	//   * The output is a NAMED output pin, "VibrationOffset". Returning a double
	//     from C++ would produce a pin called "ReturnValue" instead and orphan the
	//     link, so it is an out-parameter with the exact original name.
	//   * Parameter names must match too - pins are matched by name, not position.
	//   * Category and tooltip carried across so the node looks identical.
	// ---------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Chassis", meta = (ToolTip = "Track vibration calculation"))
	void VibrationCalculation(double VibrationAmplitude, double VibrationPhase, double& VibrationOffset);

	// ---------------------------------------------------------------------
	// Phase 13: HullAccelerationDefinition. Second function moved.
	//
	// Ported 1:1:
	//   V = Mesh->GetPhysicsLinearVelocity()
	//   HullAccelerationWorldInverted = HullSpeedWorld (previous frame) - V
	//   HullSpeedWorld = V
	// Order matters - the old HullSpeedWorld is read before being overwritten.
	//
	// Chosen over SaggingCalculation, which is BLOCKED - see CLAUDE.md. Its
	// parameter `HullDeltaXLocation` collides with the member of the same name
	// moved in Phase 7, and UHT rejects a parameter that shadows a UPROPERTY.
	// This function has NO parameters, so it cannot hit that.
	//
	// The Blueprint declares an unused local `HullAcceleration` (FVector); no node
	// references it, so it is deliberately not reproduced.
	// ---------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Default", meta = (ToolTip = "Calculates the force of inertia"))
	void HullAccelerationDefinition();

	// ---------------------------------------------------------------------
	// Phase 14: tuning values REQUIRED BY A FUNCTION PORT, with per-tank
	// overrides. This is the first group where the six tanks genuinely differ,
	// so the Phase 11 failure mode applies and the overrides must be re-applied
	// explicitly after the move (see CLAUDE.md).
	//
	// Moved because UpdateTracksMID reads both and cannot be ported otherwise -
	// exactly the middle-path criterion: move a tuning value only when a C++
	// function needs it.
	//
	// Recorded values that MUST survive (re-applied per child Blueprint):
	//   TilingSegmentLength  master 70   T90 69.58  Leo 66.424  M1A2 78.66
	//                                    Merk 42.56 Proxy 36.7  VK 29.15
	//   InvertTrackDirection master False  T90/Leo/M1A2/Merk/Proxy True  VK False
	//
	// InvertTrackDirection is the dangerous one: the master default is False but
	// five of six tanks override it to True, so a lost override silently flips
	// track direction on five tanks.
	// ---------------------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Chassis")
	double TilingSegmentLength = 70.0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Default")
	bool InvertTrackDirection = false;

	// ---------------------------------------------------------------------
	// Phase 14: UpdateTracksMID. Third function moved.
	//
	// Ported 1:1:
	//   if IsValid(MaterialInstance):
	//       V     = fmod(ChassisDistance / TilingSegmentLength, 1.0)
	//       Value = InvertTrackDirection ? -V : V
	//       MaterialInstance->SetScalarParameterValue("OffsetV", Value)
	//
	// This is why TilingSegmentLength and InvertTrackDirection moved above - the
	// function cannot be ported without them, which is the middle-path criterion.
	//
	// Impure (is_pure=false) so BlueprintCallable; two call sites in the EventGraph
	// wire exec pins. Neither parameter name collides with a member, so no UHT
	// shadowing problem (contrast SaggingCalculation).
	//
	// Only runs when UseGeometricTracks is FALSE - the UV-track path. Every tank
	// ships with UseGeometricTracks=True, so this does NOT execute in a normal PIE
	// session; it is verified by calling it directly.
	// ---------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Chassis", meta = (ToolTip = "Updates dynamic material instance"))
	void UpdateTracksMID(UMaterialInstanceDynamic* MaterialInstance, double ChassisDistance);

	// ---------------------------------------------------------------------
	// Phase 16: SaggingCalculation. Unblocked by the Phase 15 parameter rename.
	//
	// Ported 1:1:
	//   Selected = ChassisLocked ? (InHullDeltaXLocation * -1) : ChassisDeltaDistance
	//   SaggingDegreeNew = Clamp(SaggingDegree + Selected / SaggingMaxDistance, 0, 1)
	//
	// BlueprintPURE, unlike the other ported functions: get_function_signature reports
	// is_pure=true and both EventGraph call sites carry data pins only, no exec pins.
	// (The function graph's own entry/result nodes do show exec pins internally - that
	// is an artifact. Trust the CALL SITE, not the graph.)
	//
	// The parameter is InHullDeltaXLocation, not HullDeltaXLocation: UHT forbids a
	// parameter shadowing the member of that name moved in Phase 7. The Blueprint
	// parameter was renamed first (Phase 15) so the pin name still matches.
	//
	// `SaggingDegree` is the input parameter, not a member - there is no member of
	// that name, only SaggingDegreeR/L.
	// ---------------------------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "Chassis", meta = (ToolTip = "Calculation of the amount of sagging tracks"))
	void SaggingCalculation(double SaggingDegree, double InHullDeltaXLocation, double ChassisDeltaDistance, bool ChassisLocked, double& SaggingDegreeNew) const;

	// ---------------------------------------------------------------------
	// Phase 17: UseGeometricTracks + WheelRotationDefinition.
	//
	// UseGeometricTracks is the ONLY member WheelRotationDefinition reads; it is
	// True on the master and all six tanks, so there is no override to lose.
	// EditAnywhere because the Blueprint has Instance Editable CHECKED (contrast
	// the EditDefaultsOnly tuning values above).
	//
	// Everything else the function needs arrives as a PARAMETER - wheel radius,
	// track thickness, start angles, speed correction - so those per-tank values
	// stay in the Blueprint and are passed in by the callers. A function taking its
	// tuning as parameters does not require that tuning to be moved.
	//
	// !! PERMANENT CONSTRAINT !!
	// TrackThickness and WheelSpeedCorrectionUV must NEVER be moved to C++. They are
	// parameter names of this function, and UHT forbids a parameter shadowing a
	// UPROPERTY - moving either would retroactively break this port (the Phase 13
	// blocker). C++ never needs them, since they arrive as parameters.
	// ---------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chassis")
	bool UseGeometricTracks = true;

	// Ported 1:1:
	//   SpeedCorr     = UseGeometricTracks ? 0.0 : WheelSpeedCorrectionUV
	//   Circumference = 2 * (WheelRadius + TrackThickness + SpeedCorr) * PI
	//   StartAngle    = UseGeometricTracks ? (LeftWheel ? LeftGeo : RightGeo)
	//                                      : (LeftWheel ? LeftUV  : RightUV)
	//   Degrees       = -360 * (Distance / Circumference) + StartAngle
	UFUNCTION(BlueprintCallable, Category = "Chassis")
	void WheelRotationDefinition(double Distance, double WheelRadius, double TrackThickness, double WheelSpeedCorrectionUV, double WheelStartAngleLeftGeoTracks, double WheelStartAngleRightGeoTracks, double WheelStartAngleLeftUVTracks, double WheelStartAngleRightUVTracks, bool LeftWheel, double& Degrees);

	// ---------------------------------------------------------------------
	// Phase 18: RecalculateGunAndTurretRotation. No parameters, no outputs.
	//
	// Re-bases the unstabilized turret/gun rotations when the stabilizer is
	// toggled, so the turret does not jump. Ported 1:1:
	//
	//   Sign        = Stabilization ? 1.0 : -1.0                (Select Float A/B)
	//   RotCorrector = MakeRotator(0, 0, ActorRotation.Yaw * Sign)
	//   for each i:  TurretsRotUnstabilized[i] = Compose(elem, RotCorrector)
	//   RotCorrector = MakeRotator(0, Mesh socket "turret" Pitch * Sign, 0)
	//   for each i:  GunsRotUnstabilized[i]    = Compose(elem, RotCorrector)
	//
	// RotCorrector is a function-LOCAL variable in the Blueprint, not a member -
	// it does not appear in the variable list, so it stays a C++ local. Both
	// loops read and write the SAME array in place (the ForEach's array pin and
	// the SetArrayElem's target pin are two Get nodes for one variable).
	//
	// Only member read is Stabilization, moved just above for this port.
	// The two arrays moved in Phase 8 and Mesh is the native pawn mesh, so
	// nothing else needed migrating.
	//
	// Not BlueprintPure: get_functions reports is_pure=false and the single
	// EventGraph call site wires exec pins.
	// ---------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Default")
	void RecalculateGunAndTurretRotation();

	// ---------------------------------------------------------------------
	// Multiplayer jitter fix (NOT a port phase - this CHANGES behaviour).
	//
	// TurretsAndGunsRotCalculation is the only writer of TurretsRot/GunsRot (it
	// feeds them into SetArrayElem; ScatteringCalculation only reads via
	// GetArrayItem). Event Tick is gated solely on NOT Destroyed, so a client
	// recomputes those arrays every frame for EVERY tank - including tanks it does
	// not control - and overwrites whatever the server replicated down. Two writers
	// alternating is the observed jitter.
	//
	// This helper gates that recompute: simulate locally only when we own the pawn
	// (authority) or are the one aiming it (locally controlled). Anyone else should
	// consume the replicated value instead.
	//
	// Single player is unaffected: the pawn is both authority and locally
	// controlled, so this returns true and behaviour is identical.
	// ---------------------------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "Networking", meta = (ToolTip = "True when this machine should compute turret/gun rotation itself rather than use the replicated value."))
	bool IsTurretSimulatedLocally() const;

	// ---------------------------------------------------------------------
	// Multiplayer fix 2: carry a client's AIM POINT up to the server.
	//
	// Control rotation does NOT replicate for a Pawn (it does for Character), and
	// this pawn has bUseControllerRotationYaw=false, so the server genuinely cannot
	// know where a client is looking. See the multiplayer section of CLAUDE.md.
	//
	// The turret is aimed by a line trace from the Camera in
	// TurretsAndGunsRotCalculation, producing a world-space TargetPoint. On the
	// server, a remote client's camera is not looking anywhere useful, so its trace
	// is meaningless. The owning client therefore sends its traced point up, and the
	// server uses that instead of tracing.
	//
	// An earlier attempt sent Rep_ControlRotation instead. That failed because
	// nothing live reads it - its only readers are the dead UpdateTurretRotation_Old
	// and UpdateMachineGunRotation_Old. Check a variable's consumers before building
	// on its name.
	//
	// Unreliable: this fires every frame. A reliable RPC at that rate can disconnect
	// the client. BlueprintCallable is required for a Server RPC invoked from a graph.
	// ---------------------------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "Networking", meta = (ToolTip = "True only on a locally-controlled client, i.e. we must send our aim to the server."))
	bool ShouldSendAimToServer() const;

	// True when a player on THIS machine is the Gunner of THIS tank, per the framework's crew
	// assignment.
	//
	// Needed because under the three-crew model NOBODY possesses the tank - each crew member
	// possesses their own VR pawn - so IsLocallyControlled() is false on every machine and can
	// no longer answer "should I simulate this turret locally".
	UFUNCTION(BlueprintPure, Category = "Networking")
	bool IsLocalGunnerOfThisTank() const;

	// Which crew seat on THIS tank a player sitting at THIS machine holds, or None.
	//
	// The general form of IsLocalGunnerOfThisTank, which now defers to it. Everything that has to
	// ask "is the person in front of this screen crewing this particular tank, and in what seat"
	// wants this: turret simulation, and which periscope is worth rendering.
	//
	// Returns None on a dedicated server and on every remote copy of a tank, which is exactly the
	// answer those machines need.
	UFUNCTION(BlueprintPure, Category = "Networking")
	ETSCrewRole GetLocalCrewRoleOnThisTank() const;

	// Written on the SERVER by ServerSetAimPoint. Not replicated: clients never need
	// it, they receive the finished TurretsRot/GunsRot.
	UPROPERTY(BlueprintReadWrite, Category = "Networking")
	FVector ReceivedAimPoint = FVector::ZeroVector;

	// How far ahead the turret aims before any Gunner has aimed. Only a placeholder target - the
	// first real aim point replaces it.
	UPROPERTY(EditDefaultsOnly, Category = "Networking")
	float DefaultAimDistance = 100000.f;

	// Run the Chaos vehicle simulation on the SERVER ONLY, and let clients take the tank's motion
	// from replication.
	//
	// The Blueprints must ship bRequiresControllerForInputs = False or the tank does not move at all
	// (nobody possesses it, so Chaos skips gear shifting and the whole mechanical simulation - see
	// ChaosVehicleMovementComponent.cpp:1177). But False means bProcessLocally is true on EVERY
	// machine, so each client would also run its own unsynchronised sim from the replicated drive
	// input. That is not client prediction: there is no reconciliation, so the two sims drift and
	// then movement replication yanks the client copy back - the same two-writers-fighting fault as
	// the turret jitter documented in CLAUDE.md.
	//
	// So BeginPlay puts the flag back to True on clients, where the absence of a controller then
	// makes Chaos skip the sim exactly as before this was ever touched. The flag is a plain
	// (non-replicated) bool, so this per-machine split is safe.
	//
	// Turn this off to let every client simulate locally - do that only with a two-window listen
	// server test in front of you.
	UPROPERTY(EditDefaultsOnly, Category = "Networking")
	bool bSimulateVehicleOnAuthorityOnly = true;

	virtual void BeginPlay() override;

	// Exists for the crew station views, and the ordering is the whole point - see
	// UpdateCrewViewCapture. Super::Tick runs the Blueprint's Event Tick, so the turret has already
	// been rotated for this frame by the time the sight is captured.
	// InDeltaSeconds, not DeltaSeconds: this class has a ported Blueprint member of that name and
	// UHT builds with -WarningsAsErrors, so the shadow is a hard error (CLAUDE.md).
	virtual void Tick(float InDeltaSeconds) override;

	// Server simulates, clients replicate. See bSimulateVehicleOnAuthorityOnly.
	void ApplyVehicleSimulationAuthorityPolicy();

	UFUNCTION(Server, Unreliable, BlueprintCallable, Category = "Networking")
	void ServerSetAimPoint(FVector NewAimPoint);

	// =====================================================================
	// ITSTankInterface - the stable contract the multiplayer framework calls.
	//
	// This is the join between the C++ framework and this tank. The framework
	// decides WHO may act (team, crew role, permission matrix); these five
	// functions are HOW this particular tank carries the action out.
	//
	// They are BlueprintNativeEvents, so the BLUEPRINT is expected to override
	// every one of them - that is the point. The tank's real behaviour lives in
	// Blueprint graphs (ThrottleControl, TurningControl, the weapon component,
	// the turret chain), and most of it can never move to C++ because it touches
	// Blueprint-generated types. See CLAUDE.md "NOT PORTABLE".
	//
	// The C++ defaults below therefore do NOT implement tank behaviour. They log
	// a warning, so a missing Blueprint override is loud instead of silent - the
	// failure mode otherwise is "the tank simply does not respond" with nothing
	// in the log to explain it.
	//
	// We deliberately do NOT drive the vehicle directly here (e.g. calling
	// SetThrottleInput on the movement component). That would bypass
	// ThrottleControl/TurningControl, which carry the gearing, speed limiting,
	// MoveRightAxis state and the PoliceTurn behaviour. Bypassing them would
	// "work" in a smoke test and be wrong in every real case.
	// =====================================================================
	virtual void BP_SetDriveInput_Implementation(float Throttle, float Steering) override;
	virtual void BP_AimTurret_Implementation(FVector_NetQuantize AimPoint) override;
	virtual void BP_FireMainCannon_Implementation() override;
	virtual void BP_FireMachineGun_Implementation() override;

	// --- Weapon hooks -----------------------------------------------------------------------------
	// The tank's actual firing lives on BP_TankWeapon (StartShooting / StopShooting), and
	// BP_TankWeapon_C is a Blueprint-generated type C++ cannot name (see CLAUDE.md, NOT PORTABLE).
	// So the Blueprint implements these two as one node each, and C++ keeps the TIMING - which is
	// the part that actually needs logic, because the framework's fire events are single shots while
	// the weapon is a hold-to-fire pair.
	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|Blueprint Integration")
	void BP_WeaponStartShooting();

	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|Blueprint Integration")
	void BP_WeaponStopShooting();

	// --- Crew seats that ride the turret ----------------------------------------------------------
	// DriverSeat/GunnerSeat/CommanderSeat are authored as plain scene components on the tank, which
	// puts them all on the HULL. That is right for the Driver and wrong for the other two: a Gunner
	// and Commander sit in the turret basket and must traverse with the gun, or the turret swings
	// around them while they stay facing the hull's forward.
	//
	// It cannot be fixed by parenting in the Blueprint through this project's tooling: the seats are
	// SCS components and the turret bone lives on VehicleMesh, an inherited NATIVE component, which
	// the reparent action cannot target. So the attach happens here instead, once, at BeginPlay.
	//
	// KeepWorldTransform is deliberate: designers keep placing seats in the viewport in hull space
	// exactly as before, and this only changes what they RIDE, never where they start. That keeps
	// RULE 8 intact - placement stays Blueprint data, C++ only does the plumbing.
	void AttachTurretCrewSeats();

	// Bone/socket on VehicleMesh that the turret crew ride. Per-tank because the vendor meshes do not
	// have to agree on a bone name (VK1602 uses "turret").
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew Station")
	FName TurretSocketName = TEXT("turret");

	// Which seat components ride the turret. The Driver is deliberately absent - the driver's station
	// is in the hull and must NOT rotate with the gun.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew Station")
	TArray<FName> TurretMountedSeatComponents = { TEXT("GunnerSeat"), TEXT("CommanderSeat") };

	// Make every non-root skeletal mesh (the crew interior) tick AFTER this pawn.
	//
	// TurretsAndGunsRotCalculation writes TurretsRot in the pawn's Event Tick. Without an explicit
	// prerequisite the interior mesh can evaluate its AnimBP BEFORE that runs, so it draws last
	// frame's turret angle while the exterior gun draws this frame's - which reads as the interior
	// lagging behind the gun, and only the interior, because VehicleMesh is the root and does not
	// have the problem.
	//
	// Deliberately skips the root VehicleMesh: that one carries the vehicle physics, and reordering
	// its tick against the pawn is not worth the risk to fix a problem it does not have.
	void SyncInteriorMeshTickToPawn();

	// Smoothed toward CurrentDriveInput every frame; read by the interior AnimBP through the
	// GetInterior*Alpha accessors above. Driven from Tick, which is declared once, above.
	void UpdateInteriorControlState(float DeltaTime);

	// --- Interior turret bone ---------------------------------------------------------------------
	// The crew compartment is a SEPARATE skeletal mesh from the hull, with its own skeleton, so the
	// exterior ABP does not touch it. Its turret basket bone (b_Upper on the VK1602) has to be driven
	// from the same turret rotation the outside uses, or a Gunner traverses the gun and the interior
	// the crew are sitting in stays put.
	//
	// Read by ABP_<Tank>_Interior's Transform (Modify) Bone node. Yaw only: the basket spins, it does
	// not elevate - gun elevation is a different bone on the exterior mesh.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Turret")
	FRotator GetInteriorTurretRotation() const;

	// Which component of TurretsRot[0] feeds the interior bone, and how it maps onto that bone's local
	// axes. EditDefaultsOnly because the answer is per-mesh: whoever rigged the interior chose the
	// bone orientation, and it will not always match the hull's.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Turret")
	bool bInvertInteriorTurretYaw = false;

	// Extra yaw applied after the invert, for a basket whose bind pose is not facing forward.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Turret")
	float InteriorTurretYawOffset = 0.f;

	// --- Interior DRIVER controls -----------------------------------------------------------------
	// The crew compartment has driver control bones (b_Gas, b_Brake, b_Brake_001, b_L_Lever,
	// b_R_Lever on the VK1602). They are driven from CurrentDriveInput, which is REPLICATED, so a
	// remote crew member watching the driver sees the same lever positions - the animation is not a
	// local guess reconstructed from input this machine happens to own.
	//
	// C++ publishes normalised values and leaves the geometry to the AnimBP, which is where the bone
	// axis and travel actually live. See GetInteriorLeverRotation for why an angle is offered too.

	// 0..1, how far the gas pedal is pressed. Forward throttle only.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Interior")
	float GetInteriorThrottleAlpha() const { return DisplayThrottle; }

	// 0..1, how far the brake pedal is pressed.
	//
	// ASSUMPTION, stated because it is a design decision and not a fact about the tank: reverse
	// throttle drives the brake pedal. This vehicle has no separate brake input - braking is applied
	// through negative throttle - so there is no dedicated brake channel to read. If a real brake
	// input is added later, point this at that instead.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Interior")
	float GetInteriorBrakeAlpha() const { return DisplayBrake; }

	// -1..1. Negative pulls the LEFT lever, positive the RIGHT - a tracked vehicle steers by braking
	// the track on the side it turns towards, so the lever that moves is the direction of travel.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Interior")
	float GetInteriorSteeringAlpha() const { return DisplaySteering; }

	// 0..1 per lever, split out so the AnimBP needs no Select node per side.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Interior")
	float GetInteriorLeftLeverAlpha() const { return FMath::Max(-DisplaySteering, 0.f); }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Interior")
	float GetInteriorRightLeverAlpha() const { return FMath::Max(DisplaySteering, 0.f); }

	// Convenience rotators, so a Transform (Modify) Bone node can be fed directly instead of wiring a
	// multiply per bone. The ANGLE is Blueprint data (below) because the travel differs per tank;
	// which AXIS is correct depends on the bone's local frame - b_Upper needed Component space
	// precisely because its frame is rolled. Expect to try more than one and check against the mesh.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Interior")
	FRotator GetInteriorGasPedalRotation() const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Interior")
	FRotator GetInteriorBrakePedalRotation() const;

	// bLeft picks the lever. Both use the same travel angle; they differ only in which alpha drives
	// them and, if the mesh is mirrored, the sign.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Interior")
	FRotator GetInteriorLeverRotation(bool bLeft) const;

	// Travel at full input, in degrees. Per-tank data - each interior is modelled differently.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Interior")
	FRotator GasPedalFullTravel = FRotator(-18.f, 0.f, 0.f);

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Interior")
	FRotator BrakePedalFullTravel = FRotator(-18.f, 0.f, 0.f);

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Interior")
	FRotator LeverFullTravel = FRotator(-25.f, 0.f, 0.f);

	// Mirrors the right lever's travel. Off by default: whether the two levers need opposite signs
	// depends on how the interior was modelled, so it is a switch rather than an assumption.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Interior")
	bool bMirrorRightLeverTravel = false;

	// How fast the controls chase the input, in units per second. Raw input is a STEP - a stick or a
	// key goes 0 -> 1 in one frame - and a pedal that teleports reads as broken. 0 disables smoothing.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Interior", meta = (ClampMin = "0.0"))
	float InteriorControlInterpSpeed = 8.f;

	// --- Where the gun is ACTUALLY pointing -------------------------------------------------------
	// Turret traverse and gun elevation as one rotation in the tank's own space: yaw from the turret,
	// pitch from the main gun. Both come out of TurretsRot/GunsRot, the same arrays the AnimBP draws
	// the mesh from, so a reader of this cannot disagree with what the player sees on the barrel.
	//
	// This is the ACHIEVED rotation, already rate-limited by UpdateTurretRotation/UpdateGunRotation -
	// deliberately not the requested one. ATSVRPawn hangs the Gunner's sight off it so the view can
	// only turn as fast as the gun does.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Turret")
	FRotator GetMainGunAimRotation() const;

	// World location of the turret socket - the point the aim maths actually pivots about.
	//
	// UpdateTurretRotation resolves the aim POINT into an angle from here (SocketToTargetTurret),
	// so anyone building an aim point out of a direction has to project it from this location. Fire
	// the same ray from anywhere else - the Gunner's eye, say - and the angle that comes back is not
	// the angle that was asked for, by an error that scales with 1/range.
	//
	// Falls back to the actor location if the socket is missing, which is where the aim previously
	// pivoted anyway and is still far better than the seat.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Turret")
	FVector GetTurretPivotLocation() const;

	// =====================================================================
	// Crew station views - the periscope / sight render targets.
	//
	// Each station has a SceneCaptureComponent2D on the tank Blueprint drawing into a render target
	// that a screen mesh inside the crew compartment displays. A scene capture is close to a whole
	// extra render of the world, so the cost of getting this wrong is not subtle: shipped as
	// authored, EVERY capture on EVERY tank ran with bCaptureEveryFrame on EVERY machine - the
	// server and every remote copy included - so a two-tank match rendered the world four extra
	// times a frame for views nobody was looking through.
	//
	// The rule here is that a capture runs ONLY for the seat the player at this machine is actually
	// sitting in, and only on their own tank. One capture, one machine, ever.
	//
	// That is not only about frame time. The render targets are shared ASSETS: two tanks capturing
	// into RT_Gunner would take turns overwriting each other and both sights would show the wrong
	// tank's view. Gating to the local crew member's own tank is what makes a single shared asset
	// per station correct.
	//
	// Placement, render targets and materials stay Blueprint data (RULE 8). C++ owns only WHICH
	// capture runs, HOW OFTEN, and the settings that exist purely to make it affordable.
	// =====================================================================

	// Capture component name per crew seat, looked up on this tank by name. Adding the Commander's
	// view is filling in a row here plus authoring the component - no code change.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew View")
	TMap<ETSCrewRole, FName> CrewViewCaptureComponents;

	// How often the active station's view is re-rendered, in captures per second. 0 means every
	// frame.
	//
	// This is the main cost dial and it trades directly against latency: at 30 the image can be up
	// to 33ms behind the world, which is fine through a periscope and starts to be noticeable when
	// traversing a sight quickly. Raise it if the sight feels detached, lower it if the frame rate
	// suffers - but resize the render target first, that is the bigger lever by far.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew View", meta = (ClampMin = "0.0", ClampMax = "240.0"))
	float CrewViewCaptureHz = 30.f;

	// Strip the effects a periscope image does not need. Off renders the station view with the same
	// settings as the main view, which looks marginally better and costs a great deal more.
	//
	// Eye adaptation is in the list for a second reason beyond cost: with it on, the sight
	// re-exposes as the gun traverses across bright and dark parts of the world, so the image
	// visibly pumps while the player is trying to aim through it.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew View")
	bool bApplyCrewViewPerformanceDefaults = true;

	// Draw distance for station views, in cm. -1 uses the world's. A periscope looking at nearby
	// terrain does not need the far LODs the main view draws.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew View")
	float CrewViewMaxDrawDistance = -1.f;

	// How often the local crew role is re-checked. Crew assignments change on the order of seconds,
	// so polling this every frame on every tank would cost more than it is worth.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew View", meta = (ClampMin = "0.05"))
	float CrewViewRoleRefreshSeconds = 0.25f;

	// --- Vision modes (day / night vision / thermal) ----------------------------------------------
	//
	// A viewing filter on the LOCAL crew member's station capture, nothing more. It changes post
	// processing on one SceneCaptureComponent2D on one machine, so it is deliberately NOT replicated
	// and deliberately NOT server-validated: switching to thermal reveals nothing the player's own
	// periscope was not already rendering.
	//
	// Two ways to author a mode, and they are not alternatives to each other:
	//   * leave VisionModeMaterials empty and the built-in colour grading below is used. Works with
	//     no assets at all, which is why it exists - a mode that needs a material nobody has authored
	//     yet is a black screen.
	//   * assign a post-process material for a mode and it is blended over the top instead. That is
	//     the route to a real thermal ramp (inverted luminance through a gradient), which colour
	//     grading alone cannot express.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Crew View")
	void SetCrewViewVisionMode(ETSVisionMode NewMode);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Crew View")
	ETSVisionMode GetCrewViewVisionMode() const { return CrewViewVisionMode; }

	// Normal -> NightVision -> Thermal -> Normal. Returns the mode now in effect.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Crew View")
	ETSVisionMode CycleCrewViewVisionMode();

	// The render target the given station draws into, or null when that station has no capture or
	// the capture has no TextureTarget assigned. A widget showing a periscope feed asks for this
	// rather than hard-referencing RT_Gunner, so per-tank overrides work.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Crew View")
	class UTextureRenderTarget2D* GetCrewViewRenderTarget(ETSCrewRole InCrewRole) const;

	// Optional per-mode post-process material. Blueprint data (RULE 8) - no constructor load
	// (RULE 2). A row left unset falls back to the built-in grading.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew View")
	TMap<ETSVisionMode, TObjectPtr<class UMaterialInterface>> VisionModeMaterials;

	// Brightness multiplier for the built-in night vision look. This is a COLOUR GAIN, not an
	// exposure bias, on purpose: ConfigureCrewViewCapture turns the EyeAdaptation show flag off, so
	// an exposure bias would have nothing to bias.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew View", meta = (ClampMin = "1.0"))
	float NightVisionGain = 5.f;

	// The phosphor tint night vision is graded towards.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew View")
	FLinearColor NightVisionTint = FLinearColor(0.14f, 1.f, 0.32f, 1.f);

	// Contrast of the built-in white-hot thermal look. 1 is neutral; the point of thermal is that it
	// is not.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Crew View", meta = (ClampMin = "0.1"))
	float ThermalContrast = 2.f;

	// Silences every station capture on this tank. Called once, on every machine, so that a view
	// only ever renders because the code below deliberately turned it on.
	void InitialiseCrewViewCaptures();

	// Picks the capture matching the local crew seat and drives it. Runs from Tick, AFTER the
	// Blueprint's Event Tick has written TurretsRot - so a sight riding the turret captures this
	// frame's gun angle rather than last frame's, the same ordering SyncInteriorMeshTickToPawn
	// fixes for the interior mesh.
	void UpdateCrewViewCapture(float InDeltaSeconds);

	// Applied once, when a capture is first switched on for a station.
	void ConfigureCrewViewCapture(class USceneCaptureComponent2D* Capture) const;

	// One trigger pull: how long StartShooting stays held for a main-cannon shot.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Weapons", meta = (ClampMin = "0.01"))
	float MainCannonTriggerHoldSeconds = 0.15f;

	// Hold-to-fire release delay for the machine gun. ServerFireMachineGun arrives once per frame
	// while the key is held, so the gun keeps firing until requests stop arriving for this long.
	// Must comfortably exceed one frame, or the gun stutters on a hitch or a dropped packet
	// (ServerFireMachineGun is Unreliable).
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Weapons", meta = (ClampMin = "0.05"))
	float MachineGunReleaseDelaySeconds = 0.25f;

private:
	float DisplayThrottle = 0.f;
	float DisplayBrake = 0.f;
	float DisplaySteering = 0.f;


	FTimerHandle WeaponStopTimerHandle;
	bool bWeaponFiring = false;
	void ReleaseWeaponTrigger();

	// Resolved once at BeginPlay from CrewViewCaptureComponents - the name lookup is not worth
	// repeating every frame.
	UPROPERTY(Transient)
	TMap<ETSCrewRole, TObjectPtr<class USceneCaptureComponent2D>> ResolvedCrewViewCaptures;

	// The one capture currently allowed to render, and the seat it belongs to. Null and None on a
	// dedicated server, on every remote copy of this tank, and on a machine whose player is crewing
	// a different tank - which is most of them.
	UPROPERTY(Transient)
	TObjectPtr<class USceneCaptureComponent2D> ActiveCrewViewCapture;

	ETSCrewRole ActiveCrewViewRole = ETSCrewRole::None;

	// Local-only viewing filter. See SetCrewViewVisionMode.
	ETSVisionMode CrewViewVisionMode = ETSVisionMode::Normal;

	// What the Blueprint authored on each station capture, snapshotted the first time that station
	// is configured. Switching back to Normal restores THIS rather than an engine default, so a
	// designer's own grading on a periscope is not quietly thrown away by using the mode switch.
	UPROPERTY(Transient)
	TMap<ETSCrewRole, FPostProcessSettings> AuthoredCrewViewPostProcess;

	// Re-applies CrewViewVisionMode to whichever capture is currently live. Safe to call when none
	// is - it does nothing.
	void ApplyVisionModeToActiveCapture();

	float CrewViewCaptureAccumulator = 0.f;
	float CrewViewRoleRefreshAccumulator = 0.f;

public:
	virtual void BP_UpdateCommanderIntel_Implementation(const FTSCommanderIntel& Intel) override;
};
