#include "UI/TSRadarWidget.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UI/TSWidgetPaintUtils.h"

using namespace TSWidgetPaint;


UTSRadarWidget::UTSRadarWidget()
{
	SolidBrush.DrawAs = ESlateBrushDrawType::Image;
	SolidBrush.TintColor = FSlateColor(FLinearColor::White);
}

void UTSRadarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (SweepDegreesPerSecond > 0.f)
	{
		SweepAngleDeg = FMath::Fmod(SweepAngleDeg + SweepDegreesPerSecond * InDeltaTime, 360.f);
	}

	// The viewer's own tank is local, so read it straight off the actor. Using the intel copy would
	// make the scope centre lag its own hull by up to one refresh interval, and every contact with it.
	const APawn* OwnTank = GetAssignedTank();
	bHasSelf = OwnTank != nullptr;
	if (bHasSelf)
	{
		SelfLocation = OwnTank->GetActorLocation();
		SelfHeadingDeg = OwnTank->GetActorRotation().Yaw;
	}

	// Fold the latest intel into the tracked set. Contacts an update does not mention are aged out
	// rather than dropped, so one late packet does not blink the whole scope.
	for (TPair<int32, FRadarBlip>& Pair : Blips)
	{
		Pair.Value.SecondsSinceSeen += InDeltaTime;
	}

	for (const FTSRadarContact& Contact : GetIntel().Contacts)
	{
		if (Contact.bIsSelf)
		{
			continue; // Drawn at the centre from the live actor, not as a blip.
		}

		const bool bNewContact = (Blips.Find(Contact.ContactId) == nullptr);
		FRadarBlip& Blip = Blips.FindOrAdd(Contact.ContactId);

		Blip.TargetLocation = Contact.Location;
		Blip.TargetHeading = Contact.Heading;
		Blip.TeamId = Contact.TeamId;
		Blip.bHostile = Contact.bHostile;
		Blip.SecondsSinceSeen = 0.f;

		// A contact appearing for the first time must not slide in from the world origin.
		if (bNewContact)
		{
			Blip.DisplayLocation = Blip.TargetLocation;
			Blip.DisplayHeading = Blip.TargetHeading;
		}
	}

	for (TMap<int32, FRadarBlip>::TIterator It(Blips); It; ++It)
	{
		FRadarBlip& Blip = It.Value();

		if (ContactSmoothingSpeed > 0.f)
		{
			Blip.DisplayLocation = FMath::VInterpTo(Blip.DisplayLocation, Blip.TargetLocation, InDeltaTime, ContactSmoothingSpeed);
			Blip.DisplayHeading = FMath::FInterpTo(Blip.DisplayHeading, Blip.TargetHeading, InDeltaTime, ContactSmoothingSpeed);
		}
		else
		{
			Blip.DisplayLocation = Blip.TargetLocation;
			Blip.DisplayHeading = Blip.TargetHeading;
		}

		if (Blip.SecondsSinceSeen > ContactHoldSeconds)
		{
			It.RemoveCurrent();
		}
	}
}

int32 UTSRadarWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
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

	// Leave a strip at the top and bottom for the two caption rows, so the scope is never drawn over
	// its own text.
	const float CaptionBand = 16.f;
	const FVector2D Centre(Size.X * 0.5f, CaptionBand + (Size.Y - CaptionBand * 2.f) * 0.5f);
	const float Radius = FMath::Max(4.f, FMath::Min(Size.X, Size.Y - CaptionBand * 2.f) * 0.5f - 4.f);

	int32 Layer = LayerId;
	const FPaintGeometry Paint = AllottedGeometry.ToPaintGeometry();
	TArray<FVector2D> Points;

	// --- Background grid -------------------------------------------------------------------------
	if (GridDivisions > 0)
	{
		const float Spacing = Radius / GridDivisions;
		for (float X = Centre.X; X <= Size.X; X += Spacing)
		{
			Points = { FVector2D(X, 0.f), FVector2D(X, Size.Y) };
			FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None, GridColor, false, 1.f);

			const float Mirror = Centre.X - (X - Centre.X);
			if (Mirror < Centre.X)
			{
				Points = { FVector2D(Mirror, 0.f), FVector2D(Mirror, Size.Y) };
				FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None, GridColor, false, 1.f);
			}
		}
		for (float Y = Centre.Y; Y <= Size.Y; Y += Spacing)
		{
			Points = { FVector2D(0.f, Y), FVector2D(Size.X, Y) };
			FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None, GridColor, false, 1.f);

			const float Mirror = Centre.Y - (Y - Centre.Y);
			if (Mirror < Centre.Y)
			{
				Points = { FVector2D(0.f, Mirror), FVector2D(Size.X, Mirror) };
				FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None, GridColor, false, 1.f);
			}
		}
	}
	++Layer;

	// --- Scope face, rings and cross hairs --------------------------------------------------------
	FSlateDrawElement::MakeBox(OutDrawElements, Layer,
		AllottedGeometry.ToPaintGeometry(FVector2f(Radius * 2.f, Radius * 2.f),
			FSlateLayoutTransform(FVector2f(Centre - FVector2D(Radius, Radius)))),
		&SolidBrush, ESlateDrawEffect::None, FLinearColor(0.f, 0.06f, 0.f, 0.35f));
	++Layer;

	for (int32 Ring = 1; Ring <= RangeRings; ++Ring)
	{
		const float RingRadius = (Radius * Ring) / RangeRings;
		AppendCircle(Points, Centre, RingRadius, 72);
		const bool bOuter = (Ring == RangeRings);
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None,
			ScopeColor * (bOuter ? 1.f : 0.7f), true, bOuter ? 2.f : 1.f);
	}

	Points = { FVector2D(Centre.X - Radius, Centre.Y), FVector2D(Centre.X + Radius, Centre.Y) };
	FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None, ScopeColor * 0.55f, true, 1.f);
	Points = { FVector2D(Centre.X, Centre.Y - Radius), FVector2D(Centre.X, Centre.Y + Radius) };
	FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None, ScopeColor * 0.55f, true, 1.f);
	++Layer;

	// --- Sweep -----------------------------------------------------------------------------------
	// A filled wedge would need a triangle fan; a fading trail of radials gives the same afterglow
	// with nothing but line elements, and reads correctly at any size.
	const int32 TrailSteps = 26;
	for (int32 Step = TrailSteps; Step >= 0; --Step)
	{
		const float Alpha = FMath::Pow(1.f - (static_cast<float>(Step) / TrailSteps), 2.f);
		const float Angle = SweepAngleDeg - Step * 2.6f;
		Points = { Centre, PolarToLocal(Centre, Angle, Radius) };
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None,
			ScopeColor.CopyWithNewOpacity(Alpha * 0.5f), true, Step == 0 ? 2.f : 3.f);
	}
	++Layer;

	// --- Contacts --------------------------------------------------------------------------------
	const float PixelsPerCm = Radius / FMath::Max(RadarRangeCm, 1.f);
	int32 InRangeCount = 0;

	if (bHasSelf)
	{
		for (const TPair<int32, FRadarBlip>& Pair : Blips)
		{
			const FRadarBlip& Blip = Pair.Value;

			FVector Delta = Blip.DisplayLocation - SelfLocation;
			float IconHeading = Blip.DisplayHeading;
			if (bRotateWithHull)
			{
				Delta = FRotator(0.f, -SelfHeadingDeg, 0.f).RotateVector(Delta);
				IconHeading -= SelfHeadingDeg;
			}

			// UE world X is north/forward and Y is east/right; Slate Y grows downward.
			const FVector2D Offset(Delta.Y * PixelsPerCm, -Delta.X * PixelsPerCm);
			if (Offset.Size() > Radius)
			{
				// Deliberately dropped rather than clamped to the rim: a clamped blip reads as a
				// contact sitting exactly at maximum range, which is a lie.
				continue;
			}
			++InRangeCount;

			const FVector2D Position = Centre + Offset;

			// Radar persistence: a blip is brightest as the sweep crosses it and fades over the
			// revolution that follows. This is what makes the sweep look like it is doing something.
			const float BearingDeg = FMath::RadiansToDegrees(FMath::Atan2(Offset.X, -Offset.Y));
			const float SinceSweep = FMath::Fmod(SweepAngleDeg - BearingDeg + 720.f, 360.f);
			const float Persistence = FMath::Max(0.18f, 1.f - SinceSweep / 360.f);

			// An aged contact additionally fades towards being dropped.
			const float Freshness = FMath::Clamp(1.f - Blip.SecondsSinceSeen / FMath::Max(ContactHoldSeconds, 0.01f), 0.f, 1.f);
			FLinearColor Colour = Blip.bHostile ? HostileColor : FriendlyColor;
			Colour = Colour.CopyWithNewOpacity(Persistence * FMath::Max(Freshness, 0.25f));

			const float IconSize = Blip.bHostile ? 9.f : 7.f;
			FSlateDrawElement::MakeBox(OutDrawElements, Layer,
				AllottedGeometry.ToPaintGeometry(FVector2f(IconSize, IconSize),
					FSlateLayoutTransform(FVector2f(Position - FVector2D(IconSize * 0.5f, IconSize * 0.5f)))),
				&SolidBrush, ESlateDrawEffect::None, Colour);

			// Heading spur - which way that hull is pointing.
			Points = { Position, PolarToLocal(Position, IconHeading, IconSize * 1.6f) };
			FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1, Paint, Points, ESlateDrawEffect::None, Colour, true, 1.5f);

			// A bracket around hostiles, matching the tracked-target framing on a real scope.
			if (Blip.bHostile)
			{
				const float Half = IconSize;
				Points = {
					FVector2D(Position.X - Half, Position.Y - Half), FVector2D(Position.X + Half, Position.Y - Half),
					FVector2D(Position.X + Half, Position.Y + Half), FVector2D(Position.X - Half, Position.Y + Half),
					FVector2D(Position.X - Half, Position.Y - Half) };
				FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1, Paint, Points, ESlateDrawEffect::None,
					FLinearColor::White.CopyWithNewOpacity(Persistence * 0.8f), true, 1.f);
			}
		}
	}
	Layer += 2;

	// --- Own tank at the centre -------------------------------------------------------------------
	const float SelfHeadingOnScope = bRotateWithHull ? 0.f : SelfHeadingDeg;
	Points = {
		PolarToLocal(Centre, SelfHeadingOnScope, 9.f),
		PolarToLocal(Centre, SelfHeadingOnScope + 140.f, 7.f),
		PolarToLocal(Centre, SelfHeadingOnScope + 220.f, 7.f),
		PolarToLocal(Centre, SelfHeadingOnScope, 9.f) };
	FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None,
		bHasSelf ? FLinearColor::White : FLinearColor::Gray, true, 2.f);
	++Layer;

	// --- Labels -----------------------------------------------------------------------------------
	if (bShowLabels)
	{
		for (int32 Ring = 1; Ring <= RangeRings; ++Ring)
		{
			const float RingRadius = (Radius * Ring) / RangeRings;
			const float RangeMetres = (RadarRangeCm * Ring) / (RangeRings * 100.f);
			DrawCentredText(OutDrawElements, Layer, AllottedGeometry,
				FString::Printf(TEXT("%.0fm"), RangeMetres),
				FVector2D(Centre.X + RingRadius - 14.f, Centre.Y - 8.f), TinyFont, ScopeColor * 0.9f);
		}

		// Cardinal marks. With the scope heading-up these move as the hull turns, which is the whole
		// point - they are the only thing on the scope that still says where north is.
		static const TCHAR* Cardinals[] = { TEXT("N"), TEXT("E"), TEXT("S"), TEXT("W") };
		for (int32 i = 0; i < 4; ++i)
		{
			const float WorldAngle = i * 90.f;
			const float ScopeAngle = bRotateWithHull ? WorldAngle - SelfHeadingDeg : WorldAngle;
			DrawCentredText(OutDrawElements, Layer, AllottedGeometry, Cardinals[i],
				PolarToLocal(Centre, ScopeAngle, Radius - 11.f), SmallFont,
				i == 0 ? FLinearColor::White : ScopeColor);
		}

		DrawCentredText(OutDrawElements, Layer, AllottedGeometry,
			FString::Printf(TEXT("RADAR   RANGE %.0fm   CONTACTS %d"), RadarRangeCm / 100.f, InRangeCount),
			FVector2D(Size.X * 0.5f, CaptionBand * 0.5f), SmallFont, ScopeColor);

		DrawCentredText(OutDrawElements, Layer, AllottedGeometry,
			bHasSelf
				? FString::Printf(TEXT("HDG %03d   %s"), FMath::RoundToInt(FRotator::ClampAxis(SelfHeadingDeg)),
					bRotateWithHull ? TEXT("HEADING UP") : TEXT("NORTH UP"))
				: FString(TEXT("NO TANK ASSIGNED")),
			FVector2D(Size.X * 0.5f, Size.Y - CaptionBand * 0.5f), TinyFont,
			bHasSelf ? ScopeColor * 0.85f : FLinearColor(1.f, 0.4f, 0.2f));
	}
	++Layer;

	return FMath::Max(Layer,
		Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, Layer, InWidgetStyle, bParentEnabled));
}
