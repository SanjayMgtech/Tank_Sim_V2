#include "Tank/TSTankControllerBase.h"
#include "Tank/TSTankControlComponent.h"

#include "ChaosVehicleMovementComponent.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Tank_Sim_V2.h"
#include "Engine/World.h"
#include "TimerManager.h"

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

	// Crew station views: seat -> capture component NAME. Names only, so this creates no components
	// and loads no assets - RULE 1 and RULE 2 both still hold. A tank that has not authored a
	// station simply fails the lookup and says so at BeginPlay.
	CrewViewCaptureComponents.Add(ETSCrewRole::Driver, TEXT("DriverSceneCaptureComponent"));
	CrewViewCaptureComponents.Add(ETSCrewRole::Gunner, TEXT("GunnerSceneCaptureComponent"));
	CrewViewCaptureComponents.Add(ETSCrewRole::Commander, TEXT("CommanderSceneCaptureComponent"));

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

void ATSTankControllerBase::RecalculateGunAndTurretRotation()
{
	// 1:1 port. Both halves share the same Select Float: A=1.0, B=-1.0, bPickA=Stabilization,
	// and SelectFloat returns A when bPickA is true.
	const double Sign = Stabilization ? 1.0 : -1.0;

	// --- Turrets (graph comment "Turrets") ---
	// MakeRotator(Roll, Pitch, Yaw) is FRotator(Pitch, Yaw, Roll) - only Yaw is fed here.
	FRotator RotCorrector = UKismetMathLibrary::MakeRotator(0.0, 0.0, GetActorRotation().Yaw * Sign);

	// ComposeRotators(A, B) is FRotator(FQuat(B) * FQuat(A)) - NOT A + B. Called through
	// Kismet so the quaternion order and normalisation match the graph exactly.
	for (FRotator& TurretRot : TurretsRotUnstabilized)
	{
		TurretRot = UKismetMathLibrary::ComposeRotators(TurretRot, RotCorrector);
	}

	// --- Guns (graph comment "Guns") ---
	// This half reads the "turret" socket's PITCH off the mesh, not the actor rotation.
	// Mesh null-checked like HullAccelerationDefinition: the Blueprint would log
	// "Accessed None" and continue with zero, so falling back to a zero rotator matches.
	const USkeletalMeshComponent* MeshComp = GetMesh();
	const double SocketPitch = MeshComp ? MeshComp->GetSocketRotation(TEXT("turret")).Pitch : 0.0;

	RotCorrector = UKismetMathLibrary::MakeRotator(0.0, SocketPitch * Sign, 0.0);

	for (FRotator& GunRot : GunsRotUnstabilized)
	{
		GunRot = UKismetMathLibrary::ComposeRotators(GunRot, RotCorrector);
	}
}

ETSCrewRole ATSTankControllerBase::GetLocalCrewRoleOnThisTank() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return ETSCrewRole::None;
	}

	// Only local controllers matter - we are asking "is the person in front of THIS screen crewing
	// THIS tank", and a listen server holds PlayerControllers for remote clients too. A dedicated
	// server has no local controllers at all, so it falls through to None.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController())
		{
			continue;
		}

		const ATSTankPlayerState* PS = PC->GetPlayerState<ATSTankPlayerState>();
		if (PS && PS->GetAssignedTank() == static_cast<const APawn*>(this))
		{
			return PS->GetCrewRole();
		}
	}

	return ETSCrewRole::None;
}

bool ATSTankControllerBase::IsLocalGunnerOfThisTank() const
{
	return GetLocalCrewRoleOnThisTank() == ETSCrewRole::Gunner;
}

bool ATSTankControllerBase::IsTurretSimulatedLocally() const
{
	// Authority always simulates. Beyond that there are two ways this machine can be the one
	// aiming, and BOTH must be honoured:
	//
	//   IsLocallyControlled()      - the LEGACY path, where a player possesses the tank directly
	//                                (the single-player demo map and the pre-framework multiplayer
	//                                setup). Dropping this would break the existing demo.
	//   IsLocalGunnerOfThisTank()  - the FRAMEWORK path, where nobody possesses the tank and the
	//                                Gunner is identified by crew role instead.
	//
	// Anyone else consumes the replicated TurretsRot/GunsRot.
	return HasAuthority() || IsLocallyControlled() || IsLocalGunnerOfThisTank();
}

bool ATSTankControllerBase::ShouldSendAimToServer() const
{
	// Deliberately NOT extended with IsLocalGunnerOfThisTank(). This gates the tank's OWN
	// ServerSetAimPoint RPC, and a Server RPC is silently dropped unless the calling client owns
	// the actor. Under the crew model the Gunner owns their VR pawn, not the tank, so adding the
	// Gunner here would fire an RPC that never arrives and leave no trace at the call site.
	//
	// The framework path sends aim through ATSTankPlayerController::ServerAimTurret instead,
	// which the client does own. This predicate therefore covers only the legacy possessed-tank
	// case and stays exactly as it was when it was verified in the two-window listen-server test.
	return !HasAuthority() && IsLocallyControlled();
}

void ATSTankControllerBase::ServerSetAimPoint_Implementation(FVector NewAimPoint)
{
	// Runs on the server. TurretsAndGunsRotCalculation uses this instead of its own
	// camera trace for pawns this machine does not locally control.
	ReceivedAimPoint = NewAimPoint;
}

// =========================================================================
// ITSTankInterface defaults.
//
// Every one of these is meant to be overridden in BP_TankController_Chaos.
// The warning is the point: without it, a missing override presents as "the
// tank ignores the framework" with nothing in the log, which is exactly the
// class of silent failure that cost attempt 1 so much time.
//
// Logged as Warning (not Error) because an unoverridden event is a wiring gap
// during bring-up, not a crash, and Error would fail automated smoke runs that
// legitimately exercise a partially-wired tank.
// =========================================================================
namespace
{
	void WarnNotOverridden(const AActor* Tank, const TCHAR* EventName)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[TSTankInterface] %s::%s has no Blueprint override - the framework authorised this ")
			TEXT("action but the tank did nothing. Override the event in the tank Blueprint."),
			Tank ? *Tank->GetName() : TEXT("<null>"), EventName);
	}
}

void ATSTankControllerBase::BP_SetDriveInput_Implementation(float Throttle, float Steering)
{
	// Intentionally does NOT touch the movement component. The Blueprint override must
	// route this into ThrottleControl / TurningControl, which carry the gearing, speed
	// limiting, MoveRightAxis state and PoliceTurn behaviour. Calling
	// SetThrottleInput/SetSteeringInput here would drive the tank in a smoke test while
	// silently discarding all of that.
	WarnNotOverridden(this, TEXT("BP_SetDriveInput"));
}

void ATSTankControllerBase::BeginPlay()
{
	Super::BeginPlay();

	// Aim straight ahead until a Gunner actually aims.
	//
	// TurretsAndGunsRotCalculation selects ReceivedAimPoint whenever the tank is not locally
	// controlled - which, under the crew model, is ALWAYS, because nobody possesses the tank. A
	// zero default is not "unset" to that maths, it is the world origin, so a tank spawned away
	// from the origin swung its turret round to point back at it the moment it appeared.
	ReceivedAimPoint = GetActorLocation() + GetActorForwardVector() * DefaultAimDistance;

	ApplyVehicleSimulationAuthorityPolicy();
	AttachTurretCrewSeats();
	SyncInteriorMeshTickToPawn();
	InitialiseCrewViewCaptures();
}

void ATSTankControllerBase::Tick(float InDeltaSeconds)
{
	// Super FIRST, deliberately. AActor::Tick is what dispatches the Blueprint's Event Tick, and
	// that is where TurretsAndGunsRotCalculation writes this frame's turret angle. Capturing before
	// it would put a sight riding the turret one frame behind the barrel it is bolted to.
	Super::Tick(InDeltaSeconds);

	// Levers before the capture, for the same reason the capture goes after Super: whatever a crew
	// view renders should show this frame's state, not last frame's.
	UpdateInteriorControlState(InDeltaSeconds);

	UpdateCrewViewCapture(InDeltaSeconds);
}

void ATSTankControllerBase::InitialiseCrewViewCaptures()
{
	TArray<USceneCaptureComponent2D*> Captures;
	GetComponents<USceneCaptureComponent2D>(Captures);

	// Silence every one of them, on every machine, before resolving anything. The Blueprint ships
	// them with bCaptureEveryFrame and bCaptureOnMovement on, which is what made a tank nobody was
	// looking out of still render the world twice a frame. From here a view renders only because
	// UpdateCrewViewCapture asked it to.
	for (USceneCaptureComponent2D* Capture : Captures)
	{
		if (!Capture)
		{
			continue;
		}

		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->SetComponentTickEnabled(false);
	}

	// Resolve the per-seat names once. C++ only looks these up; where the camera sits, what render
	// target it draws into and how the screen displays it are all Blueprint data.
	for (const TPair<ETSCrewRole, FName>& Entry : CrewViewCaptureComponents)
	{
		if (Entry.Key == ETSCrewRole::None || Entry.Value.IsNone())
		{
			continue;
		}

		USceneCaptureComponent2D** Found = Captures.FindByPredicate(
			[&Entry](const USceneCaptureComponent2D* Capture) { return Capture && Capture->GetFName() == Entry.Value; });

		if (Found && *Found)
		{
			ResolvedCrewViewCaptures.Add(Entry.Key, *Found);

			// Snapshot BEFORE anything touches it, so Normal can restore exactly what the Blueprint
			// authored rather than an engine default.
			AuthoredCrewViewPostProcess.Add(Entry.Key, (*Found)->PostProcessSettings);
			continue;
		}

		// Loud, not silent. A missing capture presents as "the periscope is black", which is easy to
		// mistake for a broken material or an unassigned render target.
		UE_LOG(LogTankSim, Warning,
			TEXT("[Tank] %s: no SceneCaptureComponent2D named '%s' for crew role %d - that station's ")
			TEXT("view will never render. Add the component to the tank Blueprint, or clear the row ")
			TEXT("in CrewViewCaptureComponents."),
			*GetName(), *Entry.Value.ToString(), static_cast<int32>(Entry.Key));
	}
}

void ATSTankControllerBase::ConfigureCrewViewCapture(USceneCaptureComponent2D* Capture) const
{
	if (!Capture || !bApplyCrewViewPerformanceDefaults)
	{
		return;
	}

	// Kept ON because the capture is intermittent: without persisted state the renderer throws away
	// the temporal history between captures, so anti-aliasing never converges and the image crawls.
	// Only one capture is ever live, so the history buffer it costs is paid once.
	Capture->bAlwaysPersistRenderingState = true;

	Capture->MaxViewDistanceOverride = CrewViewMaxDrawDistance;

	// Effects a periscope does not need, in rough order of what they cost.
	Capture->ShowFlags.SetMotionBlur(false);
	Capture->ShowFlags.SetScreenSpaceReflections(false);
	Capture->ShowFlags.SetAmbientOcclusion(false);
	Capture->ShowFlags.SetBloom(false);
	Capture->ShowFlags.SetLensFlares(false);

	// See the header: this one is as much about the image pumping as about frame time.
	Capture->ShowFlags.SetEyeAdaptation(false);
}

void ATSTankControllerBase::SetCrewViewVisionMode(ETSVisionMode NewMode)
{
	if (CrewViewVisionMode == NewMode)
	{
		return;
	}

	CrewViewVisionMode = NewMode;
	ApplyVisionModeToActiveCapture();
}

ETSVisionMode ATSTankControllerBase::CycleCrewViewVisionMode()
{
	switch (CrewViewVisionMode)
	{
	case ETSVisionMode::Normal:      SetCrewViewVisionMode(ETSVisionMode::NightVision); break;
	case ETSVisionMode::NightVision: SetCrewViewVisionMode(ETSVisionMode::Thermal);     break;
	default:                         SetCrewViewVisionMode(ETSVisionMode::Normal);      break;
	}

	return CrewViewVisionMode;
}

UTextureRenderTarget2D* ATSTankControllerBase::GetCrewViewRenderTarget(ETSCrewRole InCrewRole) const
{
	const TObjectPtr<USceneCaptureComponent2D>* Found = ResolvedCrewViewCaptures.Find(InCrewRole);
	return (Found && *Found) ? (*Found)->TextureTarget : nullptr;
}

void ATSTankControllerBase::ApplyVisionModeToActiveCapture()
{
	if (!ActiveCrewViewCapture)
	{
		return;
	}

	// Always start from what the designer authored. Layering one mode's grading on top of the
	// previous one's is how a filter chain ends up permanently green.
	if (const FPostProcessSettings* Authored = AuthoredCrewViewPostProcess.Find(ActiveCrewViewRole))
	{
		ActiveCrewViewCapture->PostProcessSettings = *Authored;
	}
	ActiveCrewViewCapture->PostProcessBlendWeight = 1.f;

	// Bloom is off by default for cost (ConfigureCrewViewCapture), but an image intensifier without
	// bloom does not read as one - the blooming of bright sources IS the look.
	ActiveCrewViewCapture->ShowFlags.SetBloom(CrewViewVisionMode == ETSVisionMode::NightVision);

	if (CrewViewVisionMode == ETSVisionMode::Normal)
	{
		return;
	}

	// An authored material wins: colour grading cannot express a thermal ramp (inverted luminance
	// through a gradient), so anyone who needs a real one supplies a post-process material.
	if (TObjectPtr<UMaterialInterface>* ModeMaterial = VisionModeMaterials.Find(CrewViewVisionMode))
	{
		if (*ModeMaterial)
		{
			ActiveCrewViewCapture->PostProcessSettings.AddBlendable(*ModeMaterial, 1.f);
			return;
		}
	}

	FPostProcessSettings& PP = ActiveCrewViewCapture->PostProcessSettings;

	// Both built-in looks are monochrome first. W is the luminance/overall channel of the colour
	// grading vectors and must stay at 1, or the whole image scales with it.
	PP.bOverride_ColorSaturation = true;
	PP.ColorSaturation = FVector4(0.f, 0.f, 0.f, 1.f);

	if (CrewViewVisionMode == ETSVisionMode::NightVision)
	{
		PP.bOverride_ColorGain = true;
		PP.ColorGain = FVector4(
			NightVisionTint.R * NightVisionGain,
			NightVisionTint.G * NightVisionGain,
			NightVisionTint.B * NightVisionGain,
			1.f);

		// Lift the shadows: an intensifier's whole job is that you can see into them.
		PP.bOverride_ColorGamma = true;
		PP.ColorGamma = FVector4(0.65f, 0.65f, 0.65f, 1.f);

		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = 1.5f;

		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = 1.2f;

		// Grain, so the image reads as amplified rather than merely tinted.
		PP.bOverride_FilmGrainIntensity = true;
		PP.FilmGrainIntensity = 0.6f;
	}
	else // Thermal - white hot.
	{
		PP.bOverride_ColorContrast = true;
		PP.ColorContrast = FVector4(ThermalContrast, ThermalContrast, ThermalContrast, 1.f);

		PP.bOverride_ColorGain = true;
		PP.ColorGain = FVector4(1.4f, 1.4f, 1.4f, 1.f);

		PP.bOverride_ColorOffset = true;
		PP.ColorOffset = FVector4(-0.05f, -0.05f, -0.05f, 0.f);

		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = 0.4f;
	}
}

void ATSTankControllerBase::UpdateCrewViewCapture(float InDeltaSeconds)
{
	// A dedicated server draws nothing for anybody. Checked explicitly rather than left to fall out
	// of "no local crew role", so the per-tank work really is zero there.
	if (GetNetMode() == NM_DedicatedServer || ResolvedCrewViewCaptures.Num() == 0)
	{
		return;
	}

	// Re-check which seat the local player holds only occasionally. Crew assignments change on the
	// order of seconds; walking the PlayerController list every frame for every tank in the level
	// would cost more than the answer is worth.
	CrewViewRoleRefreshAccumulator += InDeltaSeconds;
	if (CrewViewRoleRefreshAccumulator >= CrewViewRoleRefreshSeconds)
	{
		CrewViewRoleRefreshAccumulator = 0.f;

		const ETSCrewRole LocalRole = GetLocalCrewRoleOnThisTank();
		if (LocalRole != ActiveCrewViewRole)
		{
			ActiveCrewViewRole = LocalRole;

			// The outgoing station keeps whatever it last drew. Clearing it would flash the screen
			// black on every seat change for no gain - the player is not looking at it any more.
			TObjectPtr<USceneCaptureComponent2D>* Found = ResolvedCrewViewCaptures.Find(LocalRole);
			ActiveCrewViewCapture = Found ? *Found : nullptr;

			if (ActiveCrewViewCapture)
			{
				ConfigureCrewViewCapture(ActiveCrewViewCapture);
				ApplyVisionModeToActiveCapture();

				// Draw one immediately rather than waiting out the interval, so sitting down does
				// not start with a stale or empty screen.
				CrewViewCaptureAccumulator = 0.f;
				ActiveCrewViewCapture->CaptureScene();
			}
		}
	}

	if (!ActiveCrewViewCapture)
	{
		return;
	}

	// 0 Hz means every frame - the lowest latency and the highest cost.
	if (CrewViewCaptureHz <= 0.f)
	{
		ActiveCrewViewCapture->CaptureScene();
		return;
	}

	CrewViewCaptureAccumulator += InDeltaSeconds;

	const float CaptureInterval = 1.f / CrewViewCaptureHz;
	if (CrewViewCaptureAccumulator >= CaptureInterval)
	{
		// Subtract rather than zero, so the capture rate does not drift below the requested one on
		// frames that overshoot the interval.
		CrewViewCaptureAccumulator = FMath::Fmod(CrewViewCaptureAccumulator, CaptureInterval);
		ActiveCrewViewCapture->CaptureScene();
	}
}

void ATSTankControllerBase::SyncInteriorMeshTickToPawn()
{
	const USkeletalMeshComponent* RootMesh = GetMesh();

	TArray<USkeletalMeshComponent*> Meshes;
	GetComponents<USkeletalMeshComponent>(Meshes);

	// NOT named 'Mesh': AWheeledVehiclePawn declares a member of that name, and UHT builds with
	// -WarningsAsErrors so C4458 shadowing is fatal. Same trap as 'Role' (see CLAUDE.md).
	for (USkeletalMeshComponent* MeshComp : Meshes)
	{
		if (!MeshComp || MeshComp == RootMesh)
		{
			continue;
		}

		// The anim evaluation happens in the component's tick, so making that tick depend on the
		// pawn guarantees it sees THIS frame's TurretsRot rather than last frame's.
		MeshComp->AddTickPrerequisiteActor(this);

		UE_LOG(LogTankSim, Log, TEXT("[Tank] %s: '%s' now ticks after the pawn (removes the interior anim frame lag)."),
			*GetName(), *MeshComp->GetName());
	}
}

void ATSTankControllerBase::AttachTurretCrewSeats()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp || TurretSocketName.IsNone())
	{
		return;
	}

	if (!MeshComp->DoesSocketExist(TurretSocketName))
	{
		UE_LOG(LogTankSim, Warning,
			TEXT("[Tank] %s: no socket/bone '%s' on %s - the Gunner and Commander seats will stay ")
			TEXT("bolted to the hull and will not traverse with the turret. Set TurretSocketName to ")
			TEXT("this mesh's turret bone."),
			*GetName(), *TurretSocketName.ToString(), *MeshComp->GetName());
		return;
	}

	TArray<USceneComponent*> SceneComponents;
	GetComponents<USceneComponent>(SceneComponents);

	for (const FName& SeatName : TurretMountedSeatComponents)
	{
		USceneComponent** Found = SceneComponents.FindByPredicate(
			[&SeatName](const USceneComponent* Component) { return Component && Component->GetFName() == SeatName; });

		if (!Found || !*Found)
		{
			UE_LOG(LogTankSim, Warning, TEXT("[Tank] %s: turret seat '%s' not found."), *GetName(), *SeatName.ToString());
			continue;
		}

		// KeepWorldTransform: the seat stays exactly where it was placed, it just rides the turret now.
		(*Found)->AttachToComponent(MeshComp, FAttachmentTransformRules::KeepWorldTransform, TurretSocketName);

		UE_LOG(LogTankSim, Log, TEXT("[Tank] %s: seat '%s' now rides socket '%s'."),
			*GetName(), *SeatName.ToString(), *TurretSocketName.ToString());
	}
}

void ATSTankControllerBase::ApplyVehicleSimulationAuthorityPolicy()
{
	if (!bSimulateVehicleOnAuthorityOnly)
	{
		return;
	}

	UChaosVehicleMovementComponent* Move = Cast<UChaosVehicleMovementComponent>(GetVehicleMovementComponent());
	if (!Move)
	{
		return;
	}

	// True on a client makes Chaos require a controller; nothing possesses the tank, so bProcessLocally
	// is false there and the client stops running its own sim. The server keeps False and remains the
	// single simulator. See the header for why this cannot just be one value in the Blueprint.
	const bool bRequiresController = !HasAuthority();
	Move->SetRequiresControllerForInputs(bRequiresController);

	UE_LOG(LogTankSim, Log, TEXT("[Tank] %s: vehicle sim %s (bRequiresControllerForInputs=%s)"),
		*GetName(),
		HasAuthority() ? TEXT("ENABLED (authority)") : TEXT("disabled (client - motion comes from replication)"),
		bRequiresController ? TEXT("true") : TEXT("false"));
}

void ATSTankControllerBase::BP_AimTurret_Implementation(FVector_NetQuantize AimPoint)
{
	// Ignore the zero default. UTSTankWeaponComponent::CurrentAimPoint starts at zero and its
	// OnRep fires on first replication, which would otherwise hand the turret the world origin as
	// a target before any Gunner has aimed.
	if (FVector(AimPoint).IsNearlyZero())
	{
		return;
	}

	// Now that the contract carries a world-space POINT, this is implementable in C++ and needs
	// no Blueprint override: ReceivedAimPoint is exactly what TurretsAndGunsRotCalculation already
	// consumes (it selects between its own camera trace and this value, then writes TargetPoint).
	//
	// So the framework's aim request lands in the same variable the verified multiplayer aim fix
	// uses, and the turret maths downstream is untouched.
	ReceivedAimPoint = AimPoint;
}

void ATSTankControllerBase::BP_FireMainCannon_Implementation()
{
	// One trigger pull. The weapon is a hold-to-fire pair (StartShooting/StopShooting) but
	// ServerFireMainCannon is a single discrete request, so hold the trigger briefly and release it.
	// Without the release the gun would stay held down after one press.
	UE_LOG(LogTankSim, Log, TEXT("[Tank] %s: MAIN CANNON - trigger held %.2fs"), *GetName(), MainCannonTriggerHoldSeconds);
	BP_WeaponStartShooting();
	bWeaponFiring = true;

	GetWorldTimerManager().SetTimer(WeaponStopTimerHandle, this,
		&ATSTankControllerBase::ReleaseWeaponTrigger, MainCannonTriggerHoldSeconds, false);
}

void ATSTankControllerBase::BP_FireMachineGun_Implementation()
{
	// Hold-to-fire. ServerFireMachineGun arrives once per frame while the key is held, so start on
	// the first request and keep pushing the release out; the gun stops once requests stop arriving.
	// Re-sending StartShooting every frame would restart the weapon's own firing cycle.
	if (!bWeaponFiring)
	{
		UE_LOG(LogTankSim, Log, TEXT("[Tank] %s: MACHINE GUN - opening fire"), *GetName());
		bWeaponFiring = true;
		BP_WeaponStartShooting();
	}

	GetWorldTimerManager().SetTimer(WeaponStopTimerHandle, this,
		&ATSTankControllerBase::ReleaseWeaponTrigger, MachineGunReleaseDelaySeconds, false);
}

FRotator ATSTankControllerBase::GetInteriorTurretRotation() const
{
	// TurretsRot is the same array the exterior AnimBP reads, so the interior cannot drift out of
	// step with the gun. Index 0 is the main turret; the array is pre-sized to 10 in the constructor,
	// but guard anyway - this runs every frame from an AnimBP, including before BeginPlay.
	if (TurretsRot.Num() == 0)
	{
		return FRotator::ZeroRotator;
	}

	const double Yaw = TurretsRot[0].Yaw * (bInvertInteriorTurretYaw ? -1.0 : 1.0) + InteriorTurretYawOffset;

	// Yaw only. Pitch would tilt the whole crew compartment with the gun.
	return FRotator(0.0, Yaw, 0.0);
}

void ATSTankControllerBase::UpdateInteriorControlState(float DeltaTime)
{
	// CurrentDriveInput is REPLICATED, so this runs correctly on every machine - the server, the
	// driver's client, and a passenger watching from another seat all smooth the same source and
	// therefore see the same lever positions.
	FVector2D Input = FVector2D::ZeroVector;
	const UTSTankControlComponent* Control = FindComponentByClass<UTSTankControlComponent>();
	if (Control)
	{
		Input = Control->GetCurrentDriveInput();
	}

	// Rate-limited diagnostic: this is the one place that can silently leave every interior control
	// at its rest pose, and a missing component looks identical to zero input from outside.
	if (bLogInteriorControlState)
	{
		// A MEMBER, not a static local. A static is shared by every tank AND survives PIE teardown,
		// so world time - which restarts at 0 each session - never exceeds the value left over from
		// the last run and the log goes silent for the rest of the editor's life. That is exactly
		// what happened here, and it made a working diagnostic look like dead code.
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (Now - LastInteriorLogTime > 1.0 || Now < LastInteriorLogTime)
		{
			LastInteriorLogTime = Now;
			UE_LOG(LogTankSim, Log,
				TEXT("[InteriorCtl] %s ctrl=%s input=(%.2f, %.2f) -> thr=%.3f brk=%.3f steer=%.3f"),
				*GetName(), Control ? TEXT("YES") : TEXT("*** NULL ***"),
				Input.X, Input.Y, DisplayThrottle, DisplayBrake, DisplaySteering);
		}
	}

	// X is throttle, Y is steering - the same convention Input_Drive and BP_SetDriveInput use.
	const float TargetThrottle = FMath::Max(Input.X, 0.f);
	const float TargetBrake = FMath::Max(-Input.X, 0.f);
	const float TargetSteering = FMath::Clamp(Input.Y, -1.f, 1.f);

	if (InteriorControlInterpSpeed <= 0.f)
	{
		DisplayThrottle = TargetThrottle;
		DisplayBrake = TargetBrake;
		DisplaySteering = TargetSteering;
		return;
	}

	DisplayThrottle = FMath::FInterpTo(DisplayThrottle, TargetThrottle, DeltaTime, InteriorControlInterpSpeed);
	DisplayBrake = FMath::FInterpTo(DisplayBrake, TargetBrake, DeltaTime, InteriorControlInterpSpeed);
	DisplaySteering = FMath::FInterpTo(DisplaySteering, TargetSteering, DeltaTime, InteriorControlInterpSpeed);
}

namespace
{
	// SLERP, not a per-component Lerp. These are measured bone rotations and can carry large values
	// on all three axes (the VK1602's gas pedal rests near roll -104, pitch -83, yaw 76); lerping
	// Euler components independently takes the long way round on at least one axis and the control
	// visibly swings through the geometry. Quaternions take the short arc by construction.
	FRotator SlerpPose(const FRotator& Rest, const FRotator& Worked, float Alpha)
	{
		return FQuat::Slerp(Rest.Quaternion(), Worked.Quaternion(), FMath::Clamp(Alpha, 0.f, 1.f)).Rotator();
	}
}

FRotator ATSTankControllerBase::GetInteriorGasPedalRotation() const
{
	return SlerpPose(GasPedalRestRotation, GasPedalPressedRotation, DisplayThrottle);
}

FRotator ATSTankControllerBase::GetInteriorBrakePedalRotation() const
{
	return SlerpPose(BrakePedalRestRotation, BrakePedalPressedRotation, DisplayBrake);
}

FRotator ATSTankControllerBase::GetInteriorLeverRotation(bool bLeft) const
{
	return bLeft
		? SlerpPose(LeftLeverRestRotation, LeftLeverPulledRotation, GetInteriorLeftLeverAlpha())
		: SlerpPose(RightLeverRestRotation, RightLeverPulledRotation, GetInteriorRightLeverAlpha());
}

FVector ATSTankControllerBase::GetInteriorGasPedalLocation() const
{
	return FMath::Lerp(GasPedalRestLocation, GasPedalPressedLocation, FMath::Clamp(DisplayThrottle, 0.f, 1.f));
}

FVector ATSTankControllerBase::GetInteriorBrakePedalLocation() const
{
	return FMath::Lerp(BrakePedalRestLocation, BrakePedalPressedLocation, FMath::Clamp(DisplayBrake, 0.f, 1.f));
}

FVector ATSTankControllerBase::GetInteriorLeverLocation(bool bLeft) const
{
	return bLeft
		? FMath::Lerp(LeftLeverRestLocation, LeftLeverPulledLocation, GetInteriorLeftLeverAlpha())
		: FMath::Lerp(RightLeverRestLocation, RightLeverPulledLocation, GetInteriorRightLeverAlpha());
}

bool ATSTankControllerBase::GetLeverGrabLocation(bool bLeft, FVector& OutLocation) const
{
	const FName GrabSocket = bLeft ? LeftLeverGrabSocket : RightLeverGrabSocket;
	if (GrabSocket.IsNone())
	{
		return false;
	}

	// Searched rather than naming the interior component: the lever lives on whichever skeletal mesh
	// the rigger put it on, and GetSocketLocation accepts a bone name as readily as a socket.
	TArray<USkeletalMeshComponent*> Meshes;
	GetComponents<USkeletalMeshComponent>(Meshes);
	for (const USkeletalMeshComponent* MeshComp : Meshes)
	{
		if (MeshComp && MeshComp->DoesSocketExist(GrabSocket))
		{
			OutLocation = MeshComp->GetSocketLocation(GrabSocket);
			return true;
		}
	}
	return false;
}

FRotator ATSTankControllerBase::GetMainGunAimRotation() const
{
	// Index 0 is the main turret / main gun. Both arrays are pre-sized to 10 in the constructor, but
	// guard anyway - this is read from another actor's tick, which can run before ours ever has.
	const double Yaw = TurretsRot.Num() > 0 ? TurretsRot[0].Yaw : 0.0;
	const double Pitch = GunsRot.Num() > 0 ? GunsRot[0].Pitch : 0.0;

	// No roll: the gun elevates and the turret traverses, neither of them banks.
	return FRotator(Pitch, Yaw, 0.0);
}

FVector ATSTankControllerBase::GetTurretPivotLocation() const
{
	const USkeletalMeshComponent* MeshComp = GetMesh();
	if (MeshComp && !TurretSocketName.IsNone() && MeshComp->DoesSocketExist(TurretSocketName))
	{
		return MeshComp->GetSocketLocation(TurretSocketName);
	}

	return GetActorLocation();
}

void ATSTankControllerBase::ReleaseWeaponTrigger()
{
	if (!bWeaponFiring)
	{
		return;
	}

	UE_LOG(LogTankSim, Log, TEXT("[Tank] %s: weapon trigger released"), *GetName());
	bWeaponFiring = false;
	BP_WeaponStopShooting();
}

void ATSTankControllerBase::BP_UpdateCommanderIntel_Implementation(const FTSCommanderIntel& Intel)
{
	// Commander UI is a Blueprint widget type (W_MainHUD_C); C++ cannot reach it.
	WarnNotOverridden(this, TEXT("BP_UpdateCommanderIntel"));
}
