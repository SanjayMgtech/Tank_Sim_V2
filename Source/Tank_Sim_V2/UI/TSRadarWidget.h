// Commander radar - a circular, animated scope drawn entirely in Slate.
//
// Nothing here is an asset. The scope, grid, rings, sweep and blips are all painted procedurally,
// which is deliberate: a radar is thirty lines of geometry and a sweep angle, and expressing it as a
// widget tree of Images would need art nobody has authored, would not scale to an arbitrary contact
// count, and could not draw a sweep at all without a flipbook.
//
// Data comes from UTSTankCommanderComponent's replicated intel (server-built, permission-filtered),
// never from walking the local world - a client must not be able to see contacts it was not sent.
#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Core/TSTypes.h"
#include "UI/TSCommanderHUDWidget.h"
#include "TSRadarWidget.generated.h"

UCLASS()
class UTSRadarWidget : public UTSCommanderHUDWidget
{
	GENERATED_BODY()

public:
	UTSRadarWidget();

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// How many contacts the scope is currently tracking. Exposed so a container widget can caption
	// the panel without duplicating the intel plumbing.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Radar")
	int32 GetTrackedContactCount() const { return Blips.Num(); }

	// Radius of the outer ring, in cm of world. The default is 1 km, which comfortably contains a
	// tank engagement; shrink it and the same contacts spread out across the scope.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Radar", meta = (ClampMin = "1000.0"))
	float RadarRangeCm = 100000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Radar", meta = (ClampMin = "0.0"))
	float SweepDegreesPerSecond = 90.f;

	// Heading-up (the scope turns with the hull, contacts hold still relative to the tank) versus
	// north-up. Heading-up is the default because every bearing read off it is then a bearing the
	// Driver can be given directly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Radar")
	bool bRotateWithHull = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Radar", meta = (ClampMin = "1", ClampMax = "8"))
	int32 RangeRings = 3;

	// Background grid squares across the half-width. 0 hides the grid.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Radar", meta = (ClampMin = "0", ClampMax = "24"))
	int32 GridDivisions = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Radar")
	bool bShowLabels = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Radar")
	FLinearColor ScopeColor = FLinearColor(0.15f, 1.f, 0.25f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Radar")
	FLinearColor GridColor = FLinearColor(0.9f, 0.12f, 0.1f, 0.55f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Radar")
	FLinearColor FriendlyColor = FLinearColor(0.25f, 1.f, 0.35f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Radar")
	FLinearColor HostileColor = FLinearColor(1.f, 0.16f, 0.12f, 1.f);

	// How fast a blip slides towards its newly-received position. Intel arrives at a few Hz, so
	// without this every blip teleports on each update.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Radar", meta = (ClampMin = "0.0"))
	float ContactSmoothingSpeed = 8.f;

	// A blip the intel stops mentioning fades out over this long before being dropped, rather than
	// vanishing the instant one update omits it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Radar", meta = (ClampMin = "0.1"))
	float ContactHoldSeconds = 3.f;

private:
	// One tracked contact. Display* is what is painted, Target* is what the last intel update said;
	// the gap between them is the interpolation.
	struct FRadarBlip
	{
		FVector DisplayLocation = FVector::ZeroVector;
		FVector TargetLocation = FVector::ZeroVector;
		float DisplayHeading = 0.f;
		float TargetHeading = 0.f;
		ETSTeamId TeamId = ETSTeamId::None;
		bool bHostile = false;
		// Seconds since this contact last appeared in an intel update.
		float SecondsSinceSeen = 0.f;
	};

	// Keyed by FTSRadarContact::ContactId, which is why that field has to be stable across refreshes.
	TMap<int32, FRadarBlip> Blips;

	float SweepAngleDeg = 0.f;

	// The viewer's own tank, read live from the actor rather than from intel - it is local, so there
	// is nothing to wait for and nothing to smooth.
	FVector SelfLocation = FVector::ZeroVector;
	float SelfHeadingDeg = 0.f;
	bool bHasSelf = false;

	// White; every draw tints it. Held as a member because FSlateDrawElement stores the pointer.
	// FSlateBrush, not FSlateColorBrush: the latter has no default constructor and UHT generates one
	// for the vtable helper, so it cannot be a member of a UCLASS.
	FSlateBrush SolidBrush;
};
