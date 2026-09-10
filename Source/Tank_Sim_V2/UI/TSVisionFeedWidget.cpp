#include "UI/TSVisionFeedWidget.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Player/TSTankPlayerState.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Tank/TSTankControllerBase.h"
#include "UI/TSWidgetPaintUtils.h"

using namespace TSWidgetPaint;

namespace
{

	FString VisionModeLabel(ETSVisionMode Mode)
	{
		switch (Mode)
		{
		case ETSVisionMode::NightVision: return TEXT("NIGHT VISION");
		case ETSVisionMode::Thermal:     return TEXT("THERMAL");
		default:                         return TEXT("DAY");
		}
	}
}

void UTSVisionFeedWidget::RefreshInstrument(float InDeltaTime)
{
	if (SolidBrush.DrawAs != ESlateBrushDrawType::Image)
	{
		SolidBrush.DrawAs = ESlateBrushDrawType::Image;
		SolidBrush.TintColor = FSlateColor(FLinearColor::White);
	}

	UTextureRenderTarget2D* Target = GetFeedRenderTarget();
	if (BoundTarget.Get() != Target)
	{
		BoundTarget = Target;
		FeedBrush.SetResourceObject(Target);
		FeedBrush.DrawAs = Target ? ESlateBrushDrawType::Image : ESlateBrushDrawType::NoDrawType;
		FeedBrush.TintColor = FSlateColor(FLinearColor::White);
		if (Target)
		{
			FeedBrush.ImageSize = FVector2f(Target->SizeX, Target->SizeY);
		}
	}
}

UTextureRenderTarget2D* UTSVisionFeedWidget::GetFeedRenderTarget() const
{
	const ATSTankControllerBase* Tank = Cast<ATSTankControllerBase>(GetAssignedTank());
	const ATSTankPlayerState* PS = GetTankPlayerState();
	if (!Tank || !PS)
	{
		return nullptr;
	}

	return Tank->GetCrewViewRenderTarget(PS->GetCrewRole());
}

ETSVisionMode UTSVisionFeedWidget::GetVisionMode() const
{
	const ATSTankControllerBase* Tank = Cast<ATSTankControllerBase>(GetAssignedTank());
	return Tank ? Tank->GetCrewViewVisionMode() : ETSVisionMode::Normal;
}

void UTSVisionFeedWidget::SetVisionMode(ETSVisionMode NewMode)
{
	if (ATSTankControllerBase* Tank = Cast<ATSTankControllerBase>(GetAssignedTank()))
	{
		Tank->SetCrewViewVisionMode(NewMode);
	}
}

ETSVisionMode UTSVisionFeedWidget::CycleVisionMode()
{
	if (ATSTankControllerBase* Tank = Cast<ATSTankControllerBase>(GetAssignedTank()))
	{
		return Tank->CycleCrewViewVisionMode();
	}
	return ETSVisionMode::Normal;
}

FReply UTSVisionFeedWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bAllowClickToCycle && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		CycleVisionMode();
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

int32 UTSVisionFeedWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	if (Size.X < 8.f || Size.Y < 8.f)
	{
		return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

	const FSlateFontInfo SmallFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10);
	const FSlateFontInfo TinyFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8);
	const ETSVisionMode Mode = GetVisionMode();

	int32 Layer = LayerId;
	const FPaintGeometry Paint = AllottedGeometry.ToPaintGeometry();
	const FPaintGeometry FullBox = AllottedGeometry.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(FVector2f(0.f, 0.f)));
	TArray<FVector2D> Points;

	// Unlit backing, so an unassigned or not-yet-captured feed is black rather than transparent.
	FSlateDrawElement::MakeBox(OutDrawElements, Layer, FullBox, &SolidBrush, ESlateDrawEffect::None,
		FLinearColor(0.f, 0.f, 0.f, 0.9f));
	++Layer;

	if (BoundTarget.IsValid())
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, FullBox, &FeedBrush, ESlateDrawEffect::None, FLinearColor::White);
	}
	else
	{
		// Say WHICH of the three ways this can be empty applies, because all three look identical.
		const ATSTankControllerBase* Tank = Cast<ATSTankControllerBase>(GetAssignedTank());
		const ATSTankPlayerState* PS = GetTankPlayerState();
		const ETSCrewRole CrewRole = PS ? PS->GetCrewRole() : ETSCrewRole::None;

		FString Reason;
		if (!Tank)
		{
			Reason = TEXT("no tank assigned");
		}
		else if (CrewRole == ETSCrewRole::None)
		{
			Reason = TEXT("no crew seat assigned");
		}
		else
		{
			Reason = FString::Printf(
				TEXT("no render target for the %s station - add a SceneCaptureComponent2D to the tank\n")
				TEXT("Blueprint, give it a TextureTarget, and name it in CrewViewCaptureComponents"),
				*UTSTypeUtils::CrewRoleToString(CrewRole));
		}

		DrawTextAt(OutDrawElements, Layer + 1, AllottedGeometry, TEXT("NO FEED"),
			FVector2D(12.f, Size.Y * 0.5f - 18.f), SmallFont, FLinearColor(1.f, 0.4f, 0.2f));
		DrawTextAt(OutDrawElements, Layer + 1, AllottedGeometry, Reason,
			FVector2D(12.f, Size.Y * 0.5f), TinyFont, FLinearColor(0.7f, 0.7f, 0.7f));
	}
	Layer += 2;

	// --- Overlay ----------------------------------------------------------------------------------
	if (bDrawReticle && BoundTarget.IsValid())
	{
		const FVector2D Centre = Size * 0.5f;
		const float Arm = FMath::Min(Size.X, Size.Y) * 0.06f;
		const float Gap = Arm * 0.35f;

		Points = { FVector2D(Centre.X - Arm, Centre.Y), FVector2D(Centre.X - Gap, Centre.Y) };
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None, FrameColor, true, 1.5f);
		Points = { FVector2D(Centre.X + Gap, Centre.Y), FVector2D(Centre.X + Arm, Centre.Y) };
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None, FrameColor, true, 1.5f);
		Points = { FVector2D(Centre.X, Centre.Y - Arm), FVector2D(Centre.X, Centre.Y - Gap) };
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None, FrameColor, true, 1.5f);
		Points = { FVector2D(Centre.X, Centre.Y + Gap), FVector2D(Centre.X, Centre.Y + Arm) };
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None, FrameColor, true, 1.5f);
	}

	// Border.
	Points = {
		FVector2D(1.f, 1.f), FVector2D(Size.X - 1.f, 1.f),
		FVector2D(Size.X - 1.f, Size.Y - 1.f), FVector2D(1.f, Size.Y - 1.f), FVector2D(1.f, 1.f) };
	FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Points, ESlateDrawEffect::None, FrameColor * 0.8f, true, 1.5f);

	// Mode readout, tinted the way the mode looks so it is identifiable at a glance.
	const FLinearColor ModeColour =
		Mode == ETSVisionMode::NightVision ? FLinearColor(0.3f, 1.f, 0.4f) :
		Mode == ETSVisionMode::Thermal ? FLinearColor(1.f, 0.95f, 0.9f) : FrameColor;

	DrawTextAt(OutDrawElements, Layer, AllottedGeometry, VisionModeLabel(Mode), FVector2D(10.f, 8.f), SmallFont, ModeColour);
	DrawTextAt(OutDrawElements, Layer, AllottedGeometry, TEXT("click / TSVision to switch"),
		FVector2D(10.f, Size.Y - 18.f), TinyFont, FrameColor * 0.6f);
	++Layer;

	return FMath::Max(Layer,
		Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, Layer, InWidgetStyle, bParentEnabled));
}
