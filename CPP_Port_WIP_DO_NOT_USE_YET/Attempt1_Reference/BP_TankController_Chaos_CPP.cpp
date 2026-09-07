#include "Tank/BP_TankController_Chaos_CPP.h"

#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/AudioComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SplineComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TimelineComponent.h"
#include "Curves/CurveFloat.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Camera/CameraShakeBase.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "UObject/ConstructorHelpers.h"
#include "Sound/SoundCue.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

// BP_TankWeapon_C's "Weapons" array element struct (S_Weapon) is a Blueprint UserDefinedStruct with no
// native C++ type, so its fields are read via reflection rather than a strongly-typed cast.
namespace TSWeaponReflection
{
	static FArrayProperty* FindWeaponsArrayProperty(UActorComponent* WeaponComponent, UScriptStruct*& OutElementStruct)
	{
		OutElementStruct = nullptr;
		if (!WeaponComponent)
		{
			return nullptr;
		}
		FArrayProperty* ArrayProp = FindFProperty<FArrayProperty>(WeaponComponent->GetClass(), TEXT("Weapons"));
		if (!ArrayProp)
		{
			return nullptr;
		}
		FStructProperty* ElemStructProp = CastField<FStructProperty>(ArrayProp->Inner);
		if (!ElemStructProp)
		{
			return nullptr;
		}
		OutElementStruct = ElemStructProp->Struct;
		return ArrayProp;
	}

	static double GetDouble(UScriptStruct* Struct, void* ElemPtr, FName FieldName)
	{
		if (!Struct || !ElemPtr) return 0.0;
		if (FDoubleProperty* P = FindFProperty<FDoubleProperty>(Struct, FieldName)) return P->GetPropertyValue_InContainer(ElemPtr);
		if (FFloatProperty* P = FindFProperty<FFloatProperty>(Struct, FieldName)) return static_cast<double>(P->GetPropertyValue_InContainer(ElemPtr));
		if (FIntProperty* P = FindFProperty<FIntProperty>(Struct, FieldName)) return static_cast<double>(P->GetPropertyValue_InContainer(ElemPtr));
		return 0.0;
	}

	static FName GetName(UScriptStruct* Struct, void* ElemPtr, FName FieldName)
	{
		if (Struct && ElemPtr)
		{
			if (FNameProperty* P = FindFProperty<FNameProperty>(Struct, FieldName))
			{
				return P->GetPropertyValue_InContainer(ElemPtr);
			}
		}
		return NAME_None;
	}

	static FVector2D GetVector2D(UScriptStruct* Struct, void* ElemPtr, FName FieldName)
	{
		if (Struct && ElemPtr)
		{
			if (FStructProperty* P = FindFProperty<FStructProperty>(Struct, FieldName))
			{
				if (P->Struct == TBaseStructure<FVector2D>::Get())
				{
					return *P->ContainerPtrToValuePtr<FVector2D>(ElemPtr);
				}
			}
		}
		return FVector2D::ZeroVector;
	}

	static TSubclassOf<AActor> GetActorClass(UScriptStruct* Struct, void* ElemPtr, FName FieldName)
	{
		if (Struct && ElemPtr)
		{
			if (FClassProperty* P = FindFProperty<FClassProperty>(Struct, FieldName))
			{
				return TSubclassOf<AActor>(Cast<UClass>(P->GetPropertyValue_InContainer(ElemPtr)));
			}
			if (FSoftClassProperty* P = FindFProperty<FSoftClassProperty>(Struct, FieldName))
			{
				const FSoftObjectPtr SoftPtr(P->GetPropertyValue_InContainer(ElemPtr));
				return TSubclassOf<AActor>(Cast<UClass>(SoftPtr.Get()));
			}
		}
		return nullptr;
	}
}

ABP_TankController_Chaos_CPP::ABP_TankController_Chaos_CPP()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	// Light_R / Light_L: authored spot-light defaults recovered from the source BP (headlights).
	Light_R = CreateDefaultSubobject<USpotLightComponent>(TEXT("Light_R"));
	Light_R->SetupAttachment(GetMesh());
	Light_R->SetRelativeLocation(FVector(368.0, -78.292870, 115.0));
	Light_R->Intensity = 250.0f;
	Light_R->SetLightColor(FLinearColor(FColor(255, 240, 195)));
	Light_R->OuterConeAngle = 96.0f;
	Light_R->AttenuationRadius = 15000.0f;
	Light_L = CreateDefaultSubobject<USpotLightComponent>(TEXT("Light_L"));
	Light_L->SetupAttachment(GetMesh());
	Light_L->SetRelativeLocation(FVector(368.0, 78.707130, 115.0));
	Light_L->Intensity = 250.0f;
	Light_L->SetLightColor(FLinearColor(FColor(255, 240, 195)));
	Light_L->OuterConeAngle = 96.0f;
	Light_L->AttenuationRadius = 15000.0f;

	static ConstructorHelpers::FObjectFinder<UParticleSystem> ExhaustFinder(TEXT("/Game/YI_TankCollection/Particles/Master/P_Exhaust.P_Exhaust"));
	static ConstructorHelpers::FObjectFinder<UParticleSystem> WheelSlideFinder(TEXT("/Game/YI_TankCollection/Particles/Master/P_WheelSlide.P_WheelSlide"));
	static ConstructorHelpers::FObjectFinder<UParticleSystem> FlamesFinder(TEXT("/Game/YI_TankCollection/Particles/Master/P_Flames.P_Flames"));

	P_Exhaust = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("P_Exhaust"));
	P_Exhaust->SetupAttachment(GetMesh());
	P_Exhaust->SetRelativeLocation(FVector(-375.0, 15.0, 110.0));
	P_Exhaust->SetRelativeScale3D(FVector(2.0, 2.0, 2.0));
	if (ExhaustFinder.Succeeded()) { P_Exhaust->SetTemplate(ExhaustFinder.Object); }
	P_Exhaust1 = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("P_Exhaust1"));
	P_Exhaust1->SetupAttachment(GetMesh());
	P_Exhaust1->SetRelativeLocation(FVector(-375.0, 65.0, 110.0));
	if (ExhaustFinder.Succeeded()) { P_Exhaust1->SetTemplate(ExhaustFinder.Object); }
	P_Exhaust2 = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("P_Exhaust2"));
	P_Exhaust2->SetupAttachment(GetMesh());
	P_Exhaust2->SetRelativeLocation(FVector(-375.0, -55.0, 110.0));
	if (ExhaustFinder.Succeeded()) { P_Exhaust2->SetTemplate(ExhaustFinder.Object); }
	SlideBackRight = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("SlideBackRight"));
	SlideBackRight->SetupAttachment(GetMesh());
	SlideBackRight->SetRelativeLocation(FVector(-250.0, 130.0, 0.0));
	SlideBackRight->bAutoActivate = false;
	if (WheelSlideFinder.Succeeded()) { SlideBackRight->SetTemplate(WheelSlideFinder.Object); }
	SlideBackLeft = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("SlideBackLeft"));
	SlideBackLeft->SetupAttachment(GetMesh());
	SlideBackLeft->SetRelativeLocation(FVector(-250.0, -130.0, 0.0));
	SlideBackLeft->bAutoActivate = false;
	if (WheelSlideFinder.Succeeded()) { SlideBackLeft->SetTemplate(WheelSlideFinder.Object); }

	SpringArmArcade = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArmArcade"));
	SpringArmArcade->SetupAttachment(GetMesh());
	SpringArmArcade->bUsePawnControlRotation = true;
	SpringArmArcade->TargetArmLength = 1000.0f;
	SpringArmArcade->SocketOffset = FVector(0.0, 0.0, 250.0);
	SpringArmArcade->bDoCollisionTest = true;
	SpringArmSniper = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArmSniper"));
	SpringArmSniper->SetupAttachment(SpringArmArcade);
	SpringArmSniper->bUsePawnControlRotation = false;
	SpringArmSniper->TargetArmLength = 0.0f;
	SpringArmSniper->bDoCollisionTest = false;
	SpringArmSniper->bEnableCameraLag = true;
	SpringArmSniper->bEnableCameraRotationLag = true;
	SpringArmSniper->CameraLagSpeed = 15.0f;
	SpringArmSniper->CameraRotationLagSpeed = 15.0f;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArmSniper);
	Camera->FieldOfView = 90.0f;
	Camera->PostProcessSettings.bOverride_VignetteIntensity = true;
	Camera->PostProcessSettings.VignetteIntensity = 0.54f;

	Brake_L = CreateDefaultSubobject<UPointLightComponent>(TEXT("Brake_L"));
	Brake_L->SetupAttachment(GetMesh());
	Brake_L->SetRelativeLocation(FVector(-365.576111, -145.002548, 147.0));
	Brake_L->Intensity = 35.0f;
	Brake_L->SetLightColor(FLinearColor(FColor(255, 0, 0)));
	Brake_L->AttenuationRadius = 500.0f;
	Brake_L->VolumetricScatteringIntensity = 5.0f;
	Brake_R = CreateDefaultSubobject<UPointLightComponent>(TEXT("Brake_R"));
	Brake_R->SetupAttachment(GetMesh());
	Brake_R->SetRelativeLocation(FVector(-365.576111, 145.997452, 147.0));
	Brake_R->Intensity = 35.0f;
	Brake_R->SetLightColor(FLinearColor(FColor(255, 0, 0)));
	Brake_R->AttenuationRadius = 500.0f;
	Brake_R->VolumetricScatteringIntensity = 5.0f;

	static ConstructorHelpers::FObjectFinder<USoundBase> TurretMotorSoundFinder(TEXT("/Game/YI_TankCollection/Sounds/Master/CUE_TurretMotor.CUE_TurretMotor"));
	static ConstructorHelpers::FObjectFinder<USoundBase> TankEngineSoundFinder(TEXT("/Game/YI_TankCollection/Sounds/Master/CUE_TankEngine.CUE_TankEngine"));
	static ConstructorHelpers::FObjectFinder<USoundBase> FireSoundFinder(TEXT("/Game/YI_TankCollection/Sounds/Master/CUE_Tank_Burning_Loop.CUE_Tank_Burning_Loop"));

	TurretMotor = CreateDefaultSubobject<UAudioComponent>(TEXT("TurretMotor"));
	TurretMotor->SetupAttachment(GetMesh());
	TurretMotor->VolumeMultiplier = 0.65f;
	if (TurretMotorSoundFinder.Succeeded()) { TurretMotor->SetSound(TurretMotorSoundFinder.Object); }
	TankEngine = CreateDefaultSubobject<UAudioComponent>(TEXT("TankEngine"));
	TankEngine->SetupAttachment(GetMesh());
	TankEngine->VolumeMultiplier = 0.1f;
	if (TankEngineSoundFinder.Succeeded()) { TankEngine->SetSound(TankEngineSoundFinder.Object); }
	Fire = CreateDefaultSubobject<UAudioComponent>(TEXT("Fire"));
	Fire->SetupAttachment(GetMesh());
	Fire->bAutoActivate = false;
	Fire->VolumeMultiplier = 0.25f;
	if (FireSoundFinder.Succeeded()) { Fire->SetSound(FireSoundFinder.Object); }

	static ConstructorHelpers::FObjectFinder<UPhysicalMaterial> TankMetalPhysMatFinder(TEXT("/Game/YI_TankCollection/Blueprint/Master/PhysicalMaterials/PM_Metal.PM_Metal"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ExplosiveDecalMatFinder(TEXT("/Game/YI_TankCollection/Materials/Master/Instances/MI_Explosive_Decal_Tank.MI_Explosive_Decal_Tank"));

	Tank_Destroyed = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Tank_Destroyed"));
	Tank_Destroyed->SetupAttachment(GetMesh());
	Tank_Destroyed->SetVisibility(false);
	if (!HasAnyFlags(RF_ClassDefaultObject) && TankMetalPhysMatFinder.Succeeded())
	{
		Tank_Destroyed->BodyInstance.SetPhysMaterialOverride(TankMetalPhysMatFinder.Object);
	}
	Decal = CreateDefaultSubobject<UDecalComponent>(TEXT("Decal"));
	Decal->SetupAttachment(Tank_Destroyed);
	Decal->SetVisibility(false);
	Decal->SetRelativeRotation(FRotator(-90.0, 179.980225, -179.980179));
	Decal->SetRelativeScale3D(FVector(7.0, 1.0, 1.0));
	Decal->DecalSize = FVector(10.666667, 1024.0, 1024.0);
	if (ExplosiveDecalMatFinder.Succeeded()) { Decal->SetDecalMaterial(ExplosiveDecalMatFinder.Object); }
	DestroyedFlames = CreateDefaultSubobject<UParticleSystemComponent>(TEXT("DestroyedFlames"));
	DestroyedFlames->SetupAttachment(Tank_Destroyed);
	DestroyedFlames->SetRelativeLocation(FVector(0.0, 0.0, 100.0));
	DestroyedFlames->SetVisibility(false);
	DestroyedFlames->bAutoActivate = false;
	if (FlamesFinder.Succeeded()) { DestroyedFlames->SetTemplate(FlamesFinder.Object); }

	// Default Input Mapping Context & Input Actions
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> DefaultMappingContextFinder(TEXT("/Game/YI_TankCollection/Inputs/IMC_Default.IMC_Default"));
	if (DefaultMappingContextFinder.Succeeded()) { DefaultMappingContext = DefaultMappingContextFinder.Object; }

	static ConstructorHelpers::FObjectFinder<UInputAction> LookUpDownFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_LookUpDown.IA_LookUpDown"));
	if (LookUpDownFinder.Succeeded()) { IA_LookUpDown = LookUpDownFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> LookRightLeftFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_LookRightLeft.IA_LookRightLeft"));
	if (LookRightLeftFinder.Succeeded()) { IA_LookRightLeft = LookRightLeftFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> MoveRightLeftFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_MoveRightLeft.IA_MoveRightLeft"));
	if (MoveRightLeftFinder.Succeeded()) { IA_MoveRightLeft = MoveRightLeftFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> MoveForwardBackFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_MoveForwardBack.IA_MoveForwardBack"));
	if (MoveForwardBackFinder.Succeeded()) { IA_MoveForwardBack = MoveForwardBackFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> HandbrakeFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_Handbrake.IA_Handbrake"));
	if (HandbrakeFinder.Succeeded()) { IA_Handbrake = HandbrakeFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> DebugViewFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_DebugView.IA_DebugView"));
	if (DebugViewFinder.Succeeded()) { IA_DebugView = DebugViewFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> DestroySelfFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_DestroySelf.IA_DestroySelf"));
	if (DestroySelfFinder.Succeeded()) { IA_DestroySelf = DestroySelfFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> ToggleInterfaceFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_ToggleInterface.IA_ToggleInterface"));
	if (ToggleInterfaceFinder.Succeeded()) { IA_ToggleInterface = ToggleInterfaceFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> BulletTimeFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_BulletTime.IA_BulletTime"));
	if (BulletTimeFinder.Succeeded()) { IA_BulletTime = BulletTimeFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> UseWeapon1Finder(TEXT("/Game/YI_TankCollection/Inputs/IA_UseWeapon1.IA_UseWeapon1"));
	if (UseWeapon1Finder.Succeeded()) { IA_UseWeapon1 = UseWeapon1Finder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> UseWeapon2Finder(TEXT("/Game/YI_TankCollection/Inputs/IA_UseWeapon2.IA_UseWeapon2"));
	if (UseWeapon2Finder.Succeeded()) { IA_UseWeapon2 = UseWeapon2Finder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> UseWeapon3Finder(TEXT("/Game/YI_TankCollection/Inputs/IA_UseWeapon3.IA_UseWeapon3"));
	if (UseWeapon3Finder.Succeeded()) { IA_UseWeapon3 = UseWeapon3Finder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> UseWeapon4Finder(TEXT("/Game/YI_TankCollection/Inputs/IA_UseWeapon4.IA_UseWeapon4"));
	if (UseWeapon4Finder.Succeeded()) { IA_UseWeapon4 = UseWeapon4Finder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> UseWeapon5Finder(TEXT("/Game/YI_TankCollection/Inputs/IA_UseWeapon5.IA_UseWeapon5"));
	if (UseWeapon5Finder.Succeeded()) { IA_UseWeapon5 = UseWeapon5Finder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> FirePrimaryWeaponFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_FirePrimaryWeapon.IA_FirePrimaryWeapon"));
	if (FirePrimaryWeaponFinder.Succeeded()) { IA_FirePrimaryWeapon = FirePrimaryWeaponFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> ReloadFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_Reload.IA_Reload"));
	if (ReloadFinder.Succeeded()) { IA_Reload = ReloadFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> AimFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_Aim.IA_Aim"));
	if (AimFinder.Succeeded()) { IA_Aim = AimFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> SwitchCamoFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_SwitchCamo.IA_SwitchCamo"));
	if (SwitchCamoFinder.Succeeded()) { IA_SwitchCamo = SwitchCamoFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> ToggleLightsFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_ToggleLights.IA_ToggleLights"));
	if (ToggleLightsFinder.Succeeded()) { IA_ToggleLights = ToggleLightsFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> TurretBlockingFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_TurretBlocking.IA_TurretBlocking"));
	if (TurretBlockingFinder.Succeeded()) { IA_TurretBlocking = TurretBlockingFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> StabilizerFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_Stabilizer.IA_Stabilizer"));
	if (StabilizerFinder.Succeeded()) { IA_Stabilizer = StabilizerFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> HealingFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_Healing.IA_Healing"));
	if (HealingFinder.Succeeded()) { IA_Healing = HealingFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> SniperModeToggleFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_SniperModeToggle.IA_SniperModeToggle"));
	if (SniperModeToggleFinder.Succeeded()) { IA_SniperModeToggle = SniperModeToggleFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> ZoomFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_Zoom.IA_Zoom"));
	if (ZoomFinder.Succeeded()) { IA_Zoom = ZoomFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> ShowHotkeysFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_ShowHotkeys.IA_ShowHotkeys"));
	if (ShowHotkeysFinder.Succeeded()) { IA_ShowHotkeys = ShowHotkeysFinder.Object; }
	static ConstructorHelpers::FObjectFinder<UInputAction> GeoTracksSwitchingFinder(TEXT("/Game/YI_TankCollection/Inputs/IA_GeoTracksSwitching.IA_GeoTracksSwitching"));
	if (GeoTracksSwitchingFinder.Succeeded()) { IA_GeoTracksSwitching = GeoTracksSwitchingFinder.Object; }

	// Default Curves
	static ConstructorHelpers::FObjectFinder<UCurveFloat> SteeringCurveFinder(TEXT("/Game/YI_TankCollection/Blueprint/Master/Curve/FC_SteeringTank.FC_SteeringTank"));
	if (SteeringCurveFinder.Succeeded()) { SteeringCurve = SteeringCurveFinder.Object; }

	static ConstructorHelpers::FObjectFinder<UParticleSystem> ExplosionEffectFinder(TEXT("/Game/YI_TankCollection/Particles/Master/P_Explosion_Bomb.P_Explosion_Bomb"));
	if (ExplosionEffectFinder.Succeeded()) { ExplosionEffect = ExplosionEffectFinder.Object; }
	static ConstructorHelpers::FObjectFinder<USoundBase> ExplosionSoundFinder(TEXT("/Game/YI_TankCollection/Sounds/Master/CUE_ImpactExplosive.CUE_ImpactExplosive"));
	if (ExplosionSoundFinder.Succeeded()) { ExplosionSound = ExplosionSoundFinder.Object; }
	static ConstructorHelpers::FObjectFinder<USoundBase> ExplosionSound2Finder(TEXT("/Game/YI_TankCollection/Sounds/Master/CUE_Debris_Cue.CUE_Debris_Cue"));
	if (ExplosionSound2Finder.Succeeded()) { ExplosionSound2 = ExplosionSound2Finder.Object; }

	// TrackPath_R: authored 12-point closed-loop spline tracing the track path, recovered from the
	// source BP (see recovered_component_data.md) - exact tangents reproduce the original curve shape.
	TrackPath_R = CreateDefaultSubobject<USplineComponent>(TEXT("TrackPath_R"));
	TrackPath_R->SetupAttachment(GetMesh());
	TrackPath_R->SetRelativeLocation(FVector(0.0, 98.666092, 0.0));
	TrackPath_R->SetRelativeRotation(FRotator(0.0, -180.0, 0.0));
	TrackPath_R->ClearSplinePoints(false);
	{
		struct FTrackSplinePointDef { FVector Location; FVector Tangent; };
		static const FTrackSplinePointDef TrackPoints[] = {
			{ FVector(-230.000000, 0.000000, 0.000000), FVector(62.008676, 0.000000, 0.000000) },
			{ FVector(-140.000000, 0.000000, 0.000000), FVector(60.000000, 0.000000, 0.000000) },
			{ FVector(-50.000000, 0.000000, 0.000000), FVector(60.000000, 0.000000, 0.000000) },
			{ FVector(40.000000, 0.000000, 0.000000), FVector(60.000000, 0.000000, 0.000000) },
			{ FVector(130.000000, 0.000000, 0.000000), FVector(60.000000, 0.000000, 0.000000) },
			{ FVector(220.000000, 0.000000, 0.000000), FVector(62.137016, 0.000000, 0.000000) },
			{ FVector(257.991581, 0.000000, 10.076616), FVector(25.735703, 0.000000, 21.250000) },
			{ FVector(271.471405, 0.000000, 42.500000), FVector(0.000000, 0.000000, 50.000000) },
			{ FVector(220.000000, 0.000000, 85.000000), FVector(-60.000000, 0.000000, 0.000000) },
			{ FVector(-230.000000, 0.000000, 85.000000), FVector(-60.000000, 0.000000, 0.000000) },
			{ FVector(-275.920563, 0.000000, 42.500000), FVector(0.000000, 0.000000, -50.000000) },
			{ FVector(-266.828299, -0.000000, 14.203281), FVector(22.972598, -0.000000, -21.261399) },
		};
		for (const FTrackSplinePointDef& PointDef : TrackPoints)
		{
			TrackPath_R->AddSplinePoint(PointDef.Location, ESplineCoordinateSpace::Local, false);
		}
		for (int32 PointIndex = 0; PointIndex < UE_ARRAY_COUNT(TrackPoints); ++PointIndex)
		{
			TrackPath_R->SetSplinePointType(PointIndex, ESplinePointType::Curve, false);
			TrackPath_R->SetTangentsAtSplinePoint(PointIndex, TrackPoints[PointIndex].Tangent, TrackPoints[PointIndex].Tangent, ESplineCoordinateSpace::Local, false);
		}
		TrackPath_R->SetClosedLoop(true, false);
		TrackPath_R->UpdateSpline();
	}

	DebugCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("DebugCamera"));
	DebugCamera->SetupAttachment(GetMesh());
	DebugCamera->Deactivate();

	MainGunSpringTimeline = CreateDefaultSubobject<UTimelineComponent>(TEXT("MainGunSpringTimeline"));
	AimTimeline = CreateDefaultSubobject<UTimelineComponent>(TEXT("AimTimeline"));

	VehicleMovement = Cast<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent());

	// Fixed-capacity logic buffers: the source BP authored these as zero-filled array
	// defaults (10 turret/gun slots, 30 wheel/antenna slots) rather than sizing them at
	// runtime, so every index-based read/write throughout this class assumes they're
	// already at capacity.
	TurretsRot.Init(FRotator::ZeroRotator, 10);
	TurretsRotUnstabilized.Init(FRotator::ZeroRotator, 10);
	TurretsRotPrevFrame.Init(FRotator::ZeroRotator, 10);
	GunsRot.Init(FRotator::ZeroRotator, 10);
	GunsRotUnstabilized.Init(FRotator::ZeroRotator, 10);
	GunsRotPrevFrame.Init(FRotator::ZeroRotator, 10);
	WheelsZOffsets.Init(0.0, 30);
	AntennaRotation.Init(FRotator::ZeroRotator, 30);
	AntennaCurrentSpeed.Init(FVector::ZeroVector, 30);
}

void ABP_TankController_Chaos_CPP::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABP_TankController_Chaos_CPP, Rep_ControlRotation);
	DOREPLIFETIME(ABP_TankController_Chaos_CPP, TurretsRot);
	DOREPLIFETIME(ABP_TankController_Chaos_CPP, GunsRot);
}


// ============================================================
// Lifecycle, input, pawn switching, networked events (from EventGraph / UserConstructionScript / PawnSwitching)
// ============================================================
void ABP_TankController_Chaos_CPP::PawnSwitching(const TArray<TSubclassOf<APawn>>& PossiblePawnClasses)
{
	// Find current pawn's class in the possible list, cycle to the next one (wrap to 0 at the end)
	APawn* CurrentPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!CurrentPawn)
	{
		return;
	}

	int32 FoundIndex = PossiblePawnClasses.Find(CurrentPawn->GetClass());
	const int32 NextIndex = (FoundIndex == PawnClassSelection.Num() - 1) ? 0 : FoundIndex + 1;
	// Note: BP reads Dimension 1 from PawnClassSelection's last index (not PossiblePawnClasses) for the wrap check - ported verbatim
	TSubclassOf<APawn> NextClass = PossiblePawnClasses.IsValidIndex(NextIndex) ? PossiblePawnClasses[NextIndex] : nullptr;
	if (!NextClass)
	{
		return;
	}

	const FTransform SpawnTransform(FRotator(CurrentPawn->GetActorRotation().Pitch, CurrentPawn->GetActorRotation().Yaw, 0.0), CurrentPawn->GetActorLocation());
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ABP_TankController_Chaos_CPP* NewTankPawn = Cast<ABP_TankController_Chaos_CPP>(GetWorld()->SpawnActor(*NextClass, &SpawnTransform, SpawnParams));
	if (!NewTankPawn)
	{
		return;
	}

	APawn* OldPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	OldPawn->Destroy();

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (PC)
	{
		PC->Possess(NewTankPawn);
	}

	NewTankPawn->Crosshair = NewTankPawn->Crosshair; // self-assignment no-op in BP (Set Crosshair on self from self); kept for 1:1 fidelity
	NewTankPawn->PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	NewTankPawn->HUD = NewTankPawn->HUD;
	NewTankPawn->WeaponSlotsDisplay();
	NewTankPawn->UpdateChosenWeaponUI();
	NewTankPawn->UpdateZoomRatioUI();
	NewTankPawn->UpdateChosenVehicleUI();

	// Bind Event to Weapon Fire (BPC_TankWeapon stays Blueprint-only - bound via reflection, see BeginPlay note)
	if (NewTankPawn->BPC_TankWeapon)
	{
		FMulticastDelegateProperty* FireProp = FindFProperty<FMulticastDelegateProperty>(NewTankPawn->BPC_TankWeapon->GetClass(), TEXT("WeaponFire"));
		if (FireProp)
		{
			FScriptDelegate Delegate;
			Delegate.BindUFunction(NewTankPawn, FName("WeaponFire"));
			FireProp->AddDelegate(Delegate, NewTankPawn->BPC_TankWeapon, FireProp->ContainerPtrToValuePtr<void>(NewTankPawn->BPC_TankWeapon));
		}
	}
}
void ABP_TankController_Chaos_CPP::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SetTrackDynamicMaterial();

	// Left spline / track instances creation
	if (UChaosVehicleMovementComponent* MoveComp = GetVehicleMovementComponent())
	{
		if (UChaosWheeledVehicleMovementComponent* WheeledMoveComp = Cast<UChaosWheeledVehicleMovementComponent>(MoveComp))
		{
			VehicleMovement = WheeledMoveComp;
			PhysWheelsAmount = VehicleMovement->GetNumWheels(); // Works for the player but not reliably for bots - re-set in BeginPlay
		}
	}

	if (TrackPath_R)
	{
		TrackPath_L = SplineMirrorCopy(TrackPath_R);
	}
	TracksInstances_R = InstanceTracksCreation(TrackPath_R, GeoTracksFlipR);
	TracksInstances_L = InstanceTracksCreation(TrackPath_L, GeoTracksFlipL);

	// Creates the Spline Track in real time (editor preview)
	if (EditorSplinePreview)
	{
		SetTracksTransform(TracksInstances_R, TrackPath_R, ChassisDistanceR);
		SetTracksTransform(TracksInstances_L, TrackPath_L, ChassisDistanceL);
	}

	// Fix "Attempted to access index 0 from array of length 0" - only resize FinalScattering if Weapons is non-empty
	if (BPC_TankWeapon)
	{
		// Weapons array lives on BP_TankWeapon_C (BP-only); length obtained via reflection
		if (FArrayProperty* WeaponsProp = FindFProperty<FArrayProperty>(BPC_TankWeapon->GetClass(), TEXT("Weapons")))
		{
			FScriptArrayHelper Helper(WeaponsProp, WeaponsProp->ContainerPtrToValuePtr<void>(BPC_TankWeapon));
			const int32 WeaponsNum = Helper.Num();
			for (int32 i = 0; i < WeaponsNum - 1; ++i)
			{
				if (FinalScattering.IsValidIndex(i) || i >= FinalScattering.Num())
				{
					FinalScattering.SetNum(FMath::Max(FinalScattering.Num(), i + 1));
				}
				FinalScattering[i] = 1.0;
			}
		}
	}

	// Size VibrationOffset_R/L to match TrackPath_R's spline point count
	// (mirrors the original UserConstructionScript's ForLoop(0, NumPoints-1) + Set Array Elem bSizeToFit=true)
	if (TrackPath_R)
	{
		const int32 NumPoints = TrackPath_R->GetNumberOfSplinePoints();
		VibrationOffset_R.SetNum(FMath::Max(VibrationOffset_R.Num(), NumPoints));
		VibrationOffset_L.SetNum(FMath::Max(VibrationOffset_L.Num(), NumPoints));
	}
}
void ABP_TankController_Chaos_CPP::BeginPlay()
{
	Super::BeginPlay();

	// Safety net for VibrationOffset_R/L sizing: OnConstruction (where this is normally
	// done) isn't reliably invoked for pawns spawned at runtime rather than placed in the
	// level, so re-check it here too.
	if (TrackPath_R)
	{
		const int32 NumPoints = TrackPath_R->GetNumberOfSplinePoints();
		VibrationOffset_R.SetNum(FMath::Max(VibrationOffset_R.Num(), NumPoints));
		VibrationOffset_L.SetNum(FMath::Max(VibrationOffset_L.Num(), NumPoints));
	}

	// Fallback load Blueprint classes at runtime if not assigned on the child CDO
	if (!TankWeaponComponentClass)
	{
		TankWeaponComponentClass = StaticLoadClass(UActorComponent::StaticClass(), nullptr, TEXT("/Game/YI_TankCollection/Blueprint/Master/Components/BP_TankWeapon.BP_TankWeapon_C"));
	}
	if (!HUDClass)
	{
		HUDClass = StaticLoadClass(UUserWidget::StaticClass(), nullptr, TEXT("/Game/YI_TankCollection/Blueprint/Master/Widgets/W_MainHUD.W_MainHUD_C"));
	}
	if (!CrosshairClass)
	{
		CrosshairClass = StaticLoadClass(UUserWidget::StaticClass(), nullptr, TEXT("/Game/YI_TankCollection/Blueprint/Master/Widgets/W_Crosshair.W_Crosshair_C"));
	}
	if (!CameraShakeClass)
	{
		CameraShakeClass = StaticLoadClass(UCameraShakeBase::StaticClass(), nullptr, TEXT("/Game/YI_TankCollection/Blueprint/Master/Components/BP_CameraShake_MainTurret.BP_CameraShake_MainTurret_C"));
	}

	// Instantiate BPC_TankWeapon if configured and not yet spawned
	if (!BPC_TankWeapon && TankWeaponComponentClass)
	{
		BPC_TankWeapon = NewObject<UActorComponent>(this, TankWeaponComponentClass, TEXT("BPC_TankWeapon"));
		if (BPC_TankWeapon)
		{
			BPC_TankWeapon->RegisterComponent();
		}
	}

	// --- Sequence branch 0: widgets, only for the locally controlling player controller
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PlayerController = PC;
		HUD = CreateWidget<UUserWidget>(PC, HUDClass);
		if (HUD)
		{
			HUD->AddToViewport();
			WeaponSlotsDisplay();
			UpdateChosenWeaponUI();
			UpdateChosenVehicleUI();
		}
		Crosshair = CreateWidget<UUserWidget>(PC, CrosshairClass);
		if (Crosshair)
		{
			Crosshair->AddToViewport();
		}
		if (BPC_TankWeapon)
		{
			// Bind Event to Weapon Fire - BP_TankWeapon_C stays Blueprint-only, bind via reflection
			if (FMulticastDelegateProperty* FireProp = FindFProperty<FMulticastDelegateProperty>(BPC_TankWeapon->GetClass(), TEXT("WeaponFire")))
			{
				FScriptDelegate Delegate;
				Delegate.BindUFunction(this, FName("WeaponFire"));
				FireProp->AddDelegate(Delegate, BPC_TankWeapon, FireProp->ContainerPtrToValuePtr<void>(BPC_TankWeapon));
			}
		}
	}

	// --- Sequence branch 1: camera/physics/spline setup (synchronous execution matching Blueprint 1:1)
	CameraAutoSetting();
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		SkelMesh->SetAllBodiesPhysicsBlendWeight(0.0f);
	}
	FilletsCompensation = TrackThickness / 2.0;
	SplineFilletsCompensation(TrackPath_R);
	SplineFilletsCompensation(TrackPath_L);
	SplinePointsParametersDefinition();
	PhysWheelsAmount = VehicleMovement ? VehicleMovement->GetNumWheels() : 0;
	Health = HealthMax;
	if (TrackPath_L && GetMesh())
	{
		TrackPath_L->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform);
	}

	if (AimTimeline)
	{
		FOnTimelineFloat AimProgressUpdate;
		AimProgressUpdate.BindUFunction(this, FName("OnAimTimelineUpdate"));
		if (AimCurve)
		{
			AimTimeline->AddInterpFloat(AimCurve, AimProgressUpdate, FName("AimingPhase"));
		}
		else
		{
			UCurveFloat* DefaultAimCurve = NewObject<UCurveFloat>(this, TEXT("DefaultAimCurve"));
			DefaultAimCurve->FloatCurve.AddKey(0.0f, 0.0f);
			DefaultAimCurve->FloatCurve.AddKey(0.2f, 1.0f);
			AimTimeline->AddInterpFloat(DefaultAimCurve, AimProgressUpdate, FName("AimingPhase"));
		}
		AimTimeline->SetLooping(false);
	}

	// --- Sequence branch 2: geometric vs UV tracks visibility
	if (UseGeometricTracks)
	{
		// Show=false hides the UV-mapped track material sections while geometric (instanced-mesh) tracks are active.
		// Exact boolean polarity is an assumption - verify against the original BP wiring.
		ShowUVTracks(false);
	}
	else
	{
		for (UInstancedStaticMeshComponent* Inst : TracksInstances_R)
		{
			if (Inst) { Inst->SetVisibility(false); }
		}
		for (UInstancedStaticMeshComponent* Inst : TracksInstances_L)
		{
			if (Inst) { Inst->SetVisibility(false); }
		}
	}

	// --- Sequence branch 3: Enhanced Input mapping context
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				FModifyContextOptions Options;
				Options.bIgnoreAllPressedKeysUntilRelease = true;
				Subsystem->AddMappingContext(DefaultMappingContext, 0, Options);
			}
		}
	}
}
void ABP_TankController_Chaos_CPP::DeferredBeginPlaySetup()
{
	CameraAutoSetting();
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		SkelMesh->SetAllBodiesPhysicsBlendWeight(0.0f);
	}
	FilletsCompensation = TrackThickness / 2.0;
	SplineFilletsCompensation(TrackPath_R);
	SplineFilletsCompensation(TrackPath_L);
	SplinePointsParametersDefinition();
	PhysWheelsAmount = VehicleMovement ? VehicleMovement->GetNumWheels() : 0; // Robust re-definition (works for bots too, unlike the OnConstruction one)
	Health = HealthMax;
	if (TrackPath_L && GetMesh())
	{
		TrackPath_L->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform);
	}
}
void ABP_TankController_Chaos_CPP::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!IsVehicleTaken)
	{
		return;
	}

	DeltaSeconds = DeltaTime;
	ForwardSpeedMPH = VehicleMovement ? VehicleMovement->GetForwardSpeed() * 0.0056818f : 0.f; // cm/s -> mph approximation, matches BP's speed unit conversion node chain

	// then_0
	ChassisDistanceDefinition();
	SaggingDegreeR = SaggingDegreeR; // computed inside ChassisDistanceDefinition/SaggingCalculation (sibling) - kept as no-op placeholder assignment for 1:1 node count
	SaggingDegreeL = SaggingDegreeL;

	// then_1
	if (!UseGeometricTracks)
	{
		UpdateTracksMID(RightTrackMID, ChassisDistanceR);
		UpdateTracksMID(LeftTrackMID, ChassisDistanceL);
		TrackPathAnimations(TrackPath_R, SaggingDegreeR, ChassisLockedR, false);
		TrackPathAnimations(TrackPath_L, SaggingDegreeL, ChassisLockedL, true);
	}

	// then_2
	if (UseGeometricTracks)
	{
		TrackPathShift(TrackPath_R, false);
		TrackPathShift(TrackPath_L, true);
		TrackPathAnimations(TrackPath_R, SaggingDegreeR, ChassisLockedR, false);
		TrackPathAnimations(TrackPath_L, SaggingDegreeL, ChassisLockedL, true);
		SetTracksTransform(TracksInstances_R, TrackPath_R, ChassisDistanceR);
		SetTracksTransform(TracksInstances_L, TrackPath_L, ChassisDistanceL);
	}

	// then_3
	UpdateHUD();
	UpdateSound();
	ReplicateControlRotation();
	UpdateCrosshairPositionAndSize();
	TracksDecal();
	HullAccelerationDefinition();
	AntennaCalculation(AntennaParameters);

	// then_4
	TurretsAndGunsRotCalculation();
	// Per-category, per-side wheel rotation matching Blueprint EventGraph wiring 1:1:
	// Front and Rear wheels receive WheelStartingAngleGeo/UV and WheelSpeedCorrectionUV;
	// Middle and Accessory wheels default starting angles and UV speed correction to 0.0.
	WheelRotFrontL = WheelRotationDefinition(ChassisDistanceL, WheelRadiusFront, TrackThickness, WheelSpeedCorrectionUV, WheelStartingAngleGeoL, WheelStartingAngleGeoR, WheelStartingAngleUVL, WheelStartingAngleUVR, true);
	WheelRotFrontR = WheelRotationDefinition(ChassisDistanceR, WheelRadiusFront, TrackThickness, WheelSpeedCorrectionUV, WheelStartingAngleGeoL, WheelStartingAngleGeoR, WheelStartingAngleUVL, WheelStartingAngleUVR, false);
	WheelRotMiddleL = WheelRotationDefinition(ChassisDistanceL, WheelRadiusMiddle, TrackThickness, 0.0, 0.0, 0.0, 0.0, 0.0, true);
	WheelRotMiddleR = WheelRotationDefinition(ChassisDistanceR, WheelRadiusMiddle, TrackThickness, 0.0, 0.0, 0.0, 0.0, 0.0, false);
	WheelRotRearL = WheelRotationDefinition(ChassisDistanceL, WheelRadiusRear, TrackThickness, WheelSpeedCorrectionUV, WheelStartingAngleGeoL, WheelStartingAngleGeoR, WheelStartingAngleUVL, WheelStartingAngleUVR, true);
	WheelRotRearR = WheelRotationDefinition(ChassisDistanceR, WheelRadiusRear, TrackThickness, WheelSpeedCorrectionUV, WheelStartingAngleGeoL, WheelStartingAngleGeoR, WheelStartingAngleUVL, WheelStartingAngleUVR, false);
	WheelRotAccessoryL = WheelRotationDefinition(ChassisDistanceL, WheelRadiusAccessory, TrackThickness, 0.0, 0.0, 0.0, 0.0, 0.0, true);
	WheelRotAccessoryR = WheelRotationDefinition(ChassisDistanceR, WheelRadiusAccessory, TrackThickness, 0.0, 0.0, 0.0, 0.0, 0.0, false);
	// For Each Loop over antenna array + world-location updates handled by sibling AntennaCalculation/attachment logic
	ScatteringCalculation();
}
float ABP_TankController_Chaos_CPP::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Result = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	Health = Health - DamageAmount;
	if (Health <= 0.0)
	{
		if (!bHasExploded) // Do Once
		{
			bHasExploded = true;
			Destroyed = true;
			SERVER_Explode();
			ClientDestroyed();
		}
	}

	return Result;
}
void ABP_TankController_Chaos_CPP::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// PoliceTurn (sibling macro/function) drives handbrake on Pressed/Released - re-applied here to fix chassis blocking after possessing
	PoliceTurn(true);
	if (VehicleMovement)
	{
		VehicleMovement->SetHandbrakeInput(true);
	}
	PoliceTurn(false);
	if (VehicleMovement)
	{
		VehicleMovement->SetHandbrakeInput(false);
	}

	// BeginPlay's Controller-gated setup (mapping context, HUD/Crosshair) runs before PossessedBy for
	// GameMode-spawned pawns (SpawnActor -> BeginPlay happens before the later Possess() call), so
	// GetController() is still null when BeginPlay's checks run and that setup is silently skipped.
	// Re-run it here, where NewController is guaranteed valid.
	if (APlayerController* PC = Cast<APlayerController>(NewController))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				FModifyContextOptions Options;
				Options.bIgnoreAllPressedKeysUntilRelease = true;
				Subsystem->AddMappingContext(DefaultMappingContext, 0, Options);
			}
		}

		if (!HUD)
		{
			PlayerController = PC;
			HUD = CreateWidget<UUserWidget>(PC, HUDClass);
			if (HUD)
			{
				HUD->AddToViewport();
				WeaponSlotsDisplay();
				UpdateChosenWeaponUI();
				UpdateChosenVehicleUI();
			}
			Crosshair = CreateWidget<UUserWidget>(PC, CrosshairClass);
			if (Crosshair)
			{
				Crosshair->AddToViewport();
			}
		}
	}
}
void ABP_TankController_Chaos_CPP::WeaponFire(int32 Index)
{
	// Switch on Int: only case 0 is wired - other weapon indices are no-ops (1:1 with the real graph)
	if (Index == 0)
	{
		if (MainGunSpringTimeline)
		{
			MainGunSpringTimeline->Play();
		}
	}
}
void ABP_TankController_Chaos_CPP::SERVER_Explode_Implementation()
{
	MC_Explode();
}

void ABP_TankController_Chaos_CPP::MC_Explode_Implementation()
{
	// then_0
	Destroyed = true;
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SpawnEmitterAtLocation(World, ExplosionEffect, GetActorLocation());
	}
	if (DestroyedFlames) { DestroyedFlames->Activate(); }
	UGameplayStatics::PlaySoundAtLocation(this, ExplosionSound, GetActorLocation());
	if (GetMesh()) { GetMesh()->SetVisibility(false, true); }
	if (Tank_Destroyed) { Tank_Destroyed->SetVisibility(true, true); }
	if (Decal) { Decal->SetVisibility(true); }
	if (DestroyedFlames) { DestroyedFlames->SetVisibility(true); }
	if (Tank_Destroyed) { Tank_Destroyed->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform); }
	UGameplayStatics::PlaySoundAtLocation(this, ExplosionSound2, GetActorLocation());
	if (TankEngine) { TankEngine->Play(); } // "Play Target is Audio Component" - approximated as TankEngine playing a destruction stinger
	if (GetMesh()) { GetMesh()->SetVisibility(false, true); }
	if (Tank_Destroyed) { Tank_Destroyed->SetVisibility(true, true); }

	// then_1
	if (DestroyedFlames && DestroyedFlamesTemplate) { DestroyedFlames->SetTemplate(DestroyedFlamesTemplate); }
	if (Tank_Destroyed) { Tank_Destroyed->SetSimulatePhysics(true); }
	ServerLights(false);
}

void ABP_TankController_Chaos_CPP::ClientDestroyed()
{
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->UnPossess();
		if (CameraShakeClass)
		{
			PC->ClientStartCameraShake(CameraShakeClass);
		}
		UWidgetLayoutLibrary::RemoveAllWidgets(this);
	}
}
void ABP_TankController_Chaos_CPP::UpdateSound()
{
	if (Destroyed)
	{
		if (TankEngine) { TankEngine->Stop(); }
		if (TurretMotor) { TurretMotor->Stop(); }
		return;
	}

	const double RPMRatio = VehicleMovement ? VehicleMovement->GetEngineRotationSpeed() / FMath::Max(VehicleMovement->GetEngineMaxRotationSpeed(), 1.0) : 0.0;
	if (TankEngine) { TankEngine->SetFloatParameter(FName("RPM"), RPMRatio); }
	CurentRPMRatio = RPMRatio;
	// Set Float Parameter on an FXSystem (Niagara) Component - target not fully traced, approximated as an engine-exhaust FX param

	if (ForwardSpeedMPH != 0.f)
	{
		if (TankEngine) { TankEngine->FadeIn(0.5f); }
	}
	else
	{
		if (TankEngine) { TankEngine->FadeOut(0.5f, 0.f); }
	}
}
void ABP_TankController_Chaos_CPP::TracksDecal()
{
	const float ForwardSpeed = VehicleMovement ? VehicleMovement->GetForwardSpeed() : 0.f;
	const bool bMoving = (ForwardSpeed > 0.0) || (ForwardSpeed < 0.0);
	const bool bCondition = IsValid(SlideBackRight) && IsValid(SlideBackLeft) && bMoving;
	if (bCondition)
	{
		const double Intensity = FMath::Abs(ForwardSpeed);
		ServerSlipCosmetics(SlideBackRight, true, Intensity);
		ServerSlipCosmetics(SlideBackLeft, true, Intensity);
	}
}
void ABP_TankController_Chaos_CPP::MulticastSlipCosmetics_Implementation(UParticleSystemComponent* Particle, bool bNewActive, double Intensity)
{
	if (Particle)
	{
		Particle->SetActive(bNewActive);
		Particle->SetFloatParameter(FName("Intensity"), Intensity);
	}
}

void ABP_TankController_Chaos_CPP::ServerSlipCosmetics_Implementation(UParticleSystemComponent* Particle, bool bNewActive, double Intensity)
{
	MulticastSlipCosmetics(Particle, bNewActive, Intensity);
}
void ABP_TankController_Chaos_CPP::SERVER_UpdateCamo_Implementation()
{
	MC_UpdateCamo();
}

void ABP_TankController_Chaos_CPP::MC_UpdateCamo_Implementation()
{
	// Cycle CamoCurrent: wrap to 0 once past the last CamoVariations entry
	const int32 LastIndex = CamoVariations.Num() - 1;
	CamoCurrent = (CamoCurrent == LastIndex) ? 0 : (CamoCurrent + 1);

	if (CamoVariations.IsValidIndex(CamoCurrent))
	{
		const FTSCamoOptions& Camo = CamoVariations[CamoCurrent];
		CamoName = Camo.CamoName;
		if (GetMesh())
		{
			for (int32 i = 0; i < Camo.MaterialInstance.Num(); ++i)
			{
				const int32 SlotId = Camo.MaterialSlotID.IsValidIndex(i) ? Camo.MaterialSlotID[i] : i;
				GetMesh()->SetMaterial(SlotId, Camo.MaterialInstance[i]);
			}
		}
		SetTrackDynamicMaterial();
	}
}
void ABP_TankController_Chaos_CPP::ServerLights_Implementation(bool bLightsOn)
{
	MulticastLights(bLightsOn);
}

void ABP_TankController_Chaos_CPP::MulticastLights_Implementation(bool bLightsOn)
{
	const float Intensity = bLightsOn ? LightIntensityLights : 0.0f;
	if (Light_R) { Light_R->SetIntensity(Intensity); }
	if (Light_L) { Light_L->SetIntensity(Intensity); }
	if (GetMesh())
	{
		GetMesh()->SetScalarParameterValueOnMaterials(FName("EmissiveIntensity"), bLightsOn ? EmissiveIntensityLights : 0.0f);
	}
}
void ABP_TankController_Chaos_CPP::SwitchPawn()
{
	PawnSwitching(PawnClassSelection);
}
void ABP_TankController_Chaos_CPP::Server_Healing_Implementation()
{
	MC_Healing();
}

void ABP_TankController_Chaos_CPP::MC_Healing_Implementation()
{
	Health = HealthMax;
}
void ABP_TankController_Chaos_CPP::UpdateUI_HP()
{
	UpdateHealthBar();
}
void ABP_TankController_Chaos_CPP::EventUpdateDamageUI(int32 Damage)
{
	UpdateDamageCausedUI(Damage);
}
void ABP_TankController_Chaos_CPP::WeaponReloadUI(int32 WeaponSlot)
{
	// Switch on Int 0..4 all follow the same pattern: poll ReloadWeaponUI(slot) until it reports Reloaded, retrying next tick
	if (WeaponSlot < 0 || WeaponSlot > 4)
	{
		return;
	}

	if (!ReloadWeaponUI(WeaponSlot)) // false == NotReloaded branch
	{
		if (UWorld* World = GetWorld())
		{
			FTimerHandle Unused;
			World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &ABP_TankController_Chaos_CPP::WeaponReloadUI, WeaponSlot));
		}
	}
}
void ABP_TankController_Chaos_CPP::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EIC->BindAction(IA_LookUpDown, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnLookUpDown);
		EIC->BindAction(IA_LookRightLeft, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnLookRightLeft);
		EIC->BindAction(IA_MoveRightLeft, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnMoveRightLeft);
		EIC->BindAction(IA_MoveRightLeft, ETriggerEvent::Completed, this, &ABP_TankController_Chaos_CPP::OnMoveRightLeftCompleted);
		EIC->BindAction(IA_MoveRightLeft, ETriggerEvent::Canceled, this, &ABP_TankController_Chaos_CPP::OnMoveRightLeftCompleted);
		EIC->BindAction(IA_MoveForwardBack, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnMoveForwardBack);
		EIC->BindAction(IA_MoveForwardBack, ETriggerEvent::Completed, this, &ABP_TankController_Chaos_CPP::OnMoveForwardBackCompleted);
		EIC->BindAction(IA_MoveForwardBack, ETriggerEvent::Canceled, this, &ABP_TankController_Chaos_CPP::OnMoveForwardBackCompleted);
		EIC->BindAction(IA_Handbrake, ETriggerEvent::Started, this, &ABP_TankController_Chaos_CPP::OnHandbrakePressed);
		EIC->BindAction(IA_Handbrake, ETriggerEvent::Completed, this, &ABP_TankController_Chaos_CPP::OnHandbrakeReleased);
		EIC->BindAction(IA_DebugView, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnDebugView);
		EIC->BindAction(IA_DestroySelf, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnDestroySelf);
		EIC->BindAction(IA_ToggleInterface, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnToggleInterface);
		EIC->BindAction(IA_BulletTime, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnBulletTime);
		EIC->BindAction(IA_UseWeapon1, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnUseWeapon1);
		EIC->BindAction(IA_UseWeapon2, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnUseWeapon2);
		EIC->BindAction(IA_UseWeapon3, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnUseWeapon3);
		EIC->BindAction(IA_UseWeapon4, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnUseWeapon4);
		EIC->BindAction(IA_UseWeapon5, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnUseWeapon5);
		EIC->BindAction(IA_FirePrimaryWeapon, ETriggerEvent::Started, this, &ABP_TankController_Chaos_CPP::OnFirePrimaryWeaponStarted);
		EIC->BindAction(IA_FirePrimaryWeapon, ETriggerEvent::Completed, this, &ABP_TankController_Chaos_CPP::OnFirePrimaryWeaponCompleted);
		EIC->BindAction(IA_Reload, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnReload);
		EIC->BindAction(IA_Aim, ETriggerEvent::Started, this, &ABP_TankController_Chaos_CPP::OnAimStarted);
		EIC->BindAction(IA_Aim, ETriggerEvent::Completed, this, &ABP_TankController_Chaos_CPP::OnAimCompleted);
		EIC->BindAction(IA_SwitchCamo, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnSwitchCamo);
		EIC->BindAction(IA_ToggleLights, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnToggleLights);
		EIC->BindAction(IA_TurretBlocking, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnTurretBlockingToggle);
		EIC->BindAction(IA_Stabilizer, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnStabilizerToggle);
		EIC->BindAction(IA_Healing, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnHealingRequested);
		EIC->BindAction(IA_SniperModeToggle, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnSniperModeToggle);
		EIC->BindAction(IA_Zoom, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnZoom);
		EIC->BindAction(IA_ShowHotkeys, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnShowHotkeys);
		EIC->BindAction(IA_GeoTracksSwitching, ETriggerEvent::Triggered, this, &ABP_TankController_Chaos_CPP::OnGeoTracksSwitching);
	}
}
void ABP_TankController_Chaos_CPP::OnLookUpDown(const FInputActionValue& Value)
{
	// CameraPitchLimit (sibling function) re-scales the raw axis before it's applied
	const double Clamped = CameraPitchLimit(Value.Get<float>());
	AddControllerPitchInput(Clamped);
}

void ABP_TankController_Chaos_CPP::OnLookRightLeft(const FInputActionValue& Value)
{
	AddControllerYawInput(Value.Get<float>());
}

void ABP_TankController_Chaos_CPP::OnMoveRightLeft(const FInputActionValue& Value)
{
	MoveRightAxis = Value.Get<float>();
	// Followed by a call using the new MoveRightAxis (TurningControl, sibling function)
	TurningControl();
}

void ABP_TankController_Chaos_CPP::OnMoveRightLeftCompleted(const FInputActionValue& Value)
{
	MoveRightAxis = 0.0f;
	TurningControl();
}

void ABP_TankController_Chaos_CPP::OnMoveForwardBack(const FInputActionValue& Value)
{
	ThrottleControl(Value.Get<float>());
}

void ABP_TankController_Chaos_CPP::OnMoveForwardBackCompleted(const FInputActionValue& Value)
{
	ThrottleControl(0.0f);
}

void ABP_TankController_Chaos_CPP::OnHandbrakePressed(const FInputActionValue& Value)
{
	PoliceTurn(true);
	if (VehicleMovement)
	{
		VehicleMovement->SetHandbrakeInput(true);
	}
}

void ABP_TankController_Chaos_CPP::OnHandbrakeReleased(const FInputActionValue& Value)
{
	PoliceTurn(false);
	if (VehicleMovement)
	{
		VehicleMovement->SetHandbrakeInput(false);
	}
}
namespace
{
	// BP_TankWeapon_C stays Blueprint-only; call its functions on BPC_TankWeapon via reflection
	void CallWeaponFunction(UActorComponent* WeaponComp, FName FunctionName, void* Params = nullptr)
	{
		if (!WeaponComp) return;
		if (UFunction* Func = WeaponComp->FindFunction(FunctionName))
		{
			WeaponComp->ProcessEvent(Func, Params);
		}
	}
}

void ABP_TankController_Chaos_CPP::OnUseWeapon1(const FInputActionValue& Value)
{
	struct { int32 WeaponIndex; } Params{ 0 };
	CallWeaponFunction(BPC_TankWeapon, FName("SERVER_ChangeWeapon"), &Params);
}

void ABP_TankController_Chaos_CPP::OnUseWeapon2(const FInputActionValue& Value)
{
	struct { int32 WeaponIndex; } Params{ 1 };
	CallWeaponFunction(BPC_TankWeapon, FName("SERVER_ChangeWeapon"), &Params);
}

void ABP_TankController_Chaos_CPP::OnUseWeapon3(const FInputActionValue& Value)
{
	struct { int32 WeaponIndex; } Params{ 2 };
	CallWeaponFunction(BPC_TankWeapon, FName("SERVER_ChangeWeapon"), &Params);
}

void ABP_TankController_Chaos_CPP::OnUseWeapon4(const FInputActionValue& Value)
{
	struct { int32 WeaponIndex; } Params{ 3 };
	CallWeaponFunction(BPC_TankWeapon, FName("SERVER_ChangeWeapon"), &Params);
}

void ABP_TankController_Chaos_CPP::OnUseWeapon5(const FInputActionValue& Value)
{
	struct { int32 WeaponIndex; } Params{ 4 };
	CallWeaponFunction(BPC_TankWeapon, FName("SERVER_ChangeWeapon"), &Params);
}

void ABP_TankController_Chaos_CPP::OnFirePrimaryWeaponStarted(const FInputActionValue& Value)
{
	CallWeaponFunction(BPC_TankWeapon, FName("StartShooting"));
}

void ABP_TankController_Chaos_CPP::OnFirePrimaryWeaponCompleted(const FInputActionValue& Value)
{
	CallWeaponFunction(BPC_TankWeapon, FName("StopShooting"));
}

void ABP_TankController_Chaos_CPP::OnReload(const FInputActionValue& Value)
{
	int32 CurrentWeaponIndex = 0;
	if (FIntProperty* IdxProp = BPC_TankWeapon ? FindFProperty<FIntProperty>(BPC_TankWeapon->GetClass(), TEXT("Current WeaponIndex")) : nullptr)
	{
		CurrentWeaponIndex = IdxProp->GetPropertyValue_InContainer(BPC_TankWeapon);
	}
	struct { int32 Weapon; } Params{ CurrentWeaponIndex };
	CallWeaponFunction(BPC_TankWeapon, FName("ReloadWeapon"), &Params);
}
void ABP_TankController_Chaos_CPP::OnDebugView(const FInputActionValue& Value)
{
	// Flip Flop: toggles a debug console command each press
	bDebugViewFlipFlop = !bDebugViewFlipFlop;
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		UKismetSystemLibrary::ExecuteConsoleCommand(this, bDebugViewFlipFlop ? TEXT("showdebug vehicle") : TEXT("showdebug"), PC);
	}
}

void ABP_TankController_Chaos_CPP::OnDestroySelf(const FInputActionValue& Value)
{
	SERVER_Explode();
}

void ABP_TankController_Chaos_CPP::OnToggleInterface(const FInputActionValue& Value)
{
	bInterfaceFlipFlop = !bInterfaceFlipFlop;
	if (HUD)
	{
		HUD->SetVisibility(bInterfaceFlipFlop ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
}

void ABP_TankController_Chaos_CPP::OnBulletTime(const FInputActionValue& Value)
{
	// Only meaningful while Sniper Mode is active; global time dilation not owned by any ported graph
	// function (BulletTime's real node wiring wasn't traced) - best-effort direct implementation.
	if (SniperMode)
	{
		UGameplayStatics::SetGlobalTimeDilation(this, 0.2f);
	}
}

void ABP_TankController_Chaos_CPP::OnAimTimelineUpdate(float AimingPhase)
{
	if (SpringArmArcade)
	{
		SpringArmArcade->TargetArmLength = UKismetMathLibrary::FInterpEaseInOut(TargetArmLengthMin, 0.0, AimingPhase, 1.0f);
		SpringArmArcade->SocketOffset = StartSocketOffsetArcade + (ArcadeAimCameraOffset * AimingPhase);
	}
}

void ABP_TankController_Chaos_CPP::OnAimStarted(const FInputActionValue& Value)
{
	if (!SniperMode && AimTimeline)
	{
		AimTimeline->Play();
	}
}

void ABP_TankController_Chaos_CPP::OnAimCompleted(const FInputActionValue& Value)
{
	if (!SniperMode && AimTimeline)
	{
		AimTimeline->Reverse();
	}
}

void ABP_TankController_Chaos_CPP::OnSwitchCamo(const FInputActionValue& Value)
{
	SERVER_UpdateCamo();
}

void ABP_TankController_Chaos_CPP::OnToggleLights(const FInputActionValue& Value)
{
	bLightsFlipFlop = !bLightsFlipFlop;
	ServerLights(bLightsFlipFlop);
}

void ABP_TankController_Chaos_CPP::OnTurretBlockingToggle(const FInputActionValue& Value)
{
	TurretBlocking = !TurretBlocking;
}

void ABP_TankController_Chaos_CPP::OnStabilizerToggle(const FInputActionValue& Value)
{
	Stabilization = !Stabilization;
	StabilizationWasSwitched = true;
}

void ABP_TankController_Chaos_CPP::OnHealingRequested(const FInputActionValue& Value)
{
	Server_Healing();
}

void ABP_TankController_Chaos_CPP::OnSniperModeToggle(const FInputActionValue& Value)
{
	SniperModeToggle();
}

void ABP_TankController_Chaos_CPP::OnZoom(const FInputActionValue& Value)
{
	const float RawValue = Value.Get<float>();
	if (RawValue > 0.f)
	{
		ZoomIn();
	}
	else
	{
		ZoomOut();
	}
}

void ABP_TankController_Chaos_CPP::OnShowHotkeys(const FInputActionValue& Value)
{
	bHotkeysFlipFlop = !bHotkeysFlipFlop;
	if (UWidget* HotkeysBox = HUD ? Cast<UWidget>(HUD->GetWidgetFromName(FName("VerticalBoxHotkeys"))) : nullptr)
	{
		HotkeysBox->SetVisibility(bHotkeysFlipFlop ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
}

void ABP_TankController_Chaos_CPP::OnGeoTracksSwitching(const FInputActionValue& Value)
{
	UseGeometricTracks = !UseGeometricTracks;
	// Followed by a Sequence of further track-remesh calls in sibling cluster code
}
void ABP_TankController_Chaos_CPP::OnDebugAdjustWheelRadiusRear(bool bPressed)
{
	// Num5 key: Branch on UseGeometricTracks selects which debug value gets nudged
	if (UseGeometricTracks)
	{
		WheelRadiusRear += 1.0; // exact delta/formula not traced - placeholder nudge, flagged for review
	}
	else
	{
		WheelSpeedCorrectionUV += 1.0;
	}
}

void ABP_TankController_Chaos_CPP::OnDebugToggleDebugCamera()
{
	// 'C' key: Toggle Active on the debug Camera component
	if (DebugCamera)
	{
		DebugCamera->ToggleActive();
	}
}

// ============================================================
// Track & wheel spline animation (Chassis)
// ============================================================
void ABP_TankController_Chaos_CPP::ChassisDistanceDefinition()
{
	// Chassis distance Z rotation component
	const double MeshYaw = GetMesh()->GetComponentRotation().Yaw;
	HullDeltaZRot = HullZRot - MeshYaw;
	HullZRot = MeshYaw;

	if (!ChassisLockedR)
	{
		ChassisDistanceZRotComponentR -= HullDeltaZRot * ProportionalCoefficient;
	}
	if (!ChassisLockedL)
	{
		ChassisDistanceZRotComponentL -= HullDeltaZRot * ProportionalCoefficient;
	}

	// Chassis distance X movement component
	UChaosVehicleMovementComponent* MovementComp = GetVehicleMovementComponent();
	HullDeltaXLocation = MovementComp ? MovementComp->GetForwardSpeed() * DeltaSeconds : 0.0;
	if (!ChassisLockedR)
	{
		ChassisDistanceXMoveComponentR += HullDeltaXLocation;
	}
	if (!ChassisLockedL)
	{
		ChassisDistanceXMoveComponentL += HullDeltaXLocation;
	}

	// Chassis acceleration definition (uses last frame's ChassisDeltaDistance as "old" speed)
	const double TracksSpeedNewR = (ChassisDistanceXMoveComponentR - ChassisDistanceZRotComponentR) - ChassisDistanceR;
	ChassisAccelerationR = TracksSpeedNewR - ChassisDeltaDistanceR;
	const double TracksSpeedNewL = (ChassisDistanceXMoveComponentL + ChassisDistanceZRotComponentL) - ChassisDistanceL;
	ChassisAccelerationL = TracksSpeedNewL - ChassisDeltaDistanceL;

	// Chassis distance R and L definition - order matters, delta uses the previous frame's distance
	ChassisDeltaDistanceR = (ChassisDistanceXMoveComponentR - ChassisDistanceZRotComponentR) - ChassisDistanceR;
	ChassisDistanceR = ChassisDistanceXMoveComponentR - ChassisDistanceZRotComponentR;
	ChassisDeltaDistanceL = (ChassisDistanceXMoveComponentL + ChassisDistanceZRotComponentL) - ChassisDistanceL;
	ChassisDistanceL = ChassisDistanceXMoveComponentL + ChassisDistanceZRotComponentL;

	// Bug fix: periodically reset accumulated distance to zero at high speed to avoid float precision oscillation
	const double ForwardSpeed = MovementComp ? MovementComp->GetForwardSpeed() : 0.0;
	if (FMath::Abs(ForwardSpeed) > 300.0 && TrackPath_R && FMath::Abs(ChassisDistanceR) > TrackPath_R->GetSplineLength())
	{
		ChassisDistanceZRotComponentR = 0.0;
		ChassisDistanceXMoveComponentR = 0.0;
		ChassisDistanceZRotComponentL = 0.0;
		ChassisDistanceXMoveComponentL = 0.0;
		ChassisDistanceR = 0.0;
		ChassisDistanceL = 0.0;
	}
}
void ABP_TankController_Chaos_CPP::UpdateTracksMID(UMaterialInstanceDynamic* MaterialInstance, double ChassisDistance)
{
	if (!IsValid(MaterialInstance))
	{
		return;
	}

	const double Frac = FMath::Fmod(ChassisDistance / TilingSegmentLength, 1.0);
	const double Offset = InvertTrackDirection ? (Frac * -1.0) : Frac;
	MaterialInstance->SetScalarParameterValue(TEXT("OffsetV"), static_cast<float>(Offset));
}
USplineComponent* ABP_TankController_Chaos_CPP::SplineMirrorCopy(USplineComponent* SplineToCopy)
{
	if (!SplineToCopy)
	{
		return nullptr;
	}

	const FTransform RelTransform = SplineToCopy->GetRelativeTransform();
	USplineComponent* NewSplineLocal = NewObject<USplineComponent>(this, USplineComponent::StaticClass());
	NewSplineLocal->SetupAttachment(GetRootComponent());
	NewSplineLocal->RegisterComponent();
	NewSplineLocal->SetRelativeLocation(FVector(RelTransform.GetLocation().X, RelTransform.GetLocation().Y * -1.0, RelTransform.GetLocation().Z));
	NewSplineLocal->SetRelativeRotation(RelTransform.GetRotation());
	NewSplineLocal->SetRelativeScale3D(RelTransform.GetScale3D());
	NewSplineLocal->SetClosedLoop(true);

	const double FirstPointZLocation = SplineToCopy->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::Local).Z;

	const int32 SourceNumPoints = SplineToCopy->GetNumberOfSplinePoints();
	for (int32 i = 0; i <= SourceNumPoints - 3; ++i)
	{
		NewSplineLocal->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local, true);
	}

	const int32 NewNumPoints = NewSplineLocal->GetNumberOfSplinePoints();
	for (int32 CurrentPointIndex = 0; CurrentPointIndex <= NewNumPoints - 1; ++CurrentPointIndex)
	{
		const bool bBottomFrontSagPoint = (SourceNumPoints - 1 == CurrentPointIndex);

		const double PrevZ = SplineToCopy->GetLocationAtSplinePoint(CurrentPointIndex - 1, ESplineCoordinateSpace::Local).Z;
		const double CurZ = SplineToCopy->GetLocationAtSplinePoint(CurrentPointIndex, ESplineCoordinateSpace::Local).Z;
		const bool bBottomRearSagPoint = !(FMath::Abs(CurZ - FirstPointZLocation) < 2.0) && (FMath::Abs(PrevZ - FirstPointZLocation) < 2.0);

		const FTransform SrcTransform = SplineToCopy->GetTransformAtSplinePoint(CurrentPointIndex, ESplineCoordinateSpace::Local, false);
		const FVector SrcLoc = SrcTransform.GetLocation();
		const bool bIsWheelContactPoint = FMath::Abs(SrcLoc.Z - FirstPointZLocation) < 2.0;
		const double BaseX = bIsWheelContactPoint ? (MiddleWheelXOffset + SrcLoc.X) : SrcLoc.X;
		const double SagOffsetX = (bBottomFrontSagPoint || bBottomRearSagPoint) ? (MiddleWheelXOffset / 2.0) : 0.0;

		const FVector NewLocation(BaseX + SagOffsetX, SrcLoc.Y, SrcLoc.Z);
		NewSplineLocal->SetLocationAtSplinePoint(CurrentPointIndex, NewLocation, ESplineCoordinateSpace::Local, true);
		NewSplineLocal->SetRotationAtSplinePoint(CurrentPointIndex, SrcTransform.Rotator(), ESplineCoordinateSpace::Local, true);

		const double TangentCorrection = bBottomRearSagPoint ? RearSagTangent : (bBottomFrontSagPoint ? FrontSagTangent : 0.0);
		FVector ArriveTangent = SplineToCopy->GetArriveTangentAtSplinePoint(CurrentPointIndex, ESplineCoordinateSpace::Local);
		FVector LeaveTangent = SplineToCopy->GetLeaveTangentAtSplinePoint(CurrentPointIndex, ESplineCoordinateSpace::Local);
		ArriveTangent.X += TangentCorrection;
		LeaveTangent.X += TangentCorrection;
		NewSplineLocal->SetTangentsAtSplinePoint(CurrentPointIndex, ArriveTangent, LeaveTangent, ESplineCoordinateSpace::Local, true);
	}

	return NewSplineLocal;
}
TArray<UInstancedStaticMeshComponent*> ABP_TankController_Chaos_CPP::InstanceTracksCreation(USplineComponent* TrackPathToAttach, bool TracksFlipY)
{
	TArray<UInstancedStaticMeshComponent*> TrackInstancesLocal;

	int32 UniqueTrackMeshesAmountLocal = 0;
	for (int32 i = 0; i < TrackStaticMeshes.Num(); ++i)
	{
		if (IsValid(TrackStaticMeshes[i]))
		{
			UniqueTrackMeshesAmountLocal = i + 1;
		}
	}
	UniqueTrackMeshesAmount = UniqueTrackMeshesAmountLocal;

	const double ScaleY = TracksFlipY ? -1.0 : 1.0;
	const int32 InstancesToAddPerMesh = UniqueTrackMeshesAmount > 0 ? (TracksAmount / UniqueTrackMeshesAmount) : 0;

	for (UStaticMesh* CurrentStaticMesh : TrackStaticMeshes)
	{
		if (!IsValid(CurrentStaticMesh))
		{
			continue;
		}

		UInstancedStaticMeshComponent* CurrentInstance = NewObject<UInstancedStaticMeshComponent>(this, UInstancedStaticMeshComponent::StaticClass());
		CurrentInstance->SetupAttachment(GetRootComponent());
		CurrentInstance->RegisterComponent();
		CurrentInstance->SetRelativeScale3D(FVector(1.0, ScaleY, 1.0));

		TrackInstancesLocal.Add(CurrentInstance);

		CurrentInstance->SetStaticMesh(CurrentStaticMesh);
		CurrentInstance->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (TrackPathToAttach)
		{
			CurrentInstance->AttachToComponent(TrackPathToAttach, FAttachmentTransformRules::KeepRelativeTransform);
		}

		for (int32 i = 0; i < InstancesToAddPerMesh; ++i)
		{
			CurrentInstance->AddInstance(FTransform::Identity, false);
		}
	}

	return TrackInstancesLocal;
}
void ABP_TankController_Chaos_CPP::SetTracksTransform(const TArray<UInstancedStaticMeshComponent*>& TrackInstances, USplineComponent* TrackPath, double ChassisDistance)
{
	if (!TrackPath)
	{
		return;
	}

	const double TrackPathLength = TrackPath->GetSplineLength();
	const int32 TracksPerMesh = UniqueTrackMeshesAmount > 0 ? (TracksAmount / UniqueTrackMeshesAmount) : 0;
	const double IntervalAmongTracks = TracksPerMesh > 0 ? (TrackPathLength / static_cast<double>(TracksPerMesh)) : 0.0;

	for (int32 ArrayIndex = 0; ArrayIndex < TrackInstances.Num(); ++ArrayIndex)
	{
		UInstancedStaticMeshComponent* TrackInstanceCurrent = TrackInstances[ArrayIndex];
		if (!TrackInstanceCurrent)
		{
			continue;
		}

		const double StartDistance = ChassisDistance + (UniqueTrackMeshesAmount > 0 ? (IntervalAmongTracks / UniqueTrackMeshesAmount) : 0.0) * ArrayIndex;
		const double ChassisCurrentDistance = TrackPathLength != 0.0 ? FMath::Fmod(FMath::Fmod(StartDistance, TrackPathLength) + TrackPathLength, TrackPathLength) : 0.0;
		const int32 TracksAmountBeforeSplineEnd = IntervalAmongTracks != 0.0 ? static_cast<int32>((TrackPathLength - ChassisCurrentDistance) / IntervalAmongTracks) : 0;

		auto UpdateOne = [&](int32 Index, double Distance)
		{
			const FTransform SplineTransform = TrackPath->GetTransformAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::Local, false);
			const FVector Right = TrackPath->GetRightVectorAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::Local);
			const FVector Location = SplineTransform.GetLocation() + Right * FilletsCompensation;
			TrackInstanceCurrent->UpdateInstanceTransform(Index, FTransform(SplineTransform.Rotator(), Location, FVector::OneVector), false, true, false);
		};

		for (int32 Index = 0; Index <= TracksAmountBeforeSplineEnd; ++Index)
		{
			UpdateOne(Index, Index * IntervalAmongTracks + ChassisCurrentDistance);
		}

		const int32 InstanceCount = TrackInstanceCurrent->GetInstanceCount();
		if ((TracksAmountBeforeSplineEnd + 1) <= (InstanceCount - 1))
		{
			for (int32 Index = TracksAmountBeforeSplineEnd + 1; Index <= InstanceCount - 1; ++Index)
			{
				UpdateOne(Index, Index * IntervalAmongTracks + (ChassisCurrentDistance - TrackPathLength));
			}
		}
	}
}
void ABP_TankController_Chaos_CPP::TrackPathShift(USplineComponent* TrackPath, bool LeftSide)
{
	UChaosWheeledVehicleMovementComponent* WheeledMovementComp = Cast<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent());
	if (!TrackPath || !WheeledMovementComp)
	{
		return;
	}

	const int32 ArrayHalfLength = WheeledMovementComp->Wheels.Num() / 2;
	for (int32 Index = 0; Index <= ArrayHalfLength - 1; ++Index)
	{
		const int32 WheelIndex = LeftSide ? (Index + ArrayHalfLength) : Index;
		if (!WheeledMovementComp->Wheels.IsValidIndex(WheelIndex))
		{
			continue;
		}

		UChaosVehicleWheel* Wheel = WheeledMovementComp->Wheels[WheelIndex];
		const FVector Loc = TrackPath->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local);
		const double Z = (Wheel ? Wheel->GetSuspensionOffset() : 0.0) + TrackThickness + (FilletsCompensation * -1.0);
		TrackPath->SetLocationAtSplinePoint(Index, FVector(Loc.X, Loc.Y, Z), ESplineCoordinateSpace::Local, true);
	}
}
void ABP_TankController_Chaos_CPP::TrackPathAnimations(USplineComponent* TrackPath, double SaggingDegree, bool ChassisLocked, bool LeftSide)
{
	UChaosVehicleMovementComponent* MovementComp = GetVehicleMovementComponent();
	const bool bHandbrake = MovementComp ? MovementComp->GetHandbrakeInput() : false;
	const double WorldDeltaSeconds = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0;

	// Amplitude multiplier (<1) right - speed influence + acceleration influence (acceleration ignored while braking, to avoid twitching)
	{
		const double SpeedTerm = FMath::Clamp(FMath::Abs(ChassisDeltaDistanceR * SpeedInfluence), 0.0, MaxSpeedInfluence);
		const double AccelTermRaw = FMath::Clamp(FMath::Abs(ChassisAccelerationR * AccelerationInfluence), 0.0, MaxAccelerationInfluence);
		const double AccelTerm = bHandbrake ? 0.0 : AccelTermRaw;
		const double AmplitudeMultiplierTargetR = SpeedTerm + AccelTerm;
		const double Current = (AmplitudeMultiplierTargetR > CurrentAmplitudeMultiplierR) ? AmplitudeMultiplierTargetR : CurrentAmplitudeMultiplierR;
		CurrentAmplitudeMultiplierR = FMath::FInterpConstantTo(Current, AmplitudeMultiplierTargetR, WorldDeltaSeconds, DecayRate);
	}

	// Amplitude multiplier (<1) left
	{
		const double SpeedTerm = FMath::Clamp(FMath::Abs(ChassisDeltaDistanceL * SpeedInfluence), 0.0, MaxSpeedInfluence);
		const double AccelTermRaw = FMath::Clamp(FMath::Abs(ChassisAccelerationL * AccelerationInfluence), 0.0, MaxAccelerationInfluence);
		const double AccelTerm = bHandbrake ? 0.0 : AccelTermRaw;
		const double AmplitudeMultiplierTargetL = SpeedTerm + AccelTerm;
		const double Current = (AmplitudeMultiplierTargetL > CurrentAmplitudeMultiplierL) ? AmplitudeMultiplierTargetL : CurrentAmplitudeMultiplierL;
		CurrentAmplitudeMultiplierL = FMath::FInterpConstantTo(Current, AmplitudeMultiplierTargetL, WorldDeltaSeconds, DecayRate);
	}

	// Apply per-point spline animation (sagging + vibration + wheel interaction)
	for (int32 ArrayIndex = 0; ArrayIndex < TankSplineAnim.Num(); ++ArrayIndex)
	{
		const FTSTankSplineAnim& Elem = TankSplineAnim[ArrayIndex];
		const FVector FinalPointLocation = PointLocationCalculation(Elem.AnimPointIndex, ChassisLocked, Elem.bInteractWithWheel, Elem.VibrationMaxAmplitude, Elem.VibrationPhase, Elem.SaggingForward, Elem.SaggingBack, SaggingDegree, ArrayIndex, TrackPath, LeftSide);
		if (TrackPath)
		{
			TrackPath->SetLocationAtSplinePoint(Elem.AnimPointIndex, FinalPointLocation, ESplineCoordinateSpace::Local, true);
		}
	}
}
void ABP_TankController_Chaos_CPP::SplinePointsParametersDefinition()
{
	for (int32 i = 0; i < TankSplineAnim.Num(); ++i)
	{
		const int32 CurrentSplinePointIndex = TankSplineAnim[i].AnimPointIndex;
		if (!TrackPath_R)
		{
			continue;
		}

		const FTransform PointTransform = TrackPath_R->GetTransformAtSplinePoint(CurrentSplinePointIndex, ESplineCoordinateSpace::Local, false);
		SplinePointLocation.Add(PointTransform.GetLocation());

		const FRotator PerpRotator = UKismetMathLibrary::ComposeRotators(PointTransform.Rotator(), FRotator(90.0, 0.0, 0.0));
		SplinePointPerpendicularVectors.Add(PerpRotator.Vector());

		if (TankSplineAnim[i].bInteractWithWheel)
		{
			CopyPointIndices.Add(FindSplineXClosestPoint(CurrentSplinePointIndex));
		}
		else
		{
			CopyPointIndices.Add(0);
		}
	}
}
FVector ABP_TankController_Chaos_CPP::PointLocationCalculation(int32 SplinePointIndex, bool ChassisLocked, bool InteractWithWheel, double VibrationMaxAmplitude, double VibrationPhase, double SaggingForward, double SaggingBack, double SaggingDegree, int32 ArrayIndex, USplineComponent* TrackPath, bool LeftSide)
{
	UChaosWheeledVehicleMovementComponent* WheeledMovementComp = Cast<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent());
	const int32 MiddleWheelsAmount = WheeledMovementComp ? (WheeledMovementComp->Wheels.Num() / 2) : 0;

	const FVector CurrentPointPerpendicularVector = SplinePointPerpendicularVectors.IsValidIndex(ArrayIndex) ? SplinePointPerpendicularVectors[ArrayIndex] : FVector::ZeroVector;

	const int32 NumSplinePoints = TrackPath ? TrackPath->GetNumberOfSplinePoints() : 0;
	const double MiddleOffsetX = (TrackPath == TrackPath_L) ? (MiddleWheelXOffset / 2.0) : 0.0;

	auto GetWheelOffset = [&](int32 WheelIndex) -> double
	{
		if (WheeledMovementComp && WheeledMovementComp->Wheels.IsValidIndex(WheelIndex) && WheeledMovementComp->Wheels[WheelIndex])
		{
			return WheeledMovementComp->Wheels[WheelIndex]->GetSuspensionOffset() / 2.0;
		}
		return 0.0;
	};

	// Offset correction near the middle (rear) wheel
	FVector C = FVector::ZeroVector;
	if (SplinePointIndex == MiddleWheelsAmount)
	{
		const int32 WheelIdxRear = LeftSide ? (MiddleWheelsAmount * 2 - 1) : (MiddleWheelsAmount - 1);
		C = FVector(MiddleOffsetX, 0.0, GetWheelOffset(WheelIdxRear));
	}

	// Offset correction near the last spline point (front wheel)
	FVector D = FVector::ZeroVector;
	if (SplinePointIndex == NumSplinePoints - 1)
	{
		const int32 WheelIdxFront = LeftSide ? 0 : MiddleWheelsAmount;
		D = FVector(MiddleOffsetX, 0.0, GetWheelOffset(WheelIdxFront));
	}

	const double SagAlpha = FMath::InterpEaseInOut(SaggingBack, SaggingForward, SaggingDegree, 1.0);
	const FVector BaseLocation = SplinePointLocation.IsValidIndex(ArrayIndex) ? SplinePointLocation[ArrayIndex] : FVector::ZeroVector;
	const FVector LocalSplinePointLocation = SagAlpha * CurrentPointPerpendicularVector + BaseLocation + C + D;

	// Minimum Z to prevent interpenetration with the wheel (only relevant if this point interacts with a wheel)
	const int32 CopyIndex = CopyPointIndices.IsValidIndex(ArrayIndex) ? CopyPointIndices[ArrayIndex] : 0;
	const double PointUnderThisZ = TrackPath ? TrackPath->GetLocationAtSplinePoint(CopyIndex, ESplineCoordinateSpace::Local).Z : 0.0;
	const double MinZCandidate = PointUnderThisZ + WheelRadiusMiddle * 2.0 + TrackThickness;
	const double MinZLocationLocal = InteractWithWheel ? MinZCandidate : 0.0;

	const double AmplitudeBeforeInteraction = (LeftSide ? CurrentAmplitudeMultiplierL : CurrentAmplitudeMultiplierR) * VibrationMaxAmplitude;

	// Reduce vibration amplitude as the point approaches the wheel
	const double Ratio = FMath::GetMappedRangeValueClamped(FVector2D(0.0, AmplitudeBeforeInteraction), FVector2D(InteractionAmplitudeMultiplier, 1.0), LocalSplinePointLocation.Z - MinZLocationLocal);
	const double VibAmplitude = Ratio * AmplitudeBeforeInteraction;
	const double CurrentVibrationOffset = VibrationCalculation(VibAmplitude, VibrationPhase);

	TArray<double>& VibrationOffsetArray = LeftSide ? VibrationOffset_L : VibrationOffset_R;
	if (VibrationOffsetArray.IsValidIndex(SplinePointIndex))
	{
		VibrationOffsetArray[SplinePointIndex] = CurrentVibrationOffset;
	}

	if (InteractWithWheel)
	{
		const double ClampedZ = FMath::Clamp(LocalSplinePointLocation.Z, MinZLocationLocal, 10000.0);
		return FVector(LocalSplinePointLocation.X, LocalSplinePointLocation.Y, ClampedZ) + CurrentVibrationOffset * CurrentPointPerpendicularVector;
	}

	return LocalSplinePointLocation + CurrentVibrationOffset * CurrentPointPerpendicularVector;
}
int32 ABP_TankController_Chaos_CPP::FindSplineXClosestPoint(int32 SplinePointindex)
{
	if (!TrackPath_R)
	{
		return 0;
	}

	const double CurrentPointXlocation = TrackPath_R->GetLocationAtSplinePoint(SplinePointindex, ESplineCoordinateSpace::Local).X;
	double MinimalDeltaXLocation = 0.0;
	int32 ClosestPointIndexCurrent = 0;

	const int32 NumPoints = TrackPath_R->GetNumberOfSplinePoints();
	for (int32 Index = 0; Index <= NumPoints - 1; ++Index)
	{
		if (Index == SplinePointindex)
		{
			continue;
		}

		const double PointX = TrackPath_R->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local).X;
		const double DeltaX = FMath::Abs(CurrentPointXlocation - PointX);

		if (DeltaX < MinimalDeltaXLocation || MinimalDeltaXLocation == 0.0)
		{
			MinimalDeltaXLocation = DeltaX;
			ClosestPointIndexCurrent = Index;
		}
	}

	return ClosestPointIndexCurrent;
}
void ABP_TankController_Chaos_CPP::SplineFilletsCompensation(USplineComponent* TrackPath)
{
	if (!TrackPath)
	{
		return;
	}

	const int32 NumPoints = TrackPath->GetNumberOfSplinePoints();
	for (int32 Index = 0; Index <= NumPoints - 2; ++Index)
	{
		const FVector Loc = TrackPath->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::Local);
		const FVector Right = TrackPath->GetRightVectorAtSplinePoint(Index, ESplineCoordinateSpace::Local);
		const FVector NewLoc = Loc + Right * (TrackThickness / -2.0);
		TrackPath->SetLocationAtSplinePoint(Index, NewLoc, ESplineCoordinateSpace::Local, true);
	}
}
void ABP_TankController_Chaos_CPP::ShowUVTracks(bool Show)
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
	{
		return;
	}

	const int32 NumLODs = MeshComp->GetNumLODs();
	for (int32 LODIndex = 0; LODIndex <= NumLODs; ++LODIndex)
	{
		const int32 MatIdxL = MeshComp->GetMaterialIndex(TEXT("Track_L"));
		MeshComp->ShowMaterialSection(MatIdxL, MatIdxL, Show, LODIndex);

		const int32 MatIdxR = MeshComp->GetMaterialIndex(TEXT("Track_R"));
		MeshComp->ShowMaterialSection(MatIdxR, MatIdxR, Show, LODIndex);
	}
}
double ABP_TankController_Chaos_CPP::WheelRotationDefinition(double Distance, double WheelRadius, double TrackThicknessParam, double WheelSpeedCorrectionUVParam, double WheelStartAngleLeftGeoTracks, double WheelStartAngleRightGeoTracks, double WheelStartAngleLeftUVTracks, double WheelStartAngleRightUVTracks, bool LeftWheel) const
{
	// Circumference
	const double Circumference = 2.0 * (WheelRadius + TrackThicknessParam + (UseGeometricTracks ? 0.0 : WheelSpeedCorrectionUVParam)) * PI;

	// WheelStartingAngle
	const double StartAngle = UseGeometricTracks
		? (LeftWheel ? WheelStartAngleLeftGeoTracks : WheelStartAngleRightGeoTracks)
		: (LeftWheel ? WheelStartAngleLeftUVTracks : WheelStartAngleRightUVTracks);

	return (-360.0 * (Distance / Circumference)) + StartAngle;
}
void ABP_TankController_Chaos_CPP::SetTrackDynamicMaterial()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
	{
		return;
	}

	const int32 LeftSlot = MeshComp->GetMaterialIndex(TEXT("Track_L"));
	LeftTrackMID = MeshComp->CreateDynamicMaterialInstance(LeftSlot, BaseTrackMaterial);

	const int32 RightSlot = MeshComp->GetMaterialIndex(TEXT("Track_R"));
	RightTrackMID = MeshComp->CreateDynamicMaterialInstance(RightSlot, BaseTrackMaterial);
}

// ============================================================
// Suspension / vibration / antenna physics + drive input
// ============================================================
double ABP_TankController_Chaos_CPP::SaggingCalculation(double SaggingDegree, double HullDeltaXLocationParam, double ChassisDeltaDistance, bool ChassisLocked) const
{
	// HullDeltaXLocationParam is used when braking (negated); otherwise use the raw chassis delta distance
	const double SelectedDistance = ChassisLocked ? (HullDeltaXLocationParam * -1.0) : ChassisDeltaDistance;
	const double SaggingDegreeNew = SaggingDegree + (SelectedDistance / SaggingMaxDistance);
	return FMath::Clamp(SaggingDegreeNew, 0.0, 1.0);
}
double ABP_TankController_Chaos_CPP::VibrationCalculation(double VibrationAmplitude, double VibrationPhase)
{
	const double TimeSeconds = GetWorld() ? UGameplayStatics::GetTimeSeconds(GetWorld()) : 0.0;
	const double Phase = VibrationPhase + (TimeSeconds * TrackFrequency);
	const double VibrationOffset = VibrationAmplitude * UKismetMathLibrary::DegSin(Phase);
	return VibrationOffset;
}
void ABP_TankController_Chaos_CPP::AntennaCalculation(const TArray<FTSAntennaParams>& InAntennaParameters)
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
	{
		return;
	}

	const FRotator TurretWorldRotationInverted = UKismetMathLibrary::ComposeRotators(
		MeshComp->GetSocketRotation(TEXT("turret")),
		MeshComp->GetComponentRotation() * -2.0f);

	const FVector TurretLinearVelocity = MeshComp->GetPhysicsLinearVelocity(TEXT("turret"));
	// Rotate Vector (>>): rotate the linear velocity into the inverted turret-local space
	const FVector RotatedTurretVelocity = TurretWorldRotationInverted.RotateVector(TurretLinearVelocity);

	// Inverted to reverse direction of rotation
	const FVector TurretAccelerationLocalInverted = (TurretSpeedLocalInverted - RotatedTurretVelocity) * FVector(-1.0, -1.0, -1.0);
	TurretSpeedLocalInverted = RotatedTurretVelocity;

	const double WorldDeltaSeconds = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0;

	const int32 Count = InAntennaParameters.Num();
	if (AntennaCurrentSpeed.Num() < Count || AntennaRotation.Num() < Count)
	{
		// Defensive: BP arrays are fixed-size (30) and pre-sized elsewhere; guard against mismatched sizes here.
		return;
	}

	for (int32 CurrentIndex = 0; CurrentIndex < Count; ++CurrentIndex)
	{
		const FTSAntennaParams& Params = InAntennaParameters[CurrentIndex];

		const FRotator DeltaRot = UKismetMathLibrary::NormalizedDeltaRotator(AntennaRotation[CurrentIndex], Params.EquilibriumRotation);
		const FVector DeltaRotAsVector(DeltaRot.Pitch, DeltaRot.Roll, DeltaRot.Yaw);
		const FVector ElasticForce = DeltaRotAsVector * FVector(-1.0, -1.0, -1.0) * FMath::Abs(Params.Stiffness);

		const FVector FrictionForce = AntennaCurrentSpeed[CurrentIndex] * FVector(-1.0, -1.0, -1.0) * FMath::Abs(Params.Damping);

		// Gravity, unrotated into the antenna's equilibrium-relative local space
		const FVector GravityVec(0.0, 0.0, GravityForce);
		const FRotator SocketRotTurret = MeshComp->GetSocketRotation(TEXT("turret"));
		const FRotator EquilibriumWorldRot = UKismetMathLibrary::ComposeRotators(SocketRotTurret, Params.EquilibriumRotation);
		const FVector UnrotatedGravity = EquilibriumWorldRot.UnrotateVector(GravityVec);
		const FVector GravityD(UnrotatedGravity.X * -1.0, UnrotatedGravity.Y, UnrotatedGravity.Z);

		const FVector CombinedForce = TurretAccelerationLocalInverted + ElasticForce + FrictionForce + GravityD;

		const FVector SpeedDelta = CombinedForce * WorldDeltaSeconds;
		const FVector NewSpeed = AntennaCurrentSpeed[CurrentIndex] + SpeedDelta;
		AntennaCurrentSpeed[CurrentIndex] = NewSpeed;

		const FVector RotDelta = AntennaCurrentSpeed[CurrentIndex] * WorldDeltaSeconds * FMath::Abs(Params.Frequency);
		const FRotator RotDeltaAsRotator(RotDelta.X, RotDelta.Y, 0.0);
		const FRotator ComposedRot = UKismetMathLibrary::ComposeRotators(AntennaRotation[CurrentIndex], RotDeltaAsRotator);

		FRotator NewAntennaRotation;
		NewAntennaRotation.Pitch = FMath::Clamp(ComposedRot.Pitch, -60.0, 60.0);
		NewAntennaRotation.Roll = FMath::Clamp(ComposedRot.Roll, -60.0, 60.0);
		NewAntennaRotation.Yaw = 0.0; // Item_Yaw pin was left unconnected in the BP (always 0)
		AntennaRotation[CurrentIndex] = NewAntennaRotation;
	}
}
void ABP_TankController_Chaos_CPP::HullAccelerationDefinition()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
	{
		return;
	}

	const FVector CurrentVelocity = MeshComp->GetPhysicsLinearVelocity(NAME_None);
	HullAccelerationWorldInverted = HullSpeedWorld - CurrentVelocity;
	HullSpeedWorld = CurrentVelocity;
}
void ABP_TankController_Chaos_CPP::TurningControl()
{
	UChaosWheeledVehicleMovementComponent* Movement = Cast<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent());
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!Movement || !MeshComp)
	{
		return;
	}

	const double AngularVelZ = FMath::Abs(MeshComp->GetPhysicsAngularVelocityInDegrees(NAME_None).Z);
	const double SteeringValue = SteeringCurve ? (SteeringCurve->GetFloatValue(ForwardSpeedMPH) * MaxTurningSpeed) : MaxTurningSpeed;
	const double PoliceTurnSum = SteeringValue + ForwardSpeedMPH; // Police turn fix
	const bool bHandbrake = Movement->GetHandbrakeInput();
	const double TurnSpeedLimit = bHandbrake ? PoliceTurnSum : SteeringValue;
	const bool bTurningSpeedOK = AngularVelZ < TurnSpeedLimit;
	const bool bHandbrakeOrMoving = !bHandbrake || (ForwardSpeedMPH > 5.0);

	// Bug fix: when speed is zero, physics is automatically frozen; it does not unfreeze on its own
	// if the tank turns left from a stop, so we wake the rigid bodies explicitly.
	if (bHandbrakeOrMoving && bTurningSpeedOK)
	{
		if (MoveRightAxis < 0.0)
		{
			MeshComp->WakeAllRigidBodies();
		}

		float Yaw;
		if (ForwardSpeedMPH < 0.0)
		{
			Yaw = static_cast<float>(MoveRightAxis * (ReverseTurnInReverse ? -1.0 : 1.0));
		}
		else
		{
			Yaw = static_cast<float>(MoveRightAxis);
		}
		Movement->SetYawInput(Yaw);

		// Physics bug fix: if the tank turns in place with zero throttle it experiences extra
		// resistance and turns slower, so a small throttle input is added when turning in place.
		const bool bStationaryTurn = (FMath::Abs(ForwardSpeedMPH) < 3.0) && (MoveRightAxis != 0.0);
		if (bStationaryTurn && FMath::IsNearlyZero(Movement->GetThrottleInput()))
		{
			Movement->SetThrottleInput(0.001f);
		}
		else if (!bStationaryTurn && FMath::IsNearlyEqual(Movement->GetThrottleInput(), 0.001f))
		{
			Movement->SetThrottleInput(0.0f);
		}
	}
	else
	{
		Movement->SetYawInput(0.0f);
	}
}
void ABP_TankController_Chaos_CPP::ThrottleControl(double ThrottleActionValue)
{
	if (!IsVehicleTaken)
	{
		return;
	}

	UChaosWheeledVehicleMovementComponent* Movement = Cast<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent());
	if (!Movement)
	{
		return;
	}

	// maximum speed limit (cm/sec to km/h)
	const double ForwardSpeedKmh = Movement->GetForwardSpeed() * 0.036;
	const double ThrottleInput = (ForwardSpeedKmh < MaxSpeedKMH) ? ThrottleActionValue : 0.0;
	Movement->SetThrottleInput(static_cast<float>(ThrottleInput));

	const bool bBrakingWhileReversing = (ThrottleInput > 0.0) && (ForwardSpeedMPH < -1.0);
	const bool bBrakingWhileForward = (ThrottleInput < 0.0) && (ForwardSpeedMPH > 1.0);
	
	double BrakeInput = 0.0;
	if (bBrakingWhileReversing)
	{
		BrakeInput = ThrottleInput;
	}
	else if (bBrakingWhileForward)
	{
		BrakeInput = ThrottleInput * -1.0;
	}
	
	Movement->SetBrakeInput(static_cast<float>(BrakeInput));

	// Automatic-transmission shift-out-of-Neutral isn't engaging on its own for this vehicle
	// (TargetGear stays 0 indefinitely even with sustained positive throttle), so drive it
	// explicitly here rather than relying on the engine's own auto-shift.
	if (ThrottleInput > 0.0 && Movement->GetCurrentGear() <= 0 && Movement->GetTargetGear() <= 0)
	{
		Movement->SetTargetGear(1, true);
	}
	else if (ThrottleInput < 0.0 && Movement->GetCurrentGear() >= 0 && Movement->GetTargetGear() >= 0)
	{
		Movement->SetTargetGear(-1, true);
	}

}
void ABP_TankController_Chaos_CPP::PoliceTurn(bool bPressed)
{
	UChaosWheeledVehicleMovementComponent* Movement = Cast<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent());
	if (!Movement)
	{
		return;
	}

	if (bPressed)
	{
		if (MoveRightAxis == 0.0)
		{
			ChassisLockedR = true;
			ChassisLockedL = true;
		}
		else if (MoveRightAxis > 0.0)
		{
			// Turning right: release handbrake influence on the second half of the wheels
			for (int32 WheelIndex = PhysWheelsAmount / 2; WheelIndex <= PhysWheelsAmount - 1; ++WheelIndex)
			{
				Movement->SetAffectedByHandbrake(WheelIndex, false);
			}
			ChassisLockedR = false;
			ChassisLockedL = true;
		}
		else
		{
			// Turning left: release handbrake influence on the first half of the wheels
			for (int32 WheelIndex = 0; WheelIndex <= (PhysWheelsAmount / 2) - 1; ++WheelIndex)
			{
				Movement->SetAffectedByHandbrake(WheelIndex, false);
			}
			ChassisLockedR = true;
			ChassisLockedL = false;
		}
	}
	else
	{
		// Return handbrake interaction to all wheels
		for (int32 WheelIndex = 0; WheelIndex <= PhysWheelsAmount - 1; ++WheelIndex)
		{
			Movement->SetAffectedByHandbrake(WheelIndex, true);
		}
		ChassisLockedR = false;
		ChassisLockedL = false;
	}
}

// ============================================================
// Turret / gun rotation + ballistics
// ============================================================
void ABP_TankController_Chaos_CPP::UpdateTurretRotation_Old()
{
	// --- Sequence pin 0: recalculate TurretRotation (world/hull relative) then interp toward target ---
	if (StabilizationWasSwitched)
	{
		if (Stabilization)
		{
			TurretRotation = UKismetMathLibrary::ComposeRotators(TurretRotation, GetActorRotation());
		}
		else
		{
			TurretRotation = UKismetMathLibrary::NormalizedDeltaRotator(TurretRotation, GetActorRotation());
		}
	}

	const FRotator InterpTarget = TurretBlocking ? TurretRotation : Rep_ControlRotation;
	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	TurretRotation = UKismetMathLibrary::RInterpTo_Constant(TurretRotation, InterpTarget, DeltaTime, TurretRotationSpeed);

	const bool bReachedOrBlocked = TurretRotation.Equals(Rep_ControlRotation, 0.0001f) || TurretBlocking;
	IsTurretRotating = !bReachedOrBlocked;

	// --- Sequence pin 1: legacy height-clipped TurretYaw/TurretPitch path ---
	if (Stabilization)
	{
		const FRotator DeltaRot = UKismetMathLibrary::NormalizedDeltaRotator(TurretRotation, GetActorRotation());
		TurretYaw = DeltaRot.Yaw;

		// Calculated pitch: angle between the turret's forward vector and the turret socket's up vector
		const FRotator TurretSocketRot = GetMesh()->GetSocketRotation(TEXT("turret"));
		const FVector TurretUp = UKismetMathLibrary::GetUpVector(TurretSocketRot);
		const FVector TurretForward = TurretRotation.Vector();
		const FVector Projected = UKismetMathLibrary::ProjectVectorOnToVector(TurretForward, TurretUp);
		const double Sign = Projected.Z > 0.0 ? 1.0 : -1.0;
		const double CalculatedPitch = UKismetMathLibrary::DegAsin(Projected.Size() * Sign);

		const bool bInClipRange = UKismetMathLibrary::InRange_FloatFloat(DeltaRot.Yaw, ClippingRangeMin, ClippingRangeMax, true, true);
		const double MinPitch = bInClipRange ? TurretHeightRangeClip : TurretVerticalRange.X;
		TurretPitch = UKismetMathLibrary::FClamp(CalculatedPitch, MinPitch, TurretVerticalRange.Y);
	}
	else
	{
		TurretYaw = TurretRotation.Yaw;

		const bool bInClipRange = UKismetMathLibrary::InRange_FloatFloat(TurretRotation.Yaw, ClippingRangeMin, ClippingRangeMax, true, true);
		const double MinPitch = bInClipRange ? TurretHeightRangeClip : TurretVerticalRange.X;
		TurretPitch = UKismetMathLibrary::FClamp(TurretRotation.Pitch, MinPitch, TurretVerticalRange.Y);
	}
}
void ABP_TankController_Chaos_CPP::UpdateMachineGunRotation_Old()
{
	const FRotator StabilizedTarget = Stabilization
		? UKismetMathLibrary::NormalizedDeltaRotator(Rep_ControlRotation, GetActorRotation())
		: Rep_ControlRotation;
	const FRotator InterpTarget = TurretBlocking ? MGRotation : StabilizedTarget;

	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	MGRotation = UKismetMathLibrary::RInterpTo_Constant(MGRotation, InterpTarget, DeltaTime, MGRotationSpeed);

	MGYaw = MGRotation.Yaw;
	MGPitch = UKismetMathLibrary::FClamp(MGRotation.Pitch, MGVerticalRange.X, MGVerticalRange.Y);
}
void ABP_TankController_Chaos_CPP::UpdateTurretRotation(FName BoneToRotate, double YawRotSpeed, TSubclassOf<AActor> GunProjectile, FRotator TurretRotCurrent, FVector2D HorizontalRange, bool CalculateBallistix, FVector TargetPoint, float& TurretRotNew_Roll, float& TurretRotNew_Pitch, float& TurretRotNew_Yaw)
{
	USkeletalMeshComponent* MeshComp = GetMesh();

	// Socket to target vector, world space
	FVector SocketToTargetTurret = TargetPoint - MeshComp->GetSocketLocation(BoneToRotate);

	if (CalculateBallistix)
	{
		const double AimPointCorrection = BallisticsCalculation(SocketToTargetTurret, GunProjectile);
		SocketToTargetTurret = SocketToTargetTurret + FVector(0.0, 0.0, AimPointCorrection);
	}

	// Project onto the mesh's horizontal plane, then convert to the turret's parent-bone space
	const FVector MeshUp = MeshComp->GetUpVector();
	const FVector ProjectedXY = FVector::VectorPlaneProject(SocketToTargetTurret, MeshUp);

	const FName ParentBone = MeshComp->GetParentBone(BoneToRotate);
	const FRotator ParentBoneRot = MeshComp->GetSocketRotation(ParentBone);
	const FVector TargetVectorProjectedXY = UKismetMathLibrary::LessLess_VectorRotator(ProjectedXY, ParentBoneRot);

	const FRotator TargetRotation = UKismetMathLibrary::Conv_VectorToRotator(TargetVectorProjectedXY);

	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	const FRotator TurretRotNew = UKismetMathLibrary::RInterpTo_Constant(TurretRotCurrent, TargetRotation, DeltaTime, YawRotSpeed);

	TurretRotNew_Roll = 0.f;
	TurretRotNew_Pitch = 0.f;
	TurretRotNew_Yaw = UKismetMathLibrary::FClamp(TurretRotNew.Yaw, HorizontalRange.X, HorizontalRange.Y);
}
void ABP_TankController_Chaos_CPP::UpdateGunRotation(int32 WeaponIndex, FName BoneToRotate, double GunPitchSpeed, FRotator GunRotCurrent, FVector2D VerticalRange, bool CalculateBallistix, FVector TargetPoint, TSubclassOf<AActor> GunProjectile, float& GunRotNew_Roll, float& GunRotNew_Pitch, float& GunRotNew_Yaw)
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	FVector SocketToTargetGun = TargetPoint - MeshComp->GetSocketLocation(BoneToRotate);

	// Is WeaponIndex the weapon currently selected on BPC_TankWeapon (BP_TankWeapon_C "Current WeaponIndex")?
	int32 CurrentWeaponIndex = 0;
	if (BPC_TankWeapon)
	{
		if (FIntProperty* Prop = FindFProperty<FIntProperty>(BPC_TankWeapon->GetClass(), TEXT("CurrentWeaponIndex")))
		{
			CurrentWeaponIndex = Prop->GetPropertyValue_InContainer(BPC_TankWeapon);
		}
	}
	const bool bCurrentWeapon = (WeaponIndex == CurrentWeaponIndex);

	if (CalculateBallistix)
	{
		const double AimPointCorrection = BallisticsCalculation(SocketToTargetGun, GunProjectile);
		if (bCurrentWeapon)
		{
			AimPointCorrectionUI = AimPointCorrection;
		}
		SocketToTargetGun = SocketToTargetGun + FVector(0.0, 0.0, AimPointCorrection);
	}
	else if (bCurrentWeapon)
	{
		AimPointCorrectionUI = 0.0;
	}

	if (bCurrentWeapon)
	{
		// Crosshair UI trace: how far along the gun's forward vector is the nearest hit (or the aim vector length if nothing is hit)
		const FVector TraceStart = MeshComp->GetSocketLocation(BoneToRotate);
		const FRotator TraceRot = MeshComp->GetSocketRotation(BoneToRotate);
		const FVector TraceEnd = TraceStart + UKismetMathLibrary::GetForwardVector(TraceRot) * 250000.0;

		FHitResult Hit;
		FCollisionQueryParams QueryParams(NAME_None, false, this);
		const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
		CrosshairTraceClamp = bHit ? Hit.Distance : SocketToTargetGun.Size();
	}

	// Convert the world-space aim vector into the actor's local space; only Pitch is carried forward (Roll/Yaw unwired in source, default 0)
	const FVector LocalTargetVector = UKismetMathLibrary::LessLess_VectorRotator(SocketToTargetGun, GetActorRotation());
	const FRotator LocalTargetRotation(UKismetMathLibrary::Conv_VectorToRotator(LocalTargetVector).Pitch, 0.0, 0.0);

	if (BoneToRotate == TEXT("main_gun"))
	{
		const FRotator GunRotationNew = SelfCollisionCheck(BoneToRotate, GunRotCurrent, LocalTargetRotation, GunPitchSpeed, VerticalRange);
		GunRotNew_Roll = GunRotationNew.Roll;
		GunRotNew_Pitch = GunRotationNew.Pitch;
		GunRotNew_Yaw = GunRotationNew.Yaw;
	}
	else
	{
		const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
		const FRotator Interp = UKismetMathLibrary::RInterpTo_Constant(GunRotCurrent, LocalTargetRotation, DeltaTime, GunPitchSpeed);
		GunRotNew_Roll = 0.f;
		GunRotNew_Pitch = UKismetMathLibrary::FClamp(Interp.Pitch, VerticalRange.X, VerticalRange.Y);
		GunRotNew_Yaw = 0.f;
	}
}
double ABP_TankController_Chaos_CPP::BallisticsCalculation(FVector VectorBetweenSocketAndTarget, TSubclassOf<AActor> GunProjectile)
{
	// Class-default projectile stats (BP_ProjectileMaster_C's "Initial Speed" / "Projectile Gravity Scale")
	double ProjectileSpeed = 0.0;
	double ProjectileGravityScale = 0.0;
	if (GunProjectile)
	{
		const UObject* CDO = GunProjectile->GetDefaultObject();
		if (FDoubleProperty* SpeedProp = FindFProperty<FDoubleProperty>(GunProjectile, TEXT("InitialSpeed")))
		{
			ProjectileSpeed = SpeedProp->GetPropertyValue_InContainer(CDO);
		}
		if (FDoubleProperty* GravProp = FindFProperty<FDoubleProperty>(GunProjectile, TEXT("ProjectileGravityScale")))
		{
			ProjectileGravityScale = GravProp->GetPropertyValue_InContainer(CDO);
		}
	}

	const double DistanceToTarget = VectorBetweenSocketAndTarget.Size();
	const FVector TargetVectorProjectedXYWorld = FVector::VectorPlaneProject(VectorBetweenSocketAndTarget, FVector(0.0, 0.0, 1.0));
	const FVector TargetVectorProjectedZWorld = VectorBetweenSocketAndTarget.ProjectOnToNormal(FVector(0.0, 0.0, 1.0));

	const double PrimaryAngle = UKismetMathLibrary::DegAcos(TargetVectorProjectedXYWorld.Size() / DistanceToTarget);

	// Approximate correction for projectile speed decay when firing at high-altitude targets (theoretical/inexact,
	// per the source comment: does not account for drag over the whole trajectory)
	const double GravityTerm = 980.0 * ProjectileGravityScale;
	const double DecayTerm = (GravityTerm * UKismetMathLibrary::DegSin(PrimaryAngle) * (DistanceToTarget / ProjectileSpeed)) * -0.25;
	const double EffectiveSpeedTerm = ProjectileSpeed + DecayTerm;
	const double CosPrimary = UKismetMathLibrary::DegCos(PrimaryAngle);
	const double AsinArg = (2.0 * (DistanceToTarget * GravityTerm * CosPrimary)) / (2.0 * (EffectiveSpeedTerm * EffectiveSpeedTerm));
	const double AdditionalElevationAngle = (FMath::Asin(AsinArg) / 2.0) * 57.324841; // radians -> degrees

	const double AimPointCorrection = UKismetMathLibrary::DegTan(PrimaryAngle + AdditionalElevationAngle) * TargetVectorProjectedXYWorld.Size() - TargetVectorProjectedZWorld.Size();
	return AimPointCorrection;
}
void ABP_TankController_Chaos_CPP::TurretsAndGunsRotCalculation()
{
	if (TurretBlocking)
	{
		IsTurretRotating = false;
		return;
	}

	// Trace from the camera along its forward vector to find where the player is aiming
	if (!Camera)
	{
		return;
	}
	const FVector TraceStart = Camera->GetComponentLocation();
	const FVector TraceEnd = TraceStart + Camera->GetForwardVector() * 250000.0;
	FHitResult Hit;
	FCollisionQueryParams QueryParams(NAME_None, false, this);
	const bool IsThereTarget = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
	const FVector TargetPoint = IsThereTarget ? Hit.Location : TraceEnd;

	if (!BPC_TankWeapon)
	{
		return;
	}

	// NOTE: Weapons is an array of S_Weapon on BP_TankWeapon_C (stays Blueprint); resolved here via reflection.
	// See BallisticsCalculation/UpdateGunRotation notes for the same caveat on projectile class access.
	UScriptStruct* WeaponStruct = nullptr;
	FArrayProperty* WeaponsArrayProp = TSWeaponReflection::FindWeaponsArrayProperty(BPC_TankWeapon, WeaponStruct);
	if (!WeaponsArrayProp)
	{
		return;
	}
	FScriptArrayHelper WeaponsHelper(WeaponsArrayProp, WeaponsArrayProp->ContainerPtrToValuePtr<void>(BPC_TankWeapon));

	USkeletalMeshComponent* MeshComp = GetMesh();

	// --- Turrets ---
	const FRotator TurretStabilizingRotation = Stabilization ? FRotator(0.0, GetActorRotation().Yaw, 0.0) : FRotator::ZeroRotator;
	for (int32 i = 0; i < WeaponsHelper.Num(); ++i)
	{
		void* Elem = WeaponsHelper.GetRawPtr(i);
		const FName ProjectileSocket = TSWeaponReflection::GetName(WeaponStruct, Elem, TEXT("ProjectileSocket"));
		const double TurretRotSpeedField = TSWeaponReflection::GetDouble(WeaponStruct, Elem, TEXT("TurretRotationSpeed"));
		const TSubclassOf<AActor> Projectile = TSWeaponReflection::GetActorClass(WeaponStruct, Elem, TEXT("Projectile"));
		const FVector2D HorizontalRange = TSWeaponReflection::GetVector2D(WeaponStruct, Elem, TEXT("HorizontalRange"));

		const FName ProjectileSocketBone = MeshComp->GetSocketBoneName(ProjectileSocket);
		const FName TurretBone = MeshComp->GetParentBone(ProjectileSocketBone);

		const FRotator TurretRotCurrent = UKismetMathLibrary::NormalizedDeltaRotator(TurretsRotUnstabilized[i], TurretStabilizingRotation);

		float RollOut = 0.f, PitchOut = 0.f, YawOut = 0.f;
		UpdateTurretRotation(TurretBone, TurretRotSpeedField, Projectile, TurretRotCurrent, HorizontalRange, IsThereTarget, TargetPoint, RollOut, PitchOut, YawOut);
		const FRotator TurretRotNew(PitchOut, YawOut, RollOut);

		TurretsRotUnstabilized[i] = UKismetMathLibrary::ComposeRotators(TurretRotNew, TurretStabilizingRotation);
		TurretsRot[i] = TurretRotNew;
	}

	// --- Guns ---
	const FRotator GunStabilizingRotation = Stabilization ? FRotator(MeshComp->GetSocketRotation(TEXT("turret")).Pitch, 0.0, 0.0) : FRotator::ZeroRotator;
	for (int32 i = 0; i < WeaponsHelper.Num(); ++i)
	{
		void* Elem = WeaponsHelper.GetRawPtr(i);
		const FName ProjectileSocket = TSWeaponReflection::GetName(WeaponStruct, Elem, TEXT("ProjectileSocket"));
		const double GunRotationSpeed = TSWeaponReflection::GetDouble(WeaponStruct, Elem, TEXT("GunRotationSpeed"));
		const TSubclassOf<AActor> Projectile = TSWeaponReflection::GetActorClass(WeaponStruct, Elem, TEXT("Projectile"));
		const FVector2D VerticalRange = TSWeaponReflection::GetVector2D(WeaponStruct, Elem, TEXT("VerticalRange"));

		const FName GunBone = MeshComp->GetSocketBoneName(ProjectileSocket);

		const FRotator GunRotCurrent = UKismetMathLibrary::NormalizedDeltaRotator(GunsRotUnstabilized[i], GunStabilizingRotation);

		float RollOut = 0.f, PitchOut = 0.f, YawOut = 0.f;
		UpdateGunRotation(i, GunBone, GunRotationSpeed, GunRotCurrent, VerticalRange, IsThereTarget, TargetPoint, Projectile, RollOut, PitchOut, YawOut);
		const FRotator GunRotNew(PitchOut, YawOut, RollOut);

		GunsRotUnstabilized[i] = UKismetMathLibrary::ComposeRotators(GunRotNew, GunStabilizingRotation);
		GunsRot[i] = GunRotNew;
	}

	// For sounds: has the combined turret+gun world rotation changed since last frame?
	const FRotator CombinedTurretGunRotation = UKismetMathLibrary::ComposeRotators(MeshComp->GetSocketRotation(TEXT("turret")), MeshComp->GetSocketRotation(TEXT("main_gun")));
	IsTurretRotating = !MainTurretAndGunRotation.Equals(CombinedTurretGunRotation, 0.1f);
	MainTurretAndGunRotation = CombinedTurretGunRotation;
}
void ABP_TankController_Chaos_CPP::RecalculateGunAndTurretRotation()
{
	// Re-bake the stabilization offset into TurretsRotUnstabilized (turret yaw follows hull yaw when unstabilized)
	const double YawMultiplier = Stabilization ? 1.0 : -1.0;
	FRotator RotCorrector(0.0, GetActorRotation().Yaw * YawMultiplier, 0.0);
	for (FRotator& TurretRot : TurretsRotUnstabilized)
	{
		TurretRot = UKismetMathLibrary::ComposeRotators(TurretRot, RotCorrector);
	}

	// Re-bake the stabilization offset into GunsRotUnstabilized (gun pitch follows turret socket pitch when unstabilized)
	const double PitchMultiplier = Stabilization ? 1.0 : -1.0;
	RotCorrector = FRotator(GetMesh()->GetSocketRotation(TEXT("turret")).Pitch * PitchMultiplier, 0.0, 0.0);
	for (FRotator& GunRot : GunsRotUnstabilized)
	{
		GunRot = UKismetMathLibrary::ComposeRotators(GunRot, RotCorrector);
	}
}
void ABP_TankController_Chaos_CPP::ScatteringCalculation()
{
	if (!BPC_TankWeapon)
	{
		return;
	}

	UScriptStruct* WeaponStruct = nullptr;
	FArrayProperty* WeaponsArrayProp = TSWeaponReflection::FindWeaponsArrayProperty(BPC_TankWeapon, WeaponStruct);
	if (!WeaponsArrayProp)
	{
		return;
	}
	FScriptArrayHelper WeaponsHelper(WeaponsArrayProp, WeaponsArrayProp->ContainerPtrToValuePtr<void>(BPC_TankWeapon));

	const float WorldDeltaSeconds = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	const double BodyLinearVelocity = GetMesh()->GetComponentVelocity().Size();

	for (int32 i = 0; i < WeaponsHelper.Num(); ++i)
	{
		void* Elem = WeaponsHelper.GetRawPtr(i);
		const double MinScattering = TSWeaponReflection::GetDouble(WeaponStruct, Elem, TEXT("MinScattering"));
		const double AimingTime = TSWeaponReflection::GetDouble(WeaponStruct, Elem, TEXT("AimingTime"));
		const double ScatteringStabAngular = TSWeaponReflection::GetDouble(WeaponStruct, Elem, TEXT("ScatteringStabAngular"));
		const double ScatteringStabLinear = TSWeaponReflection::GetDouble(WeaponStruct, Elem, TEXT("ScatteringStabLinear"));

		// Scattering from turning: how much the combined turret+gun rotation changed this frame, normalized by ScatteringStabAngular
		const FRotator CurrentCombined = UKismetMathLibrary::ComposeRotators(TurretsRot[i], GunsRot[i]);
		const FRotator PrevCombined = UKismetMathLibrary::ComposeRotators(TurretsRotPrevFrame[i], GunsRotPrevFrame[i]);
		const FRotator Delta = UKismetMathLibrary::NormalizedDeltaRotator(CurrentCombined, PrevCombined);
		const double ScatteringFromTurn = (FMath::Abs(Delta.Pitch) + FMath::Abs(Delta.Yaw)) / ScatteringStabAngular;

		// Scattering from linear movement, normalized by ScatteringStabLinear
		const double ScatteringFromLinearMove = BodyLinearVelocity / ScatteringStabLinear;

		const double ScatteringGain = (ScatteringFromLinearMove + ScatteringFromTurn) * WorldDeltaSeconds;

		// Aiming: scattering decays back toward MinScattering over AimingTime
		const double ScatteringDecay = (FinalScattering[i] / 2.0) * WorldDeltaSeconds / AimingTime;

		FinalScattering[i] = UKismetMathLibrary::FClamp(FinalScattering[i] + ScatteringGain - ScatteringDecay, MinScattering, 100000000000.0);
	}

	GunsRotPrevFrame = GunsRot;
	TurretsRotPrevFrame = TurretsRot;
}
FRotator ABP_TankController_Chaos_CPP::SelfCollisionCheck(FName BoneToRotate, FRotator CurrentRotation, FRotator TargetRotation, double GunPitchSpeed, FVector2D VerticalRange)
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	const FVector SocketLoc = MeshComp->GetSocketLocation(BoneToRotate) - FVector(0.0, 0.0, 10.0);
	const FRotator SocketRot = MeshComp->GetSocketRotation(BoneToRotate);
	FCollisionQueryParams QueryParams;

	// Obstacle check: trace along the gun's current forward vector to see if it is already pointed into the hull ("body" bone)
	FHitResult ObstacleHit;
	const FVector TraceStart = SocketLoc + SocketRot.Vector() * 500.0;
	GetWorld()->LineTraceSingleByChannel(ObstacleHit, TraceStart, SocketLoc, ECC_Visibility, QueryParams);

	if (ObstacleHit.BoneName == TEXT("body"))
	{
		// Blocked: keep nudging ExtraRotation up in 0.1-degree steps and re-tracing until clear of the hull
		FName HitBone = ObstacleHit.BoneName;
		double ExtraRotation = 0.0;
		while (HitBone == TEXT("body"))
		{
			ExtraRotation += 0.1;

			const FRotator RotWithExtra(SocketRot.Pitch + ExtraRotation, SocketRot.Yaw, SocketRot.Roll);
			FHitResult Hit2;
			const FVector Start2 = SocketLoc + RotWithExtra.Vector() * 500.0;
			GetWorld()->LineTraceSingleByChannel(Hit2, Start2, SocketLoc, ECC_Visibility, QueryParams);
			HitBone = Hit2.BoneName;
		}
		return FRotator(CurrentRotation.Pitch + ExtraRotation, CurrentRotation.Yaw, CurrentRotation.Roll);
	}

	// Not blocked: interpolate toward TargetRotation and clamp, then verify the resulting pitch doesn't turn into the hull
	const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	const FRotator Interp = UKismetMathLibrary::RInterpTo_Constant(CurrentRotation, TargetRotation, DeltaTime, GunPitchSpeed);
	const double NewPitch = UKismetMathLibrary::FClamp(Interp.Pitch, VerticalRange.X, VerticalRange.Y);

	const FRotator TurretSocketRot = MeshComp->GetSocketRotation(TEXT("turret"));
	const FRotator CandidateRot = UKismetMathLibrary::ComposeRotators(FRotator(NewPitch, 0.0, 0.0), TurretSocketRot);

	FHitResult FinalHit;
	const FVector Start3 = SocketLoc + CandidateRot.Vector() * 500.0;
	GetWorld()->LineTraceSingleByChannel(FinalHit, Start3, SocketLoc, ECC_Visibility, QueryParams);

	if (FinalHit.BoneName == TEXT("body"))
	{
		// Would turn into an obstacle: stay put
		return CurrentRotation;
	}

	return FRotator(NewPitch, 0.0, 0.0);
}

// ============================================================
// Camera, HUD and UI update functions
// ============================================================
void ABP_TankController_Chaos_CPP::ReplicateControlRotation()
{
	if (HasAuthority() || IsLocallyControlled())
	{
		if (Stabilization)
		{
			// Relative to the world
			Rep_ControlRotation = GetBaseAimRotation();
		}
		else
		{
			// Relative to the hull
			const FRotator MeshWorldRotation = GetMesh()->GetComponentRotation();
			const FRotator Delta = UKismetMathLibrary::NormalizedDeltaRotator(MeshWorldRotation, GetBaseAimRotation());
			Rep_ControlRotation = Delta * -1.0f;
		}
	}
}
// --- shared reflection helpers (BP_TankWeapon_C fields have no native C++ type) ---
int32 ABP_TankController_Chaos_CPP::GetIntProp(const UObject* Obj, FName PropertyName, int32 DefaultValue)
{
	if (!Obj) return DefaultValue;
	if (const FIntProperty* P = FindFProperty<FIntProperty>(Obj->GetClass(), PropertyName))
	{
		return P->GetPropertyValue_InContainer(Obj);
	}
	return DefaultValue;
}

bool ABP_TankController_Chaos_CPP::GetBoolProp(const UObject* Obj, FName PropertyName, bool DefaultValue)
{
	if (!Obj) return DefaultValue;
	if (const FBoolProperty* P = FindFProperty<FBoolProperty>(Obj->GetClass(), PropertyName))
	{
		return P->GetPropertyValue_InContainer(Obj);
	}
	return DefaultValue;
}

double ABP_TankController_Chaos_CPP::GetDoubleProp(const UObject* Obj, FName PropertyName, double DefaultValue)
{
	if (!Obj) return DefaultValue;
	if (const FDoubleProperty* P = FindFProperty<FDoubleProperty>(Obj->GetClass(), PropertyName))
	{
		return P->GetPropertyValue_InContainer(Obj);
	}
	return DefaultValue;
}

FVector ABP_TankController_Chaos_CPP::GetVectorProp(const UObject* Obj, FName PropertyName, FVector DefaultValue)
{
	if (!Obj) return DefaultValue;
	if (const FStructProperty* P = FindFProperty<FStructProperty>(Obj->GetClass(), PropertyName))
	{
		if (P->Struct == TBaseStructure<FVector>::Get())
		{
			return *P->ContainerPtrToValuePtr<FVector>(Obj);
		}
	}
	return DefaultValue;
}

int32 ABP_TankController_Chaos_CPP::GetArrayNum(const UObject* Obj, FName ArrayPropertyName)
{
	if (!Obj) return 0;
	if (const FArrayProperty* ArrProp = FindFProperty<FArrayProperty>(Obj->GetClass(), ArrayPropertyName))
	{
		FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(Obj));
		return Helper.Num();
	}
	return 0;
}

void* ABP_TankController_Chaos_CPP::GetStructPropPtr(UObject* Obj, FName PropertyName, UScriptStruct** OutStruct)
{
	if (!Obj) return nullptr;
	if (FStructProperty* P = FindFProperty<FStructProperty>(Obj->GetClass(), PropertyName))
	{
		if (OutStruct) *OutStruct = P->Struct;
		return P->ContainerPtrToValuePtr<void>(Obj);
	}
	return nullptr;
}

void* ABP_TankController_Chaos_CPP::GetArrayElementStructPtr(UObject* Obj, FName ArrayPropertyName, int32 Index, UScriptStruct** OutStruct)
{
	if (!Obj) return nullptr;
	FArrayProperty* ArrProp = FindFProperty<FArrayProperty>(Obj->GetClass(), ArrayPropertyName);
	if (!ArrProp) return nullptr;
	FStructProperty* InnerStruct = CastField<FStructProperty>(ArrProp->Inner);
	if (!InnerStruct) return nullptr;
	FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(Obj));
	if (!Helper.IsValidIndex(Index)) return nullptr;
	if (OutStruct) *OutStruct = InnerStruct->Struct;
	return Helper.GetRawPtr(Index);
}

FText ABP_TankController_Chaos_CPP::GetTextFieldFromStruct(void* StructPtr, UScriptStruct* Struct, FName FieldName)
{
	if (!StructPtr || !Struct) return FText::GetEmpty();
	if (const FTextProperty* P = FindFProperty<FTextProperty>(Struct, FieldName))
	{
		return P->GetPropertyValue(P->ContainerPtrToValuePtr<void>(StructPtr));
	}
	return FText::GetEmpty();
}

double ABP_TankController_Chaos_CPP::GetDoubleFieldFromStruct(void* StructPtr, UScriptStruct* Struct, FName FieldName, double DefaultValue)
{
	if (!StructPtr || !Struct) return DefaultValue;
	if (const FDoubleProperty* P = FindFProperty<FDoubleProperty>(Struct, FieldName))
	{
		return P->GetPropertyValue(P->ContainerPtrToValuePtr<void>(StructPtr));
	}
	return DefaultValue;
}

void ABP_TankController_Chaos_CPP::UpdateHUD()
{
	if (!IsValid(HUD)) return;

	// Update Camo
	if (CamoVariations.IsValidIndex(CamoCurrent))
	{
		if (UTextBlock* CamoNameText = Cast<UTextBlock>(HUD->GetWidgetFromName(TEXT("CamoName"))))
		{
			CamoNameText->SetText(FText::FromString(CamoVariations[CamoCurrent].CamoName));
		}
	}

	// Update Speed
	if (UChaosWheeledVehicleMovementComponent* Movement = Cast<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent()))
	{
		const int32 SpeedKmh = FMath::RoundToInt(Movement->GetForwardSpeed() * 0.036);
		if (UTextBlock* SpeedText = Cast<UTextBlock>(HUD->GetWidgetFromName(TEXT("SpeedText"))))
		{
			SpeedText->SetText(FText::AsNumber(SpeedKmh));
		}
	}

	// Update Weapon Name (from BPC_TankWeapon.CurrentWeapon.WeaponName)
	UScriptStruct* WeaponStruct = nullptr;
	void* CurrentWeaponPtr = GetStructPropPtr(BPC_TankWeapon, TEXT("CurrentWeapon"), &WeaponStruct);
	if (UTextBlock* WeaponNameText = Cast<UTextBlock>(HUD->GetWidgetFromName(TEXT("WeaponName"))))
	{
		WeaponNameText->SetText(GetTextFieldFromStruct(CurrentWeaponPtr, WeaponStruct, TEXT("WeaponName")));
	}

	// Update Weapon Ammo: show the currently selected weapon's ammo count, blank while it is reloading
	const int32 CurrentWeaponIndex = GetIntProp(BPC_TankWeapon, TEXT("CurrentWeaponIndex"));
	static const TCHAR* AmmoPropNames[5] = { TEXT("Weapon1CurrentAmmo"), TEXT("Weapon2CurrentAmmo"), TEXT("Weapon3CurrentAmmo"), TEXT("Weapon4CurrentAmmo"), TEXT("Weapon5CurrentAmmo") };
	static const TCHAR* ReloadingPropNames[5] = { TEXT("ReloadingWeapon1"), TEXT("ReloadingWeapon2"), TEXT("ReloadingWeapon3"), TEXT("ReloadingWeapon4"), TEXT("ReloadingWeapon5") };
	if (UTextBlock* AmmoAmountText = Cast<UTextBlock>(HUD->GetWidgetFromName(TEXT("AmmoAmount"))))
	{
		if (CurrentWeaponIndex >= 0 && CurrentWeaponIndex < 5)
		{
			const bool bReloading = GetBoolProp(BPC_TankWeapon, ReloadingPropNames[CurrentWeaponIndex]);
			if (bReloading)
			{
				AmmoAmountText->SetText(FText::GetEmpty());
			}
			else
			{
				const int32 Ammo = GetIntProp(BPC_TankWeapon, AmmoPropNames[CurrentWeaponIndex]);
				AmmoAmountText->SetText(FText::AsNumber(Ammo));
			}
		}
	}
}
void ABP_TankController_Chaos_CPP::UpdateCrosshairPositionAndSize()
{
	if (!IsValid(Crosshair)) return;

	const FVector TraceEnd = GetVectorProp(BPC_TankWeapon, TEXT("TraceEnd"));
	FVector2D ScreenPosition;
	UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PlayerController, TraceEnd, ScreenPosition, false);

	const FVector2D DesiredSize = Crosshair->GetDesiredSize();
	const FVector2D Position(ScreenPosition.X - DesiredSize.X / 2.0, ScreenPosition.Y - DesiredSize.Y / 2.0);
	Crosshair->SetPositionInViewport(Position, false);

	// Size the crosshair ring from the current weapon's scattering cone at this range
	const int32 WeaponIndex = GetIntProp(BPC_TankWeapon, TEXT("CurrentWeaponIndex"));
	const double ScatterCm = FinalScattering.IsValidIndex(WeaponIndex) ? FinalScattering[WeaponIndex] : 0.0;
	const double ScatterAngleDeg = UKismetMathLibrary::DegAtan(ScatterCm / 100.0);
	const double FOV = Camera ? Camera->FieldOfView : 90.0;
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);
	const double SizePx = (ScatterAngleDeg / FOV) * ViewportSize.X;

	if (UImage* CrosshairImage = Cast<UImage>(Crosshair->GetWidgetFromName(TEXT("Image_25"))))
	{
		if (UCanvasPanelSlot* CanvasSlot = UWidgetLayoutLibrary::SlotAsCanvasSlot(CrosshairImage))
		{
			CanvasSlot->SetSize(FVector2D(SizePx, SizePx));
		}
	}
}
void ABP_TankController_Chaos_CPP::CameraAutoSetting()
{
	if (!SpringArmArcade || !SpringArmSniper || !GetMesh())
	{
		UE_LOG(LogTemp, Error, TEXT("ABP_TankController_Chaos_CPP::CameraAutoSetting: missing SpringArmArcade/SpringArmSniper/Mesh on %s - skipping camera setup"), *GetNameSafe(this));
		return;
	}

	// Start values (captured once so Zoom+SniperMode can restore arcade framing when leaving sniper mode)
	StartTargetArmLengthArcade = SpringArmArcade->TargetArmLength;
	StartSocketOffsetArcade = SpringArmArcade->SocketOffset;
	StartLagSpeed = SpringArmSniper->CameraLagSpeed;

	GetMesh()->UpdateComponentToWorld();

	// SpringArmArcade alignment: align X/Y with the turret's rotation axis, then Z with the gun's rotation axis
	const FVector TurretSocketLocation = GetMesh()->GetSocketLocation(TEXT("turret"));
	SpringArmArcade->SetWorldLocation(TurretSocketLocation);

	const FVector GunSocketLocation = GetMesh()->GetSocketLocation(TEXT("main_gun"));
	FVector NewLocation = SpringArmArcade->GetComponentLocation();
	NewLocation.Z = GunSocketLocation.Z; // aligns the Z axis with the gun
	SpringArmArcade->SetWorldLocation(NewLocation);

	// equal to the distance between the axis of rotation of the turret and the axis of rotation of the gun
	const FVector2D GunXY(GunSocketLocation.X, GunSocketLocation.Y);
	const FVector2D TurretXY(TurretSocketLocation.X, TurretSocketLocation.Y);
	TargetArmLengthArcade_2 = (GunXY - TurretXY).Size() * -1.0;
}
double ABP_TankController_Chaos_CPP::CameraPitchLimit(double AxisValue)
{
	const double AxisValueInput = AxisValue;

	if (!SniperMode)
	{
		return AxisValueInput;
	}

	// GunMinAngle is read from weapon slot 0's vertical range (main gun), regardless of the currently selected weapon
	double GunMinAngle = 0.0;
	UScriptStruct* WeaponStruct = nullptr;
	void* Weapon0Ptr = GetArrayElementStructPtr(BPC_TankWeapon, TEXT("Weapons"), 0, &WeaponStruct);
	if (Weapon0Ptr && WeaponStruct)
	{
		if (const FStructProperty* VertRangeProp = FindFProperty<FStructProperty>(WeaponStruct, TEXT("VerticalRange")))
		{
			if (const FVector2D* VertRange = VertRangeProp->ContainerPtrToValuePtr<FVector2D>(Weapon0Ptr))
			{
				GunMinAngle = VertRange->X;
			}
		}
	}

	const FVector CameraForward = Camera->GetForwardVector();
	const FVector ActorUp = GetActorUpVector();
	const FVector Projected = UKismetMathLibrary::ProjectVectorOnToVector(CameraForward, ActorUp);
	const double Length = Projected.Size();
	const bool bPositive = (Projected.Z * ActorUp.Z) > 0.0;
	const double SignedLength = bPositive ? Length : -Length;
	const double GunCurrentAngle = UKismetMathLibrary::DegAsin(SignedLength);

	// if the limit is exceeded, the camera goes back
	const bool bUseRawInput = (AxisValueInput < 0.0) || (GunCurrentAngle > (GunMinAngle - 2.0));
	if (bUseRawInput)
	{
		return AxisValueInput;
	}

	// for smoothness / speed multiplier
	const double Delta = GunMinAngle - GunCurrentAngle;
	return (Delta * Delta) / -800.0;
}
void ABP_TankController_Chaos_CPP::SniperModeToggle()
{
	if (!SpringArmArcade || !SpringArmSniper || !Camera)
	{
		return;
	}

	SniperMode = !SniperMode;

	if (SniperMode)
	{
		SpringArmArcade->TargetArmLength = TargetArmLengthArcade_2;
		SpringArmArcade->SocketOffset = FVector::ZeroVector;
		SpringArmSniper->SocketOffset = SocketOffsetSniper;

		FPostProcessSettings SniperPost;
		SniperPost.bOverride_ColorGainShadows = true;
		SniperPost.ColorGainShadows = FVector4(0.0, 0.5, 0.0, 1.0);
		SniperPost.bOverride_VignetteIntensity = true;
		SniperPost.VignetteIntensity = 1.0f;
		Camera->PostProcessSettings = SniperPost;
	}
	else
	{
		SpringArmArcade->TargetArmLength = StartTargetArmLengthArcade;
		SpringArmArcade->SocketOffset = StartSocketOffsetArcade;
		Camera->SetFieldOfView(90.0f);
		SpringArmSniper->CameraLagSpeed = StartLagSpeed;
		SpringArmSniper->SocketOffset = FVector::ZeroVector;

		FPostProcessSettings ArcadePost;
		ArcadePost.bOverride_VignetteIntensity = true;
		ArcadePost.VignetteIntensity = 0.54f;
		Camera->PostProcessSettings = ArcadePost;
	}

	UpdateZoomRatioUI();
}

void ABP_TankController_Chaos_CPP::ZoomIn()
{
	if (!SpringArmArcade || !Camera)
	{
		return;
	}

	if (SniperMode)
	{
		const double NewFOV = FMath::Clamp(Camera->FieldOfView * 0.5, 90.0 / SniperCameraMaxZoom, 90.0);
		Camera->SetFieldOfView(NewFOV);
		UpdateZoomRatioUI();
		return;
	}

	if (SpringArmArcade->TargetArmLength == TargetArmLengthMin)
	{
		// already at max arcade zoom-in: hand off to sniper mode
		SniperModeToggle();
		return;
	}

	const double Step = LeftCtrlPressed ? CameraZoomStep / 5.0 : CameraZoomStep;
	SpringArmArcade->TargetArmLength = FMath::Clamp(SpringArmArcade->TargetArmLength - Step, TargetArmLengthMin, TargetArmLengthMax);
}

void ABP_TankController_Chaos_CPP::ZoomOut()
{
	if (!SpringArmArcade || !Camera)
	{
		return;
	}

	if (SniperMode)
	{
		if (Camera->FieldOfView == 90.0f)
		{
			// already at minimum sniper zoom: hand off back to arcade mode
			SniperModeToggle();
			return;
		}
		const double NewFOV = FMath::Clamp(Camera->FieldOfView * 2.0, 90.0 / SniperCameraMaxZoom, 90.0);
		Camera->SetFieldOfView(NewFOV);
		UpdateZoomRatioUI();
		return;
	}

	const double Step = LeftCtrlPressed ? CameraZoomStep / 5.0 : CameraZoomStep;
	SpringArmArcade->TargetArmLength = FMath::Clamp(SpringArmArcade->TargetArmLength + Step, TargetArmLengthMin, TargetArmLengthMax);
}
void ABP_TankController_Chaos_CPP::UpdateHealthBar()
{
	if (!IsValid(HUD)) return;

	const double Percent = (HealthMax != 0.0) ? (Health / HealthMax) : 0.0;

	if (UProgressBar* HealthBar = Cast<UProgressBar>(HUD->GetWidgetFromName(TEXT("HealthBar"))))
	{
		HealthBar->SetPercent(Percent);
		// Red rises as health drops below 50%, green rises as health climbs above 0% (both clamped to [0,0.5]*2)
		const double R = FMath::Clamp(1.0 - Percent, 0.0, 0.5) * 2.0;
		const double G = FMath::Clamp(Percent, 0.0, 0.5) * 2.0;
		HealthBar->SetFillColorAndOpacity(FLinearColor(R, G, 0.0, 1.0));
	}

	if (UTextBlock* HPText = Cast<UTextBlock>(HUD->GetWidgetFromName(TEXT("HP"))))
	{
		HPText->SetText(FText::AsNumber(Health));
	}
}
void ABP_TankController_Chaos_CPP::WeaponSlotsDisplay()
{
	if (!IsValid(HUD)) return;

	static const TCHAR* OverlayNames[5] = { TEXT("OverlayGun_1"), TEXT("OverlayGun_2"), TEXT("OverlayGun_3"), TEXT("OverlayGun_4"), TEXT("OverlayGun_5") };
	static const TCHAR* SpacerNames[4] = { TEXT("SpacerGun_2"), TEXT("SpacerGun_3"), TEXT("SpacerGun_4"), TEXT("SpacerGun_5") };

	// Hide old guns
	for (const TCHAR* Name : OverlayNames)
	{
		if (UWidget* W = HUD->GetWidgetFromName(Name)) W->SetVisibility(ESlateVisibility::Collapsed);
	}
	for (const TCHAR* Name : SpacerNames)
	{
		if (UWidget* W = HUD->GetWidgetFromName(Name)) W->SetVisibility(ESlateVisibility::Collapsed);
	}

	// Unhide new guns: show one overlay per equipped weapon, and a spacer before every weapon after the first
	const int32 NumWeapons = GetArrayNum(BPC_TankWeapon, TEXT("Weapons"));
	for (int32 Index = 0; Index < NumWeapons && Index < 5; ++Index)
	{
		if (UWidget* Overlay = HUD->GetWidgetFromName(OverlayNames[Index]))
		{
			Overlay->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		if (Index != 0 && (Index - 1) < 4)
		{
			if (UWidget* Spacer = HUD->GetWidgetFromName(SpacerNames[Index - 1]))
			{
				Spacer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
		}
	}
}
void ABP_TankController_Chaos_CPP::UpdateChosenWeaponUI()
{
	if (!IsValid(HUD)) return;

	static const TCHAR* ImageNames[5] = { TEXT("Image_ChooseGun_1"), TEXT("Image_ChooseGun_2"), TEXT("Image_ChooseGun_3"), TEXT("Image_ChooseGun_4"), TEXT("Image_ChooseGun_5") };

	for (const TCHAR* Name : ImageNames)
	{
		if (UWidget* W = HUD->GetWidgetFromName(Name)) W->SetVisibility(ESlateVisibility::Collapsed);
	}

	const int32 CurrentWeaponIndex = GetIntProp(BPC_TankWeapon, TEXT("CurrentWeaponIndex"));
	if (CurrentWeaponIndex >= 0 && CurrentWeaponIndex < 5)
	{
		if (UWidget* W = HUD->GetWidgetFromName(ImageNames[CurrentWeaponIndex]))
		{
			W->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}
}
void ABP_TankController_Chaos_CPP::UpdateZoomRatioUI()
{
	if (!IsValid(HUD)) return;
	UTextBlock* ZoomRatioText = Cast<UTextBlock>(HUD->GetWidgetFromName(TEXT("TextZoomRatio")));
	if (!ZoomRatioText) return;

	if (!SniperMode)
	{
		ZoomRatioText->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	ZoomRatioText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	const double FOV = Camera ? Camera->FieldOfView : 90.0;
	const int32 Ratio = FMath::TruncToInt(90.0 / FOV); // To remove the zero after the decimal point
	ZoomRatioText->SetText(FText::FromString(FString::Printf(TEXT("x%d"), Ratio)));
}
void ABP_TankController_Chaos_CPP::UpdateChosenVehicleUI()
{
	if (!IsValid(HUD)) return;

	const FString ClassDisplayName = UKismetSystemLibrary::GetClassDisplayName(GetClass());
	// mirrors BP: RightChop(3) then LeftChop(19) to strip a leading "BP_" and a trailing "_Controller_Chaos_C"-style suffix
	const FString Trimmed = ClassDisplayName.RightChop(3).LeftChop(19);

	if (UTextBlock* TankNameText = Cast<UTextBlock>(HUD->GetWidgetFromName(TEXT("TankName"))))
	{
		TankNameText->SetText(FText::FromString(Trimmed));
	}
}
void ABP_TankController_Chaos_CPP::UpdateDamageCausedUI(int32 Damage)
{
	DamageCausedUI += Damage;

	if (IsValid(HUD))
	{
		if (UTextBlock* DamageText = Cast<UTextBlock>(HUD->GetWidgetFromName(TEXT("Textdamage"))))
		{
			DamageText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			DamageText->SetText(FText::FromString(FString::Printf(TEXT("Caused damage %d"), DamageCausedUI)));
		}
	}

	// BP used a Retriggerable Delay (2s) then hid the text and reset the counter; a retriggerable timer is the C++ equivalent
	GetWorldTimerManager().SetTimer(DamageCausedUITimerHandle, this, &ABP_TankController_Chaos_CPP::HideDamageCausedUI, 2.0f, false);
}

void ABP_TankController_Chaos_CPP::HideDamageCausedUI()
{
	if (IsValid(HUD))
	{
		if (UTextBlock* DamageText = Cast<UTextBlock>(HUD->GetWidgetFromName(TEXT("Textdamage"))))
		{
			DamageText->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	DamageCausedUI = 0;
}
bool ABP_TankController_Chaos_CPP::ReloadWeaponUI(int32 WeaponIndex)
{
	static const TCHAR* ProgressBarNames[5] = { TEXT("ProgressBar_GunReload_1"), TEXT("ProgressBar_GunReload_2"), TEXT("ProgressBar_GunReload_3"), TEXT("ProgressBar_GunReload_4"), TEXT("ProgressBar_GunReload_5") };

	if (!IsValid(HUD) || WeaponIndex < 0 || WeaponIndex >= 5) return false;

	UProgressBar* ReloadBar = Cast<UProgressBar>(HUD->GetWidgetFromName(ProgressBarNames[WeaponIndex]));
	if (!ReloadBar) return false;

	UScriptStruct* WeaponStruct = nullptr;
	void* WeaponPtr = GetArrayElementStructPtr(BPC_TankWeapon, TEXT("Weapons"), WeaponIndex, &WeaponStruct);
	const double ReloadTime = GetDoubleFieldFromStruct(WeaponPtr, WeaponStruct, TEXT("ReloadTime"), 1.0);

	const double DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0;
	const double NewPercent = (ReloadTime != 0.0) ? (DeltaTime / ReloadTime) + ReloadBar->GetPercent() : ReloadBar->GetPercent();
	ReloadBar->SetPercent(NewPercent);

	if (NewPercent >= 1.0)
	{
		ReloadBar->SetPercent(0.0f);
		return true; // Reloaded
	}
	return false; // NotReloaded
}
