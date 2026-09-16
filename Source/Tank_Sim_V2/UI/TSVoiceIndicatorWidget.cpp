#include "UI/TSVoiceIndicatorWidget.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "UI/TSWidgetPaintUtils.h"

using namespace TSWidgetPaint;

UTSVoiceIndicatorWidget::UTSVoiceIndicatorWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// HitTestInvisible: a lamp is a readout, and it sits inside rows whose buttons must stay
	// clickable through it.
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UTSVoiceIndicatorWidget::SetIndicatorState(bool bInTransmitting, bool bInReceiving, float PulseSeconds)
{
	bTransmitting = bInTransmitting;
	bReceiving = bInReceiving;
	Pulse = PulseSeconds;
}

void UTSVoiceIndicatorWidget::PaintLamp(FSlateWindowElementList& OutDrawElements, int32 Layer, const FGeometry& Geometry,
	const FVector2D& Centre, float Radius, const FLinearColor& ActiveColour, bool bActive, const FString& Label) const
{
	// 0 at the bottom of the breath, 1 at the top. Never reaches 0: a lamp that blinks fully off is
	// read as flickering hardware rather than as live audio.
	const float Breath = bActive ? (0.62f + 0.38f * FMath::Sin(Pulse * PulseHz * 2.f * PI)) : 0.f;
	const FLinearColor Body = bActive ? ActiveColour * (0.55f + 0.45f * Breath) : IdleColour;

	TArray<FVector2D> Points;

	// Halo first, underneath, so the lamp face sits on top of its own glow.
	if (bActive)
	{
		FLinearColor Halo = ActiveColour;
		Halo.A = 0.25f * Breath;
		AppendCircle(Points, Centre, Radius * 1.9f, 20);
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Halo, true, Radius * 0.9f);
	}

	// The face, drawn as a thick-stroked circle rather than a filled one - Slate has no filled-disc
	// primitive without a brush, and a stroke of radius/2 at radius/2 is a disc.
	AppendCircle(Points, Centre, Radius * 0.5f, 16);
	FSlateDrawElement::MakeLines(OutDrawElements, Layer + 1, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Body, true, Radius);

	// The rim is always drawn, active or not, so an idle channel reads as present-and-quiet.
	AppendCircle(Points, Centre, Radius, 20);
	FSlateDrawElement::MakeLines(OutDrawElements, Layer + 2, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None,
		bActive ? ActiveColour : FLinearColor(0.35f, 0.36f, 0.40f, 1.f), true, 1.2f);

	if (bShowLabels && !Label.IsEmpty())
	{
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), FMath::Max(7, FMath::RoundToInt(Radius * 1.1f)));
		const FLinearColor TextColour = bActive ? ActiveColour : FLinearColor(0.45f, 0.46f, 0.50f, 1.f);
		DrawTextAt(OutDrawElements, Layer + 3, Geometry, Label,
			FVector2D(Centre.X + Radius * 1.4f, Centre.Y - Radius * 0.85f), Font, TextColour);
	}
}

int32 UTSVoiceIndicatorWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	if (Size.X <= 2.f || Size.Y <= 2.f)
	{
		// No geometry yet, or the parent forgot the USizeBox this widget needs. Painting into a
		// degenerate rect would draw a smear rather than nothing, which is worse than nothing.
		return Layer;
	}

	const float Radius = FMath::Max(2.f, Size.Y * LampRadiusFraction);

	// Each visible lamp gets an equal share of the width, so a receive-only row centres its single
	// lamp instead of leaving a hole where the transmit one would have been.
	const int32 NumLamps = (bShowTransmit ? 1 : 0) + (bShowReceive ? 1 : 0);
	if (NumLamps == 0)
	{
		return Layer;
	}

	const float Slice = Size.X / NumLamps;
	float NextCentreX = Slice * 0.5f;

	// Biased left within its slice when labelled, because the label is drawn to the right of the
	// lamp and would otherwise run off the end of the row.
	const float LampOffset = bShowLabels ? -Radius * 1.1f : 0.f;

	if (bShowTransmit)
	{
		PaintLamp(OutDrawElements, Layer, AllottedGeometry, FVector2D(NextCentreX + LampOffset, Size.Y * 0.5f),
			Radius, TransmitColour, bTransmitting, TEXT("TX"));
		NextCentreX += Slice;
	}

	if (bShowReceive)
	{
		PaintLamp(OutDrawElements, Layer, AllottedGeometry, FVector2D(NextCentreX + LampOffset, Size.Y * 0.5f),
			Radius, ReceiveColour, bReceiving, TEXT("RX"));
	}

	return Layer + 4;
}
