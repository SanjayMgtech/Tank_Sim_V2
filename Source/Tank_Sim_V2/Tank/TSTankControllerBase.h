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
};
