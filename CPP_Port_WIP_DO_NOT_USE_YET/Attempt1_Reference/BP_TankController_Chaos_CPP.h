// Native 1:1 port of Content/YI_TankCollection/Blueprint/Master/Controller/BP_TankController_Chaos.
// The vendor Blueprint calls itself a "Controller" but is actually the master Chaos vehicle Pawn
// that every per-tank Blueprint (Leopard2A7, M1A2, Merkava, Proxy, T90, VK1602Leopard) inherits from.
// BP_TankController_Chaos itself becomes legacy once the per-tank BPs are reparented to this class.
#pragma once

#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"
#include "Tank/TSTankControllerChaosTypes.h"
#include "InputActionValue.h"
#include "BP_TankController_Chaos_CPP.generated.h"

class UChaosWheeledVehicleMovementComponent;
class USpotLightComponent;
class UPointLightComponent;
class UParticleSystemComponent;
class USpringArmComponent;
class UCameraComponent;
class UAudioComponent;
class UStaticMeshComponent;
class UDecalComponent;
class USplineComponent;
class UInstancedStaticMeshComponent;
class UActorComponent;
class UUserWidget;
class UCurveFloat;
class UMaterialInterface;
class UMaterialInstance;
class UMaterialInstanceDynamic;
class UInputMappingContext;
class UInputAction;
class UTimelineComponent;
class UCameraShakeBase;
class UParticleSystem;
class USoundBase;
struct FInputActionValue;

UCLASS()
class ABP_TankController_Chaos_CPP : public AWheeledVehiclePawn
{
	GENERATED_BODY()

public:
	ABP_TankController_Chaos_CPP();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaTime) override;
	virtual void BeginPlay() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// ------------------------------------------------------------------
	// Components (SCS in the source BP)
	// ------------------------------------------------------------------
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<USpotLightComponent> Light_R;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<USpotLightComponent> Light_L;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<UParticleSystemComponent> P_Exhaust;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<UParticleSystemComponent> P_Exhaust1;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<UParticleSystemComponent> P_Exhaust2;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<UParticleSystemComponent> SlideBackRight;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<UParticleSystemComponent> SlideBackLeft;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<USpringArmComponent> SpringArmArcade;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<USpringArmComponent> SpringArmSniper;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<UPointLightComponent> Brake_L;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<UPointLightComponent> Brake_R;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<UAudioComponent> TurretMotor;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<UAudioComponent> TankEngine;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<UAudioComponent> Fire;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<UStaticMeshComponent> Tank_Destroyed;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<UDecalComponent> Decal;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<UParticleSystemComponent> DestroyedFlames;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<USplineComponent> TrackPath_R;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<UCameraComponent> DebugCamera;

	// BP_TankWeapon_C stays a Blueprint-only ActorComponent (out of scope for this pass); the class to
	// spawn is picked per-tank via this EditDefaultsOnly slot and instantiated at runtime.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tank|Weapon")
	TSubclassOf<UActorComponent> TankWeaponComponentClass;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Weapon")
	TObjectPtr<UActorComponent> BPC_TankWeapon;

	// Runtime-created (not SCS): TrackPath_L mirrors TrackPath_R (SplineMirrorCopy); the instanced
	// track meshes are built by InstanceTracksCreation.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<USplineComponent> TrackPath_L;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> TracksInstances_R;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> TracksInstances_L;

	// Cached typed pointer to the native VehicleMovementComponent (ChaosWheeledVehicleMovementComponent).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Components")
	TObjectPtr<UChaosWheeledVehicleMovementComponent> VehicleMovement;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	TObjectPtr<UMaterialInterface> BaseTrackMaterial;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	TObjectPtr<UMaterialInstanceDynamic> LeftTrackMID;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	TObjectPtr<UMaterialInstanceDynamic> RightTrackMID;

	// ------------------------------------------------------------------
	// Widgets / UI
	// ------------------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Widgets")
	TSubclassOf<UUserWidget> HUDClass;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Widgets")
	TSubclassOf<UUserWidget> CrosshairClass;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Widgets")
	TObjectPtr<UUserWidget> HUD;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Widgets")
	TObjectPtr<UUserWidget> Crosshair;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Widgets")
	TObjectPtr<APlayerController> PlayerController;

	// ------------------------------------------------------------------
	// Input
	// ------------------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_LookUpDown;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_LookRightLeft;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_MoveRightLeft;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_MoveForwardBack;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_Handbrake;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_DebugView;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_DestroySelf;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_ToggleInterface;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_BulletTime;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_UseWeapon1;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_UseWeapon2;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_UseWeapon3;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_UseWeapon4;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_UseWeapon5;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_FirePrimaryWeapon;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_Reload;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_Aim;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_SwitchCamo;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_ToggleLights;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_TurretBlocking;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_Stabilizer;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_Healing;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_SniperModeToggle;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_Zoom;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_ShowHotkeys;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Input")
	TObjectPtr<UInputAction> IA_GeoTracksSwitching;

	// ------------------------------------------------------------------
	// FX / audio / camera-shake defaults (EventGraph asset references)
	// ------------------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, Category = "Tank|FX")
	TSubclassOf<UCameraShakeBase> CameraShakeClass;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|FX")
	TObjectPtr<UParticleSystem> ExplosionEffect;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Audio")
	TObjectPtr<USoundBase> ExplosionSound;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Audio")
	TObjectPtr<USoundBase> ExplosionSound2;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|FX")
	TObjectPtr<UParticleSystem> DestroyedFlamesTemplate;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Timeline")
	TObjectPtr<UTimelineComponent> MainGunSpringTimeline;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank|Timeline")
	TObjectPtr<UTimelineComponent> AimTimeline;
	UPROPERTY(EditDefaultsOnly, Category = "Tank|Curves")
	TObjectPtr<UCurveFloat> AimCurve;

	// ------------------------------------------------------------------
	// Variables (member-for-member port of the BP's 147 variables)
	// ------------------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "Tank|State")
	bool Destroyed = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|State")
	bool IsVehicleTaken = true;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|State")
	bool Stabilization = false;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|State")
	bool StabilizationWasSwitched = false;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Camo")
	int32 CamoCurrent = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Camo")
	FString CamoName;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Weapon")
	float MainGunSpringOnFire = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|State")
	double Health = 100.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Default")
	double HealthMax = 100.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|State")
	double CurentRPMRatio = 0.0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Tank|Turret")
	FRotator Rep_ControlRotation = FRotator::ZeroRotator;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Turret")
	bool IsTurretRotating = false;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|State")
	double MoveRightAxis = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Camo")
	TArray<FTSCamoOptions> CamoVariations;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Default")
	double LightIntensityLights = 25.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Default")
	double EmissiveIntensityLights = 5.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|State")
	double DeltaSeconds = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Default")
	bool InvertTrackDirection = false;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double TrackSpeedModifier = 1.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	bool ChassisLockedR = false;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	bool ChassisLockedL = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	bool ReverseTurnInReverse = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	double MaxSpeedKMH = 60.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	double MaxTurningSpeed = 45.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	TObjectPtr<UCurveFloat> SteeringCurve;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	int32 TracksAmount = 50;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	int32 PhysWheelsAmount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	int32 UniqueTrackMeshesAmount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double HullZRot = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double HullDeltaZRot = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double ChassisDistanceZRotComponentR = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double ChassisDistanceZRotComponentL = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double ChassisDistanceXMoveComponentR = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double ChassisDistanceXMoveComponentL = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double ChassisDistanceR = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double ChassisDistanceL = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double ChassisDeltaDistanceR = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double ChassisDeltaDistanceL = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	double TilingSegmentLength = 70.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	double TrackThickness = 4.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	TArray<FTSTankSplineAnim> TankSplineAnim;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	TArray<TObjectPtr<UStaticMesh>> TrackStaticMeshes;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double ChassisAccelerationR = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double ChassisAccelerationL = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double SaggingDegreeR = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double SaggingDegreeL = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	double WheelRadiusFront = 22.5;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	double WheelRadiusMiddle = 38.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	double WheelRadiusRear = 22.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	double WheelRadiusAccessory = 11.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	TArray<double> VibrationOffset_R;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	TArray<double> VibrationOffset_L;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis|Assymmetrical")
	double MiddleWheelXOffset = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double HullDeltaXLocation = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	TArray<FVector> SplinePointLocation;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	TArray<FVector> SplinePointPerpendicularVectors;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	TArray<int32> CopyPointIndices;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Antenna")
	FVector HullSpeedWorld = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Antenna")
	FVector HullAccelerationWorldInverted = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Antenna")
	FVector TurretSpeedLocalInverted = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Antenna")
	TArray<FVector> AntennaCurrentSpeed;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Antenna")
	TArray<FTSAntennaParams> AntennaParameters;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Antenna")
	TArray<FRotator> AntennaRotation;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Zoom")
	bool SniperMode = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Zoom")
	double SniperCameraMaxZoom = 4.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Zoom")
	double CameraZoomStep = 1000.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Zoom")
	float StartTargetArmLengthArcade = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Zoom")
	float TargetArmLengthArcade_2 = -60.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Zoom")
	double TargetArmLengthMax = 2000.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Zoom")
	double TargetArmLengthMin = 1000.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Default")
	bool EditorSplinePreview = true;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Zoom")
	float StartLagSpeed = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Zoom")
	FVector StartSocketOffsetArcade = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Zoom")
	FVector SocketOffsetSniper = FVector(60.0, -50.0, 35.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Zoom")
	double SniperLagSpeed = 30.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	bool UseGeometricTracks = true;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Turret")
	FRotator TurretRotation = FRotator::ZeroRotator;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Turret")
	double TurretYaw = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Turret")
	double TurretPitch = 0.0;
	// Legacy ("Old" category) turret/MG tuning values - only consumed by the _Old graphs.
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Old")
	double TurretRotationSpeed = 40.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Old")
	double ClippingRangeMin = -110.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Old")
	double ClippingRangeMax = 110.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Old")
	FVector2D TurretVerticalRange = FVector2D(-10.0, 20.0);
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Old")
	double TurretHeightRangeClip = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Old")
	double TurretRotationSpeedVertical = 5.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Old")
	FVector2D MGVerticalRange = FVector2D(-10.0, 30.0);
	UPROPERTY(BlueprintReadOnly, Category = "Tank|MachineGun")
	FRotator MGRotation = FRotator::ZeroRotator;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|MachineGun")
	double MGPitch = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|MachineGun")
	double MGYaw = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Old")
	double MGRotationSpeed = 100.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double WheelRotMiddleL = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double WheelRotMiddleR = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double WheelRotFrontL = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double WheelRotFrontR = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double WheelRotRearL = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double WheelRotRearR = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double WheelRotAccessoryL = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double WheelRotAccessoryR = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	TArray<double> WheelsZOffsets;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	double WheelStartingAngleGeoR = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Antenna")
	double GravityForce = -70.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	double WheelStartingAngleGeoL = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	double WheelStartingAngleUVR = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	double WheelStartingAngleUVL = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	double WheelSpeedCorrectionUV = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis|Vibration")
	double SpeedInfluence = 0.3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis|Vibration")
	double MaxSpeedInfluence = 0.6;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	double SaggingMaxDistance = 20.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis")
	double ProportionalCoefficient = 5.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis|Vibration")
	double AccelerationInfluence = 0.2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis|Vibration")
	double MaxAccelerationInfluence = 1.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis|Vibration")
	double TrackFrequency = 1500.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	float ForwardSpeedMPH = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double FilletsCompensation = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis|Assymmetrical")
	double FrontSagTangent = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis|Assymmetrical")
	double RearSagTangent = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Default")
	TArray<TSubclassOf<APawn>> PawnClassSelection;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Turret")
	bool TurretBlocking = false;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|State")
	bool LeftCtrlPressed = false;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Turret")
	TArray<FRotator> TurretsRotUnstabilized;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Tank|Turret")
	TArray<FRotator> TurretsRot;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Tank|Gun")
	TArray<FRotator> GunsRot;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Gun")
	TArray<FRotator> GunsRotUnstabilized;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Scattering")
	TArray<FRotator> GunsRotPrevFrame;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Scattering")
	TArray<FRotator> TurretsRotPrevFrame;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Turret")
	FRotator MainTurretAndGunRotation = FRotator::ZeroRotator;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|UI")
	int32 DamageCausedUI = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Scattering")
	TArray<double> FinalScattering;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|UI")
	double CrosshairTraceClamp = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|UI")
	double AimPointCorrectionUI = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	bool GeoTracksFlipR = false;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	bool GeoTracksFlipL = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Zoom")
	FVector ArcadeAimCameraOffset = FVector(-180.0, 0.0, -140.0);
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double CurrentAmplitudeMultiplierR = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis|Vibration")
	double DecayRate = 0.3;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|Chassis")
	double CurrentAmplitudeMultiplierL = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank|Chassis|Vibration")
	double InteractionAmplitudeMultiplier = 0.4;

	// New state discovered while porting graph logic (not present as named BP variables - see
	// Docs/notes captured during the C++ port for the reasoning behind each).
	UPROPERTY(BlueprintReadOnly, Category = "Tank|State")
	bool bHasExploded = false;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|State")
	bool bDebugViewFlipFlop = false;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|State")
	bool bLightsFlipFlop = false;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|State")
	bool bInterfaceFlipFlop = false;
	UPROPERTY(BlueprintReadOnly, Category = "Tank|State")
	bool bHotkeysFlipFlop = false;

	// ------------------------------------------------------------------
	// Ported functions (grouped as extracted from the source Blueprint graphs)
	// ------------------------------------------------------------------

	// --- Lifecycle, input, pawn switching, networked events (from EventGraph / UserConstructionScript / PawnSwitching) ---
	UFUNCTION(BlueprintCallable, Category = "Tank|Pawn")
	void PawnSwitching(const TArray<TSubclassOf<APawn>>& PossiblePawnClasses);
	UFUNCTION(BlueprintCallable, Category = "Tank|Weapon")
	void WeaponFire(int32 Index);
	UFUNCTION(Server, Reliable, Category = "Tank|Damage")
	void SERVER_Explode();
	UFUNCTION(NetMulticast, Reliable, Category = "Tank|Damage")
	void MC_Explode();
	UFUNCTION(BlueprintCallable, Category = "Tank|Damage")
	void ClientDestroyed();
	UFUNCTION(BlueprintCallable, Category = "Tank|Audio")
	void UpdateSound();
	UFUNCTION(BlueprintCallable, Category = "Tank|FX")
	void TracksDecal();
	UFUNCTION(NetMulticast, Reliable, Category = "Tank|FX")
	void MulticastSlipCosmetics(UParticleSystemComponent* Particle, bool bNewActive, double Intensity);
	UFUNCTION(Server, Reliable, Category = "Tank|FX")
	void ServerSlipCosmetics(UParticleSystemComponent* Particle, bool bNewActive, double Intensity);
	UFUNCTION(Server, Reliable, Category = "Tank|Camo")
	void SERVER_UpdateCamo();
	UFUNCTION(NetMulticast, Reliable, Category = "Tank|Camo")
	void MC_UpdateCamo();
	UFUNCTION(Server, Reliable, Category = "Tank|FX")
	void ServerLights(bool bLightsOn);
	UFUNCTION(NetMulticast, Reliable, Category = "Tank|FX")
	void MulticastLights(bool bLightsOn);
	UFUNCTION(BlueprintCallable, Category = "Tank|Pawn")
	void SwitchPawn();
	UFUNCTION(Server, Reliable, Category = "Tank|Health")
	void Server_Healing();
	UFUNCTION(NetMulticast, Reliable, Category = "Tank|Health")
	void MC_Healing();
	UFUNCTION(BlueprintCallable, Category = "Tank|UI")
	void UpdateUI_HP();
	UFUNCTION(BlueprintCallable, Category = "Tank|UI")
	void EventUpdateDamageUI(int32 Damage);
	UFUNCTION(BlueprintCallable, Category = "Tank|UI")
	void WeaponReloadUI(int32 WeaponSlot);
	void OnLookUpDown(const FInputActionValue& Value);
	void OnLookRightLeft(const FInputActionValue& Value);
	void OnMoveRightLeft(const FInputActionValue& Value);
	void OnMoveRightLeftCompleted(const FInputActionValue& Value);
	void OnMoveForwardBack(const FInputActionValue& Value);
	void OnMoveForwardBackCompleted(const FInputActionValue& Value);
	void OnHandbrakePressed(const FInputActionValue& Value);
	void OnHandbrakeReleased(const FInputActionValue& Value);
	void OnUseWeapon1(const FInputActionValue& Value);
	void OnUseWeapon2(const FInputActionValue& Value);
	void OnUseWeapon3(const FInputActionValue& Value);
	void OnUseWeapon4(const FInputActionValue& Value);
	void OnUseWeapon5(const FInputActionValue& Value);
	void OnFirePrimaryWeaponStarted(const FInputActionValue& Value);
	void OnFirePrimaryWeaponCompleted(const FInputActionValue& Value);
	void OnReload(const FInputActionValue& Value);
	void OnDebugView(const FInputActionValue& Value);
	void OnDestroySelf(const FInputActionValue& Value);
	void OnToggleInterface(const FInputActionValue& Value);
	void OnBulletTime(const FInputActionValue& Value);
	void OnAimStarted(const FInputActionValue& Value);
	void OnAimCompleted(const FInputActionValue& Value);
	void OnSwitchCamo(const FInputActionValue& Value);
	void OnToggleLights(const FInputActionValue& Value);
	void OnTurretBlockingToggle(const FInputActionValue& Value);
	void OnStabilizerToggle(const FInputActionValue& Value);
	void OnHealingRequested(const FInputActionValue& Value);
	void OnSniperModeToggle(const FInputActionValue& Value);
	void OnZoom(const FInputActionValue& Value);
	void OnShowHotkeys(const FInputActionValue& Value);
	void OnGeoTracksSwitching(const FInputActionValue& Value);
	// Legacy/debug editor hotkeys for tuning wheel radius & UV correction values live-in-PIE; bound via raw K2Node_InputKey, not Enhanced Input
	void OnDebugAdjustWheelRadiusRear(bool bPressed);
	void OnDebugToggleDebugCamera();

	// --- Track & wheel spline animation (Chassis) ---
	UFUNCTION(BlueprintCallable, Category="Chassis")
	void ChassisDistanceDefinition();
	UFUNCTION(BlueprintCallable, Category="Chassis")
	void UpdateTracksMID(UMaterialInstanceDynamic* MaterialInstance, double ChassisDistance);
	UFUNCTION(BlueprintCallable, Category="Chassis")
	USplineComponent* SplineMirrorCopy(USplineComponent* SplineToCopy);
	UFUNCTION(BlueprintCallable, Category="Chassis")
	TArray<UInstancedStaticMeshComponent*> InstanceTracksCreation(USplineComponent* TrackPathToAttach, bool TracksFlipY);
	UFUNCTION(BlueprintCallable, Category="Chassis")
	void SetTracksTransform(const TArray<UInstancedStaticMeshComponent*>& TrackInstances, USplineComponent* TrackPath, double ChassisDistance);
	UFUNCTION(BlueprintCallable, Category="Chassis")
	void TrackPathShift(USplineComponent* TrackPath, bool LeftSide);
	UFUNCTION(BlueprintCallable, Category="Chassis")
	void TrackPathAnimations(USplineComponent* TrackPath, double SaggingDegree, bool ChassisLocked, bool LeftSide);
	UFUNCTION(BlueprintCallable, Category="Chassis")
	void SplinePointsParametersDefinition();
	UFUNCTION(BlueprintCallable, Category="Chassis")
	FVector PointLocationCalculation(int32 SplinePointIndex, bool ChassisLocked, bool InteractWithWheel, double VibrationMaxAmplitude, double VibrationPhase, double SaggingForward, double SaggingBack, double SaggingDegree, int32 ArrayIndex, USplineComponent* TrackPath, bool LeftSide);
	UFUNCTION(BlueprintCallable, Category="Chassis")
	int32 FindSplineXClosestPoint(int32 SplinePointindex);
	UFUNCTION(BlueprintCallable, Category="Chassis")
	void SplineFilletsCompensation(USplineComponent* TrackPath);
	UFUNCTION(BlueprintCallable, Category="Chassis")
	void ShowUVTracks(bool Show);
	UFUNCTION(BlueprintPure, Category="Chassis")
	double WheelRotationDefinition(double Distance, double WheelRadius, double TrackThicknessParam, double WheelSpeedCorrectionUVParam, double WheelStartAngleLeftGeoTracks, double WheelStartAngleRightGeoTracks, double WheelStartAngleLeftUVTracks, double WheelStartAngleRightUVTracks, bool LeftWheel) const;
	UFUNCTION(BlueprintCallable, Category="Chassis")
	void SetTrackDynamicMaterial();

	// --- Suspension / vibration / antenna physics + drive input ---
	UFUNCTION(BlueprintPure, Category="Chassis")
	double SaggingCalculation(double SaggingDegree, double HullDeltaXLocationParam, double ChassisDeltaDistance, bool ChassisLocked) const;
	UFUNCTION(BlueprintCallable, Category="Chassis")
	double VibrationCalculation(double VibrationAmplitude, double VibrationPhase);
	UFUNCTION(BlueprintCallable)
	void AntennaCalculation(const TArray<FTSAntennaParams>& InAntennaParameters);
	UFUNCTION(BlueprintCallable, Category="Default")
	void HullAccelerationDefinition();
	UFUNCTION(BlueprintCallable)
	void TurningControl();
	UFUNCTION(BlueprintCallable)
	void ThrottleControl(double ThrottleActionValue);
	UFUNCTION(BlueprintCallable)
	void PoliceTurn(bool bPressed);

	// --- Turret / gun rotation + ballistics ---
	UFUNCTION(BlueprintCallable, Category="Old")
	void UpdateTurretRotation_Old();
	UFUNCTION(BlueprintCallable, Category="Old")
	void UpdateMachineGunRotation_Old();
	UFUNCTION(BlueprintCallable, Category="Turret")
	void UpdateTurretRotation(FName BoneToRotate, double YawRotSpeed, TSubclassOf<AActor> GunProjectile, FRotator TurretRotCurrent, FVector2D HorizontalRange, bool CalculateBallistix, FVector TargetPoint, float& TurretRotNew_Roll, float& TurretRotNew_Pitch, float& TurretRotNew_Yaw);
	UFUNCTION(BlueprintCallable, Category="Default")
	void UpdateGunRotation(int32 WeaponIndex, FName BoneToRotate, double GunPitchSpeed, FRotator GunRotCurrent, FVector2D VerticalRange, bool CalculateBallistix, FVector TargetPoint, TSubclassOf<AActor> GunProjectile, float& GunRotNew_Roll, float& GunRotNew_Pitch, float& GunRotNew_Yaw);
	UFUNCTION(BlueprintCallable, Category="Ballistics")
	double BallisticsCalculation(FVector VectorBetweenSocketAndTarget, TSubclassOf<AActor> GunProjectile);
	UFUNCTION(BlueprintCallable, Category="Default")
	void TurretsAndGunsRotCalculation();
	UFUNCTION(BlueprintCallable, Category="Default")
	void RecalculateGunAndTurretRotation();
	UFUNCTION(BlueprintCallable, Category="Default")
	void ScatteringCalculation();
	UFUNCTION(BlueprintCallable, Category="Default")
	FRotator SelfCollisionCheck(FName BoneToRotate, FRotator CurrentRotation, FRotator TargetRotation, double GunPitchSpeed, FVector2D VerticalRange);

	// --- Camera, HUD and UI update functions ---
	UFUNCTION(BlueprintCallable, Category = "Old")
	void ReplicateControlRotation();
	public:
	UFUNCTION(BlueprintCallable, Category = "UI")
	void UpdateHUD();

	private:
	// BP_TankWeapon_C stays a Blueprint class (BPC_TankWeapon is a generic UActorComponent*), so its fields
	// (Weapons array of S_Weapon, CurrentWeapon, CurrentWeaponIndex, Weapon1..5CurrentAmmo, ReloadingWeapon1..5)
	// are not statically typed in C++. These reflection helpers read them by FName at runtime. Shared by all
	// functions in this cluster that touch BPC_TankWeapon or a widget-bound HUD field.
	static int32 GetIntProp(const UObject* Obj, FName PropertyName, int32 DefaultValue = 0);
	static bool GetBoolProp(const UObject* Obj, FName PropertyName, bool DefaultValue = false);
	static double GetDoubleProp(const UObject* Obj, FName PropertyName, double DefaultValue = 0.0);
	static FVector GetVectorProp(const UObject* Obj, FName PropertyName, FVector DefaultValue = FVector::ZeroVector);
	static int32 GetArrayNum(const UObject* Obj, FName ArrayPropertyName);
	static void* GetStructPropPtr(UObject* Obj, FName PropertyName, UScriptStruct** OutStruct);
	static void* GetArrayElementStructPtr(UObject* Obj, FName ArrayPropertyName, int32 Index, UScriptStruct** OutStruct);
	static FText GetTextFieldFromStruct(void* StructPtr, UScriptStruct* Struct, FName FieldName);
	static double GetDoubleFieldFromStruct(void* StructPtr, UScriptStruct* Struct, FName FieldName, double DefaultValue = 0.0);
	UFUNCTION(BlueprintCallable, Category = "UI")
	void UpdateCrosshairPositionAndSize();
	UFUNCTION(BlueprintCallable, Category = "Camera")
	void CameraAutoSetting();
	// GetMesh() (AWheeledVehiclePawn::Mesh) is unreliable at the exact point BeginPlay() runs -
	// the Chaos vehicle's mesh/physics state isn't fully settled yet on that same frame, so
	// CameraAutoSetting's null-guard was firing every play session, leaving the camera at raw CDO
	// defaults (visually inside the tank). Deferred one tick via this handle+callback instead.
	FTimerHandle DeferredBeginPlaySetupTimerHandle;
	void DeferredBeginPlaySetup();
	UFUNCTION(BlueprintCallable, Category = "Camera")
	double CameraPitchLimit(double AxisValue);
	// The BP macro 'Zoom+SniperMode' has 3 separate exec entry points (SniperModeToggle, Zoom+, Zoom-) bound to
	// different input actions in EventGraph; ported as 3 independent UFUNCTIONs rather than one macro-call.
	UFUNCTION(BlueprintCallable, Category = "Zoom")
	void SniperModeToggle();

	UFUNCTION()
	void OnAimTimelineUpdate(float AimingPhase);

	UFUNCTION(BlueprintCallable, Category = "Zoom")
	void ZoomIn();

	UFUNCTION(BlueprintCallable, Category = "Zoom")
	void ZoomOut();
	UFUNCTION(BlueprintCallable, Category = "UI")
	void UpdateHealthBar();
	UFUNCTION(BlueprintCallable, Category = "UI")
	void WeaponSlotsDisplay();
	UFUNCTION(BlueprintCallable, Category = "UI")
	void UpdateChosenWeaponUI();
	UFUNCTION(BlueprintCallable, Category = "UI")
	void UpdateZoomRatioUI();
	UFUNCTION(BlueprintCallable, Category = "UI")
	void UpdateChosenVehicleUI();
	public:
	UFUNCTION(BlueprintCallable, Category = "UI")
	void UpdateDamageCausedUI(int32 Damage);

	private:
	void HideDamageCausedUI();

	FTimerHandle DamageCausedUITimerHandle;
	UFUNCTION(BlueprintCallable, Category = "UI")
	bool ReloadWeaponUI(int32 WeaponIndex);

protected:
	FTimerHandle DamageUITimerHandle;

private:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
