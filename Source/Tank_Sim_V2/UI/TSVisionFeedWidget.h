// The crew station's periscope feed, plus the day / night-vision / thermal switch.
//
// The image is the render target that ATSTankControllerBase's station SceneCaptureComponent2D is
// already drawing into - this widget adds no capture of its own. A scene capture is close to a whole
// extra render of the world, so a second one purely to fill a HUD panel would double the cost of
// looking at it.
//
// It always shows the LOCAL player's own seat, and that is not a limitation to be worked around: the
// tank deliberately runs exactly one capture per machine, for the seat the person at that machine is
// sitting in. Pointing this at another station would display whatever stale frame that render target
// last held - a frozen image, which reads as a bug.
#pragma once

#include "CoreMinimal.h"
#include "Core/TSTypes.h"
#include "Styling/SlateBrush.h"
#include "UI/TSHUDWidgetBase.h"
#include "TSVisionFeedWidget.generated.h"

UCLASS()
class UTSVisionFeedWidget : public UTSHUDWidgetBase
{
	GENERATED_BODY()

public:
	// Driven by UTSCommanderScreenWidget - see UTSRadarWidget::RefreshInstrument for why.
	void RefreshInstrument(float InDeltaTime);

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	// Day -> Night Vision -> Thermal -> Day. Local only: this changes post processing on one capture
	// on one machine and is neither replicated nor server-validated, because it reveals nothing the
	// player's own periscope was not already rendering.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Vision")
	ETSVisionMode CycleVisionMode();

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Vision")
	void SetVisionMode(ETSVisionMode NewMode);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Vision")
	ETSVisionMode GetVisionMode() const;

	// Null when the local player holds no seat on this tank, when that seat has no capture component,
	// or when the capture has no TextureTarget assigned. All three present as a black panel, so the
	// widget says which it is rather than leaving it to be guessed.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Vision")
	class UTextureRenderTarget2D* GetFeedRenderTarget() const;

	// Clicking the feed cycles the vision mode. Only reachable when the widget is hit-testable -
	// a HUD added with SelfHitTestInvisible will never see the click, which is why the exec command
	// TSVision and the BlueprintCallable above exist alongside it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Vision")
	bool bAllowClickToCycle = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Vision")
	bool bDrawReticle = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Vision")
	FLinearColor FrameColor = FLinearColor(0.15f, 1.f, 0.25f, 1.f);

private:
	// Points at the station render target. Rebuilt only when the target changes, because assigning a
	// resource object every frame churns the brush's resource handle for nothing.
	FSlateBrush FeedBrush;
	FSlateBrush SolidBrush;

	TWeakObjectPtr<class UTextureRenderTarget2D> BoundTarget;
};
