// The TX / RX lamps: a small painted pair of lights saying whether this player is sending audio on
// a net, and whether audio is arriving on it.
//
// It paints rather than composing UMG images because the useful part is the PULSE - a lamp that
// merely switches on reads as a static decoration, and one that breathes reads as live. That is
// three lines of maths in NativePaint and a flipbook or an animated material otherwise.
//
// IT DOES NOT TICK ITSELF. Its parent panel pushes state in through SetIndicatorState, including
// the pulse phase, for the same reason UTSCommanderScreenWidget drives its own instrument panels:
// one clock, one explicit ordering, and no dependence on Slate ticking a nested UUserWidget - which
// is a behaviour this project has an open, unresolved question about (see CLAUDE.md).
//
// IT HAS NO SIZE OF ITS OWN either. A paint-only widget has zero desired size and would collapse
// inside a box; the parent is expected to wrap it in a USizeBox. Putting that decision in the
// parent is correct - how big a lamp should be is a layout question, and layout is the parent's.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TSVoiceIndicatorWidget.generated.h"

UCLASS()
class UTSVoiceIndicatorWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UTSVoiceIndicatorWidget(const FObjectInitializer& ObjectInitializer);

	// Called by the owning panel every tick. PulseSeconds is a free-running clock, not a delta - the
	// lamp derives its own phase from it so several lamps on one panel pulse together rather than
	// each drifting off on its own accumulator.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Voice")
	void SetIndicatorState(bool bInTransmitting, bool bInReceiving, float PulseSeconds);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	bool IsTransmitting() const { return bTransmitting; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	bool IsReceiving() const { return bReceiving; }

	// Draw the TX lamp at all. The Commander's channel rows show both; a row that can only ever
	// receive (a commander the host has selected, seen from the host's panel) shows only RX.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Voice")
	bool bShowTransmit = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Voice")
	bool bShowReceive = true;

	// Letters beside each lamp. Off for a tight row where the colour alone carries the meaning.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Voice")
	bool bShowLabels = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Voice", meta = (ClampMin = "0.1"))
	float PulseHz = 2.2f;

	// Sizes the two lamps against the widget's own height, so one property drives the whole thing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Voice", meta = (ClampMin = "0.05", ClampMax = "0.5"))
	float LampRadiusFraction = 0.3f;

	// Sending. Amber-red, the colour a transmit light is on every radio ever built.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Voice")
	FLinearColor TransmitColour = FLinearColor(1.f, 0.35f, 0.12f, 1.f);

	// Receiving. Green, and deliberately NOT the same hue as transmit: the whole value of the pair
	// is being able to tell at a glance which way the audio is going.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Voice")
	FLinearColor ReceiveColour = FLinearColor(0.25f, 0.95f, 0.35f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Voice")
	FLinearColor IdleColour = FLinearColor(0.16f, 0.17f, 0.19f, 1.f);

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	// Draws one lamp: a dim ring that is always present so the reader can see there IS a lamp there,
	// filled and haloed while active. A lamp that vanishes when idle makes an inactive channel look
	// broken rather than quiet.
	void PaintLamp(FSlateWindowElementList& OutDrawElements, int32 Layer, const FGeometry& Geometry,
		const FVector2D& Centre, float Radius, const FLinearColor& ActiveColour, bool bActive,
		const FString& Label) const;

	bool bTransmitting = false;
	bool bReceiving = false;
	float Pulse = 0.f;
};
