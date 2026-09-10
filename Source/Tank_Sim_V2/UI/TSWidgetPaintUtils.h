// Slate painting helpers shared by the natively-drawn instrument widgets (radar, attitude dial,
// vision feed).
//
// These started as a copy in each .cpp inside an anonymous namespace, which is normally the right
// way to keep a helper file-local. It is NOT safe here: UBT compiles this module as a UNITY build,
// so several .cpp files become one translation unit and two anonymous namespaces with the same
// function names collide:
//
//     error C2084: function 'PolarToLocal' already has a body
//
// The trap is that it builds fine at first. UBT's adaptive unity excludes recently-changed files
// from the unity blob, so duplicated helpers compile cleanly while you are working on them and
// break later, on a machine that has no reason to look at those files.
//
// So: one definition, in a NAMED namespace, included where it is needed.
#pragma once

#include "CoreMinimal.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"

namespace TSWidgetPaint
{
	// 0 degrees is straight up and angles run clockwise, which is how a bearing reads on both the
	// radar scope and the compass. Slate Y grows downward, hence the negated cosine.
	inline FVector2D PolarToLocal(const FVector2D& Centre, float AngleDeg, float Radius)
	{
		const float Rad = FMath::DegreesToRadians(AngleDeg);
		return Centre + FVector2D(FMath::Sin(Rad) * Radius, -FMath::Cos(Rad) * Radius);
	}

	inline void AppendCircle(TArray<FVector2D>& OutPoints, const FVector2D& Centre, float Radius, int32 Segments)
	{
		OutPoints.Reset(Segments + 1);
		for (int32 i = 0; i <= Segments; ++i)
		{
			OutPoints.Add(PolarToLocal(Centre, (360.f * i) / Segments, Radius));
		}
	}

	// Falls back to a rough per-character estimate when Slate is not up (thumbnail capture, a
	// commandlet), so a label is never simply missing.
	inline FVector2D MeasureText(const FString& Text, const FSlateFontInfo& Font)
	{
		if (FSlateApplication::IsInitialized())
		{
			return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font);
		}
		return FVector2D(Text.Len() * Font.Size * 0.55f, Font.Size * 1.2f);
	}

	// Deliberately NOT named DrawText: <windows.h> defines that as a macro, and in a unity build a
	// header that pulls it in would rewrite the call sites into DrawTextW.
	inline void DrawTextAt(FSlateWindowElementList& OutDrawElements, int32 Layer, const FGeometry& Geometry,
		const FString& Text, const FVector2D& TopLeft, const FSlateFontInfo& Font, const FLinearColor& Colour)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer,
			Geometry.ToPaintGeometry(FVector2f(MeasureText(Text, Font)), FSlateLayoutTransform(FVector2f(TopLeft))),
			Text, Font, ESlateDrawEffect::None, Colour);
	}

	inline void DrawCentredText(FSlateWindowElementList& OutDrawElements, int32 Layer, const FGeometry& Geometry,
		const FString& Text, const FVector2D& Centre, const FSlateFontInfo& Font, const FLinearColor& Colour)
	{
		DrawTextAt(OutDrawElements, Layer, Geometry, Text, Centre - MeasureText(Text, Font) * 0.5f, Font, Colour);
	}
}
