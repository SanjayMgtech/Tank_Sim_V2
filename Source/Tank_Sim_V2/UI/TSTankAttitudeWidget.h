// Hull heading and turret traverse, drawn as one instrument.
//
// The layering is the point, and it is not arbitrary: the HULL is painted static and facing up, the
// COMPASS behind it counter-rotates by the hull's yaw, and the LAUNCHER on top rotates by the
// turret's yaw RELATIVE to the hull. So the tank never moves on screen, the world turns around it,
// and the gun swings across it - which is what the crew actually experience from inside.
//
// Drawing the hull static is what makes the turret readable. Rotate both and the eye cannot tell a
// 20 degree traverse from a 20 degree hull turn.
//
// Art is optional: leave the texture slots empty and the instrument draws itself. That is a
// fallback, not a preference - drop a hull and turret texture in and they replace the vector shapes.
#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UI/TSHUDWidgetBase.h"
#include "TSTankAttitudeWidget.generated.h"

UCLASS()
class UTSTankAttitudeWidget : public UTSHUDWidgetBase
{
	GENERATED_BODY()

public:
	// Driven by UTSCommanderScreenWidget - see UTSRadarWidget::RefreshInstrument for why.
	void RefreshInstrument(float InDeltaTime);

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// World yaw of the hull, degrees. Drives the compass.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Attitude")
	float GetHullHeading() const { return HullYawDeg; }

	// Turret yaw relative to the hull, degrees. Drives the launcher.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Attitude")
	float GetTurretTraverse() const { return TurretYawDeg; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Attitude")
	float GetGunElevation() const { return GunPitchDeg; }

	// Optional art. Each is drawn centred on the instrument and rotated by the angle its layer owns;
	// authored facing UP (towards -Y in the image), which is 0 degrees.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Attitude")
	TObjectPtr<class UTexture2D> CompassTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Attitude")
	TObjectPtr<class UTexture2D> HullTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Attitude")
	TObjectPtr<class UTexture2D> LauncherTexture;

	// Fraction of the panel's short side each layer occupies.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Attitude", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float CompassScale = 0.94f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Attitude", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float HullScale = 0.42f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Attitude", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float LauncherScale = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Attitude")
	FLinearColor DialColor = FLinearColor(0.15f, 1.f, 0.25f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Attitude")
	FLinearColor HullColor = FLinearColor(0.55f, 0.75f, 0.55f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Attitude")
	FLinearColor LauncherColor = FLinearColor(1.f, 0.82f, 0.2f, 1.f);

	// Display smoothing, in degrees per second of catch-up rate. The underlying values are already
	// rate-limited by the turret code, so this only takes the step out of a replicated update
	// arriving between frames. 0 shows the raw value.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Attitude", meta = (ClampMin = "0.0"))
	float DisplayInterpSpeed = 14.f;

private:
	// Read every frame from the assigned tank, so this is as live as the tank's own animation.
	float HullYawDeg = 0.f;
	float TurretYawDeg = 0.f;
	float GunPitchDeg = 0.f;
	bool bHasTank = false;

	// Brushes are rebuilt each tick from the texture slots; FSlateDrawElement stores the pointer, so
	// they have to outlive the paint call.
	FSlateBrush CompassBrush;
	FSlateBrush HullBrush;
	FSlateBrush LauncherBrush;
	FSlateBrush SolidBrush;
};
