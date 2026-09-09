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

	// The panels do NOT tick themselves. A child UUserWidget living inside another widget's tree is
	// not reliably ticked by Slate - measured, not assumed: with interpolation disabled the attitude
	// dial's values never left zero while the tank was demonstrably assigned and moving. This widget
	// is the one actually added to the viewport, so it drives all three from here. One clock, one
	// ordering, and it works the same whether the layout was generated or hand-authored.
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

private:
	int32 TickCount = 0;

	// Only runs when the widget tree is empty - i.e. this class used directly, with no WBP.
	void BuildDefaultLayout();

	// Finds the three panels wherever they are, so a hand-authored WBP tree binds the same way the
	// generated one does.
	void BindPanelsFromWidgetTree();
};
