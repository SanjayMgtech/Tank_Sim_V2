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
#include "TSTankControllerBase.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UInstancedStaticMeshComponent;
class USplineComponent;
class UChaosWheeledVehicleMovementComponent;

UCLASS(Blueprintable, BlueprintType)
class TANK_SIM_V2_API ATSTankControllerBase : public AWheeledVehiclePawn
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
	// Two verification classes, because reference sites differ:
	//   LIVE   - written every tick via TurretsAndGunsRotCalculation / EventGraph:
	//            MainTurretAndGunRotation, TurretBlocking, IsTurretRotating.
	//   LEGACY - referenced only from UpdateTurretRotation_Old and
	//            UpdateMachineGunRotation_Old: TurretRotation, MGRotation,
	//            TurretYaw, TurretPitch. These stay 0 in a normal PIE session by
	//            design; that is NOT a failed rebind. They are proven by calling
	//            those _Old functions directly and observing the writes land.
	// ---------------------------------------------------------------------
	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Turret")
	FRotator MainTurretAndGunRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Turret")
	bool TurretBlocking = false;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Turret")
	bool IsTurretRotating = false;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Turret")
	FRotator TurretRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Turret")
	double TurretYaw = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Turret")
	double TurretPitch = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Hidden (Used for logic)|Machine gun")
	FRotator MGRotation = FRotator::ZeroRotator;

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
	FRotator Rep_ControlRotation = FRotator::ZeroRotator;

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
};
