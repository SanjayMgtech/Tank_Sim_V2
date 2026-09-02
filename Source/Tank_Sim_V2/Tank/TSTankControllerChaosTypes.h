// Native structs mirroring the YI_TankCollection User Defined Structs used by
// BP_TankController_Chaos (S_TankSplineAnim, S_Antenna, S_CamoOptions), so the
// C++ port keeps the same per-instance data shape the vendor Blueprints authored.
#pragma once

#include "CoreMinimal.h"
#include "TSTankControllerChaosTypes.generated.h"

USTRUCT(BlueprintType)
struct FTSTankSplineAnim
{
	GENERATED_BODY()

	// Index of the spline point this entry animates.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Spline Anim")
	int32 AnimPointIndex = 0;

	// Check for spline points above the wheels; the point "lies" on the wheel if it rises high
	// enough, avoiding wheel-track crossing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Spline Anim")
	bool bInteractWithWheel = false;

	// Maximum vibration amplitude of the spline point.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Spline Anim")
	double VibrationMaxAmplitude = 2.5;

	// Phase of oscillation, in degrees (360 = one full oscillation).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Spline Anim")
	double VibrationPhase = 0.0;

	// Offset of the spline point in the perpendicular direction while moving forward.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Spline Anim")
	double SaggingForward = 0.0;

	// Offset of the spline point in the perpendicular direction while moving backwards.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Spline Anim")
	double SaggingBack = 0.0;
};

USTRUCT(BlueprintType)
struct FTSAntennaParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Antenna")
	double Stiffness = 5.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Antenna")
	double Damping = 5.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Antenna")
	double Frequency = 30.0;

	// Rotation at which the antenna is in equilibrium and around which oscillations occur.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Antenna")
	FRotator EquilibriumRotation = FRotator::ZeroRotator;
};

USTRUCT(BlueprintType)
struct FTSCamoOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camo")
	FString CamoName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camo")
	TArray<TObjectPtr<class UMaterialInstance>> MaterialInstance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camo")
	TArray<TObjectPtr<class UMaterialInstance>> MaterialInstanceDestroyed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camo")
	TArray<int32> MaterialSlotID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camo")
	TArray<int32> MaterialSlotIDDestroyed;
};
