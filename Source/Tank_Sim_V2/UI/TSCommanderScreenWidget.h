// The Commander's screen: periscope feed, radar and hull/turret attitude, split across one panel.
//
// The layout is built in C++ rather than authored as a WBP widget tree. That is deliberate. All
// three panels paint themselves procedurally, so a WBP would contribute nothing but a
// HorizontalBox and two slots - and it would then be an asset that has to exist, be found, and be
// kept in sync with the C++ that drives it.
//
// A WBP subclass is still supported and still wins: if the Blueprint provides its own widget tree,
// this class leaves it alone and simply binds to whichever of the three panels it finds inside. So
// the default costs no asset, and hand-authoring the layout later costs no code change.
#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "UI/TSCommanderHUDWidget.h"
#include "TSCommanderScreenWidget.generated.h"

class UTSRadarWidget;
class UTSTankAttitudeWidget;
class UTSVisionFeedWidget;

UCLASS()
class UTSCommanderScreenWidget : public UTSCommanderHUDWidget
{
	GENERATED_BODY()

public:
	UTSCommanderScreenWidget();

	// The panels do NOT tick themselves - this widget drives all three. One clock, one ordering, and
	// it behaves the same whether the layout was generated here or hand-authored in a WBP.
	//
	// HONEST NOTE ON WHY: an earlier version of this comment claimed child UUserWidgets are not
	// reliably ticked by Slate, and said that was measured. It was not. What was measured is that in
	// an MCP-driven editor PIE session NOTHING in this hierarchy runs - THIS widget's NativeTick and
	// NativePaint never fire either, ~450 frames after its NativeConstruct logs with all three panels
	// bound. So the child-tick theory is unproven and the restructure below is not a fix for it.
	//
	// It is kept because it is the better structure regardless, but the real symptom is still open:
	// see the "Still owed a test" note in CLAUDE.md. Do not read this design as having resolved it.
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Frames this screen has ticked. Exists because "the instruments read zero" has two very
	// different causes - no data, or no tick - and they are indistinguishable from the outside.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Commander Screen")
	int32 GetTickCount() const { return TickCount; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Commander Screen")
	UTSRadarWidget* GetRadarWidget() const { return RadarWidget; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Commander Screen")
	UTSTankAttitudeWidget* GetAttitudeWidget() const { return AttitudeWidget; }

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Commander Screen")
	UTSVisionFeedWidget* GetVisionFeedWidget() const { return VisionFeedWidget; }

	// Which classes the default layout instantiates. Swapping one for a subclass is how a project
	// customises a panel without re-authoring the split.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Commander Screen")
	TSubclassOf<UTSVisionFeedWidget> VisionFeedWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Commander Screen")
	TSubclassOf<UTSRadarWidget> RadarWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Commander Screen")
	TSubclassOf<UTSTankAttitudeWidget> AttitudeWidgetClass;

	// Relative widths of the vision feed and of the instrument column beside it.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Commander Screen", meta = (ClampMin = "0.05"))
	float VisionFill = 1.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Commander Screen", meta = (ClampMin = "0.05"))
	float InstrumentFill = 1.f;

	// Relative heights of the radar and the attitude dial within the instrument column.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Commander Screen", meta = (ClampMin = "0.05"))
	float RadarFill = 1.2f;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Commander Screen", meta = (ClampMin = "0.05"))
	float AttitudeFill = 1.f;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Commander Screen")
	FMargin PanelPadding = FMargin(4.f);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Commander Screen")
	TObjectPtr<UTSRadarWidget> RadarWidget;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Commander Screen")
	TObjectPtr<UTSTankAttitudeWidget> AttitudeWidget;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Commander Screen")
	TObjectPtr<UTSVisionFeedWidget> VisionFeedWidget;

	virtual void NativeConstruct() override;

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	int32 TickCount = 0;

	// One line each, the first time this screen constructs / ticks / paints. Two of those three can
	// be true while the third is not, and the difference decides whether an instrument reading zero
	// is a data problem or a lifecycle problem - which is exactly the ambiguity that made this hard
	// to pin down the first time. Cheap enough to keep: three lines per session, ever.
	mutable bool bLoggedFirstPaint = false;
	bool bLoggedFirstTick = false;

	// Only runs when the widget tree is empty - i.e. this class used directly, with no WBP.
	void BuildDefaultLayout();

	// Finds the three panels wherever they are, so a hand-authored WBP tree binds the same way the
	// generated one does.
	void BindPanelsFromWidgetTree();
};
