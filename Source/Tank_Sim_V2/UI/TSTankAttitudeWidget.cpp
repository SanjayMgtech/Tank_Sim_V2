#include "UI/TSTankAttitudeWidget.h"

#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Tank/TSTankControllerBase.h"
#include "UI/TSWidgetPaintUtils.h"

using namespace TSWidgetPaint;

namespace
{
	// Shortest-way-round interpolation. FInterpTo on raw degrees would take the long way round every
	// time the value crosses +/-180 and the dial would spin backwards through a full turn.
	float InterpAngleDegrees(float Current, float Target, float DeltaTime, float Speed)
	{
		if (Speed <= 0.f)
		{
			return Target;
		}
		const float Delta = FMath::UnwindDegrees(Target - Current);
		return Current + Delta * FMath::Clamp(DeltaTime * Speed, 0.f, 1.f);
	}


	// Centred, rotated about its own middle. Angle in degrees, clockwise, 0 = as authored.
	void DrawRotatedSprite(FSlateWindowElementList& OutDrawElements, int32 Layer, const FGeometry& Geometry,
		const FSlateBrush* Brush, const FVector2D& Centre, const FVector2D& BoxSize, float AngleDeg,
		const FLinearColor& Tint)
	{
		FSlateDrawElement::MakeRotatedBox(OutDrawElements, Layer,
			Geometry.ToPaintGeometry(FVector2f(BoxSize), FSlateLayoutTransform(FVector2f(Centre - BoxSize * 0.5f))),
			Brush, ESlateDrawEffect::None, FMath::DegreesToRadians(AngleDeg),
			FVector2f(BoxSize * 0.5f), FSlateDrawElement::RelativeToElement, Tint);
	}

	void ApplyTextureToBrush(FSlateBrush& Brush, UTexture2D* Texture)
	{
		if (Brush.GetResourceObject() != Texture)
		{
			Brush.SetResourceObject(Texture);
			Brush.DrawAs = Texture ? ESlateBrushDrawType::Image : ESlateBrushDrawType::NoDrawType;
			Brush.TintColor = FSlateColor(FLinearColor::White);
		}
	}
}

void UTSTankAttitudeWidget::RefreshInstrument(float InDeltaTime)
{
	ApplyTextureToBrush(CompassBrush, CompassTexture);
	ApplyTextureToBrush(HullBrush, HullTexture);
	ApplyTextureToBrush(LauncherBrush, LauncherTexture);
	if (SolidBrush.DrawAs != ESlateBrushDrawType::Image)
	{
		SolidBrush.DrawAs = ESlateBrushDrawType::Image;
		SolidBrush.TintColor = FSlateColor(FLinearColor::White);
	}

	const ATSTankControllerBase* Tank = Cast<ATSTankControllerBase>(GetAssignedTank());
	bHasTank = Tank != nullptr;
	if (!bHasTank)
	{
		return;
	}

	// GetMainGunAimRotation is the ACHIEVED aim in the tank's own space - the same arrays the AnimBP
	// draws the barrel from - so the dial cannot disagree with what the player sees on the tank.
	const FRotator Aim = Tank->GetMainGunAimRotation();

	HullYawDeg = FMath::UnwindDegrees(InterpAngleDegrees(HullYawDeg, Tank->GetActorRotation().Yaw, InDeltaTime, DisplayInterpSpeed));
	TurretYawDeg = FMath::UnwindDegrees(InterpAngleDegrees(TurretYawDeg, Aim.Yaw, InDeltaTime, DisplayInterpSpeed));
	GunPitchDeg = InterpAngleDegrees(GunPitchDeg, Aim.Pitch, InDeltaTime, DisplayInterpSpeed);
}

int32 UTSTankAttitudeWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	if (Size.X < 8.f || Size.Y < 8.f)
	{
		return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

	const FSlateFontInfo SmallFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9);
	const FSlateFontInfo TinyFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8);

	const float CaptionBand = 16.f;
	const FVector2D Centre(Size.X * 0.5f, CaptionBand + (Size.Y - CaptionBand * 2.f) * 0.5f);
	const float Extent = FMath::Max(8.f, FMath::Min(Size.X, Size.Y - CaptionBand * 2.f) * 0.5f - 2.f);

	int32 Layer = LayerId;
	const FPaintGeometry Paint = AllottedGeometry.ToPaintGeometry();
	TArray<FVector2D> Points;

	// =============================================================================================
	// Layer 1 - the COMPASS. Counter-rotates by the hull's yaw: a world bearing B sits at B - yaw on
	// the dial, so the ring turns whenever the tank turns while the tank itself stays put.
	// =============================================================================================
	const float CompassRadius = Extent * CompassScale;

	if (CompassTexture)
	{
		DrawRotatedSprite(OutDrawElements, Layer, AllottedGeometry, &CompassBrush, Centre,
			FVector2D(CompassRadius * 2.f), -HullYawDeg, FLinearColor::White);
	}
	else
	{
		for (int32 Ring = 0; Ring < 2; ++Ring)
		{
			const float R = Ring == 0 ? CompassRadius : CompassRadius * 0.8f;
			Points.Reset(73);
			for (int32 i = 0; i <= 72; ++i)
			{
				Points.Add(PolarToLocal(Centre, i * 5.f, R));
			}
			FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None,
				DialColor * (Ring == 0 ? 1.f : 0.45f), true, Ring == 0 ? 2.f : 1.f);
		}

		for (int32 Bearing = 0; Bearing < 360; Bearing += 10)
		{
			const float DialAngle = Bearing - HullYawDeg;
			const bool bMajor = (Bearing % 90) == 0;
			const bool bMedium = (Bearing % 30) == 0;
			const float TickLength = bMajor ? CompassRadius * 0.20f : (bMedium ? CompassRadius * 0.13f : CompassRadius * 0.07f);

			Points = {
				PolarToLocal(Centre, DialAngle, CompassRadius),
				PolarToLocal(Centre, DialAngle, CompassRadius - TickLength) };
			FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None,
				DialColor * (bMajor ? 1.f : 0.6f), true, bMajor ? 2.f : 1.f);

			if (bMajor)
			{
				static const TCHAR* Cardinals[] = { TEXT("N"), TEXT("E"), TEXT("S"), TEXT("W") };
				DrawCentredText(OutDrawElements, Layer + 1, AllottedGeometry, Cardinals[Bearing / 90],
					PolarToLocal(Centre, DialAngle, CompassRadius - TickLength - 9.f), SmallFont,
					Bearing == 0 ? FLinearColor(1.f, 0.35f, 0.25f) : DialColor);
			}
		}
	}
	Layer += 2;

	// =============================================================================================
	// Layer 2 - the HULL. Static, facing up, always. This is the fixed reference everything else is
	// read against.
	// =============================================================================================
	const float HullHalfHeight = Extent * HullScale;
	const float HullHalfWidth = HullHalfHeight * 0.62f;

	if (HullTexture)
	{
		DrawRotatedSprite(OutDrawElements, Layer, AllottedGeometry, &HullBrush, Centre,
			FVector2D(HullHalfWidth * 2.f, HullHalfHeight * 2.f), 0.f, FLinearColor::White);
	}
	else
	{
		// Two tracks and a hull between them.
		const float TrackWidth = HullHalfWidth * 0.42f;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const float X = Centre.X + (Side == 0 ? -HullHalfWidth : HullHalfWidth - TrackWidth);
			FSlateDrawElement::MakeBox(OutDrawElements, Layer,
				AllottedGeometry.ToPaintGeometry(FVector2f(TrackWidth, HullHalfHeight * 2.f),
					FSlateLayoutTransform(FVector2f(X, Centre.Y - HullHalfHeight))),
				&SolidBrush, ESlateDrawEffect::None, HullColor * 0.55f);
		}

		FSlateDrawElement::MakeBox(OutDrawElements, Layer + 1,
			AllottedGeometry.ToPaintGeometry(FVector2f(HullHalfWidth * 1.25f, HullHalfHeight * 1.8f),
				FSlateLayoutTransform(FVector2f(Centre - FVector2D(HullHalfWidth * 0.625f, HullHalfHeight * 0.9f)))),
			&SolidBrush, ESlateDrawEffect::None, HullColor);

		// Glacis - a nose so the static hull still reads as pointing somewhere.
		Points = {
			FVector2D(Centre.X - HullHalfWidth * 0.62f, Centre.Y - HullHalfHeight * 0.9f),
			FVector2D(Centre.X, Centre.Y - HullHalfHeight * 1.15f),
			FVector2D(Centre.X + HullHalfWidth * 0.62f, Centre.Y - HullHalfHeight * 0.9f) };
		FSlateDrawElement::MakeLines(OutDrawElements, Layer + 2, Paint, Points, ESlateDrawEffect::None,
			HullColor * 1.4f, true, 2.f);
	}
	Layer += 3;

	// =============================================================================================
	// Layer 3 - the LAUNCHER, on top of the hull, rotated by the turret's traverse RELATIVE to the
	// hull. Because the hull below it is static, that relative angle is exactly what the eye reads.
	// =============================================================================================
	const float TurretRadius = Extent * LauncherScale;

	if (LauncherTexture)
	{
		DrawRotatedSprite(OutDrawElements, Layer, AllottedGeometry, &LauncherBrush, Centre,
			FVector2D(TurretRadius * 2.f), TurretYawDeg, FLinearColor::White);
	}
	else
	{
		// Turret body.
		const FVector2D BodySize(TurretRadius * 0.62f, TurretRadius * 0.75f);
		DrawRotatedSprite(OutDrawElements, Layer, AllottedGeometry, &SolidBrush, Centre, BodySize,
			TurretYawDeg, LauncherColor * 0.8f);

		// Barrel, from the turret centre out along the traverse.
		const FVector2D Muzzle = PolarToLocal(Centre, TurretYawDeg, TurretRadius);
		Points = { PolarToLocal(Centre, TurretYawDeg, TurretRadius * 0.2f), Muzzle };
		FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1, Paint, Points, ESlateDrawEffect::None,
			LauncherColor, true, 5.f);

		// Muzzle brake.
		Points = {
			PolarToLocal(Muzzle, TurretYawDeg + 90.f, TurretRadius * 0.11f),
			PolarToLocal(Muzzle, TurretYawDeg - 90.f, TurretRadius * 0.11f) };
		FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1, Paint, Points, ESlateDrawEffect::None,
			LauncherColor, true, 4.f);
	}

	// Traverse cue on the dial, so a large traverse is legible even when the barrel overlaps a tick.
	Points = {
		PolarToLocal(Centre, TurretYawDeg, CompassRadius * 0.82f),
		PolarToLocal(Centre, TurretYawDeg, CompassRadius) };
	FSlateDrawElement::MakeLines(OutDrawElements, Layer + 2, Paint, Points, ESlateDrawEffect::None,
		LauncherColor, true, 3.f);
	Layer += 3;

	// Fixed index at 12 o'clock - the hull's own heading, read against the moving dial.
	Points = {
		FVector2D(Centre.X - 6.f, Centre.Y - CompassRadius - 7.f),
		FVector2D(Centre.X + 6.f, Centre.Y - CompassRadius - 7.f),
		FVector2D(Centre.X, Centre.Y - CompassRadius + 3.f),
		FVector2D(Centre.X - 6.f, Centre.Y - CompassRadius - 7.f) };
	FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None,
		FLinearColor::White, true, 2.f);
	++Layer;

	// --- Readouts ---------------------------------------------------------------------------------
	DrawCentredText(OutDrawElements, Layer, AllottedGeometry,
		bHasTank
			? FString::Printf(TEXT("HULL %03d"), FMath::RoundToInt(FRotator::ClampAxis(HullYawDeg)))
			: FString(TEXT("NO TANK ASSIGNED")),
		FVector2D(Size.X * 0.5f, CaptionBand * 0.5f), SmallFont,
		bHasTank ? DialColor : FLinearColor(1.f, 0.4f, 0.2f));

	if (bHasTank)
	{
		DrawCentredText(OutDrawElements, Layer, AllottedGeometry,
			FString::Printf(TEXT("TRAVERSE %+04d    ELEV %+03d"),
				FMath::RoundToInt(TurretYawDeg), FMath::RoundToInt(GunPitchDeg)),
			FVector2D(Size.X * 0.5f, Size.Y - CaptionBand * 0.5f), TinyFont, LauncherColor);
	}
	++Layer;

	return FMath::Max(Layer,
		Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, Layer, InWidgetStyle, bParentEnabled));
}
