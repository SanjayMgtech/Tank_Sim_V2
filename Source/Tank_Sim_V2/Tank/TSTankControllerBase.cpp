#include "Tank/TSTankControllerBase.h"

#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

ATSTankControllerBase::ATSTankControllerBase()
{
	// Phase 8: restore the Blueprint's pre-sized array defaults.
	//
	// These are the ONLY statements this constructor is allowed to grow. The
	// Blueprint shipped these arrays already sized, and the graph indexes into
	// them directly, so leaving them empty would produce
	// "Attempted to access index N from array of length 0" at runtime.
	// Lengths verified identical across all six per-tank Blueprints.
	//
	// This does not weaken RULE 1 or RULE 2: no components are created and no
	// assets are loaded here.
	AntennaCurrentSpeed.Init(FVector::ZeroVector, 30);
	TurretsRotUnstabilized.Init(FRotator::ZeroRotator, 10);
	TurretsRotPrevFrame.Init(FRotator::ZeroRotator, 10);
	GunsRotUnstabilized.Init(FRotator::ZeroRotator, 10);
	GunsRotPrevFrame.Init(FRotator::ZeroRotator, 10);

	// Phase 9: the replicated rotator arrays are pre-sized to 10 in the Blueprint
	// exactly like their non-replicated counterparts above.
	TurretsRot.Init(FRotator::ZeroRotator, 10);
	GunsRot.Init(FRotator::ZeroRotator, 10);

	// Otherwise deliberately empty.
	//
	// The Blueprint (BP_TankController_Chaos) supplies every component and every
	// default value. Recreating them here would re-base the components and silently
	// drop any property not manually copied - the failure mode that sank the first
	// port attempt (see CLAUDE.md section 4).
	//
	// Likewise, no ConstructorHelpers asset loading: FClassFinder on a Blueprint class
	// deadlocks the editor during Blueprint compilation.
}

void ATSTankControllerBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Every property carrying the Replicated specifier MUST be registered here.
	// The Blueprint replicated all three with the default condition (no RepNotify,
	// no replication condition), so plain DOREPLIFETIME reproduces that exactly.
	//
	// If a future phase moves another replicated Blueprint variable, add it here in
	// the same commit. Forgetting is silent in single-player and only breaks over
	// the network.
	DOREPLIFETIME(ATSTankControllerBase, Rep_ControlRotation);
	DOREPLIFETIME(ATSTankControllerBase, TurretsRot);
	DOREPLIFETIME(ATSTankControllerBase, GunsRot);
}

void ATSTankControllerBase::VibrationCalculation(double VibrationAmplitude, double VibrationPhase, double& VibrationOffset)
{
	// 1:1 port of the Blueprint graph. Node order there was:
	//   GetTimeSeconds -> (* TrackFrequency) -> (+ VibrationPhase) -> DegSin
	//                  -> (* VibrationAmplitude) -> VibrationOffset
	//
	// UKismetMathLibrary::DegSin is Sin(PI/180 * A), so DegreesToRadians here is
	// the same operation, not a re-derivation.
	const double TimeSeconds = UGameplayStatics::GetTimeSeconds(this);
	const double Phase = VibrationPhase + TimeSeconds * TrackFrequency;

	VibrationOffset = VibrationAmplitude * FMath::Sin(FMath::DegreesToRadians(Phase));
}

void ATSTankControllerBase::HullAccelerationDefinition()
{
	// 1:1 port. Graph order was:
	//   Get Mesh -> GetPhysicsLinearVelocity
	//   -> Set HullAccelerationWorldInverted = (HullSpeedWorld - Velocity)
	//   -> Set HullSpeedWorld = Velocity
	//
	// The subtraction uses the PREVIOUS frame's HullSpeedWorld, so it must happen
	// before HullSpeedWorld is reassigned. Reordering these two lines would
	// silently make the result always zero.
	//
	// Mesh is null-checked: the Blueprint would emit "Accessed None" and carry on
	// with a zero vector, so falling back to ZeroVector matches that behaviour
	// rather than crashing.
	// Not const: UPrimitiveComponent::GetPhysicsLinearVelocity is a non-const method.
	USkeletalMeshComponent* MeshComp = GetMesh();
	const FVector CurrentVelocity = MeshComp ? MeshComp->GetPhysicsLinearVelocity(NAME_None) : FVector::ZeroVector;

	HullAccelerationWorldInverted = HullSpeedWorld - CurrentVelocity;
	HullSpeedWorld = CurrentVelocity;
}

void ATSTankControllerBase::UpdateTracksMID(UMaterialInstanceDynamic* MaterialInstance, double ChassisDistance)
{
	// 1:1 port. The whole body sits inside the graph's IsValid check, so an
	// invalid MID is a silent no-op exactly as before.
	if (!IsValid(MaterialInstance))
	{
		return;
	}

	// UKismetMathLibrary::Divide_DoubleDouble and Percent_FloatFloat both return 0
	// on a zero divisor instead of faulting; mirror that rather than dividing blind.
	const double Scaled = FMath::IsNearlyZero(TilingSegmentLength) ? 0.0 : (ChassisDistance / TilingSegmentLength);
	const double Wrapped = FMath::Fmod(Scaled, 1.0);

	// Graph used a Select on the bool: Option 0 (false) = Wrapped, Option 1 (true)
	// = Wrapped * -1.
	const double Value = InvertTrackDirection ? (Wrapped * -1.0) : Wrapped;

	MaterialInstance->SetScalarParameterValue(TEXT("OffsetV"), static_cast<float>(Value));
}

void ATSTankControllerBase::SaggingCalculation(double SaggingDegree, double InHullDeltaXLocation, double ChassisDeltaDistance, bool ChassisLocked, double& SaggingDegreeNew) const
{
	// 1:1 port. Graph order was:
	//   InHullDeltaXLocation * -1 -> SelectFloat(A, ChassisDeltaDistance, bPickA=ChassisLocked)
	//   -> / SaggingMaxDistance -> + SaggingDegree -> Clamp(0, 1)
	//
	// SelectFloat returns A when bPickA is true. The graph comment on the negate node
	// reads "HullDeltaXLocation is used when braking", i.e. the locked-chassis branch.
	const double Selected = ChassisLocked ? (InHullDeltaXLocation * -1.0) : ChassisDeltaDistance;

	// UKismetMathLibrary::Divide_DoubleDouble returns 0 rather than dividing by zero.
	// SaggingMaxDistance is 20 on every tank today but is designer-editable.
	const double Scaled = FMath::IsNearlyZero(SaggingMaxDistance) ? 0.0 : (Selected / SaggingMaxDistance);

	SaggingDegreeNew = FMath::Clamp(SaggingDegree + Scaled, 0.0, 1.0);
}

void ATSTankControllerBase::WheelRotationDefinition(double Distance, double WheelRadius, double TrackThickness, double WheelSpeedCorrectionUV, double WheelStartAngleLeftGeoTracks, double WheelStartAngleRightGeoTracks, double WheelStartAngleLeftUVTracks, double WheelStartAngleRightUVTracks, bool LeftWheel, double& Degrees)
{
	// 1:1 port. SelectFloat(A, B, bPickA) returns A when bPickA is true.

	// Graph comment: "Wheel speed correction (when using UV tracks)" - the A pin is
	// left unconnected, i.e. literal 0.0, and is chosen when UseGeometricTracks.
	const double SpeedCorr = UseGeometricTracks ? 0.0 : WheelSpeedCorrectionUV;

	// Graph comment: "Circumference". The multiply node is 2 * sum * PI.
	const double Circumference = 2.0 * (WheelRadius + TrackThickness + SpeedCorr) * UE_DOUBLE_PI;

	// Graph comment: "WheelStartingAngle" - side first, then track mode.
	const double StartAngleGeo = LeftWheel ? WheelStartAngleLeftGeoTracks : WheelStartAngleRightGeoTracks;
	const double StartAngleUV = LeftWheel ? WheelStartAngleLeftUVTracks : WheelStartAngleRightUVTracks;
	const double StartAngle = UseGeometricTracks ? StartAngleGeo : StartAngleUV;

	// Kismet's Divide_DoubleDouble returns 0 on a zero divisor rather than faulting.
	const double Revolutions = FMath::IsNearlyZero(Circumference) ? 0.0 : (Distance / Circumference);

	Degrees = (-360.0 * Revolutions) + StartAngle;
}

bool ATSTankControllerBase::IsTurretSimulatedLocally() const
{
	// Same condition ReplicateControlRotation already uses to decide whether to
	// write Rep_ControlRotation, kept deliberately identical so the two agree.
	return HasAuthority() || IsLocallyControlled();
}
