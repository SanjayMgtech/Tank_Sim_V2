// The Commander screen's view-mode options: DAY / NIGHT / HEATMAP.
//
// Each button sets the VisionMode parameter on one crew station's view material on the local player's
// assigned tank: M_Commander_View (CommanderView mesh) or M_Driver_View (DriverScreen mesh), chosen by
// ViewTarget. The look of each mode lives in that material, not here.
//
// Like the other Commander screen panels it builds its own tree in C++ when used bare. A WBP subclass
// that authors its own tree keeps it, and gets its buttons bound by NAME (DayButton, NightVisionButton,
// HeatmapButton) - leave any of them out and that option simply is not offered.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/TSTypes.h"
#include "TSVisionModeSelectorWidget.generated.h"

class ATSTankControllerBase;
class UButton;
class UTextBlock;

// Which station's view material a selector drives.
UENUM(BlueprintType)
enum class ETSVisionViewTarget : uint8
{
	Commander,
	Driver
};

UCLASS()
class UTSVisionModeSelectorWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UTSVisionModeSelectorWidget(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Commander Screen")
	void SelectVisionMode(ETSVisionMode NewMode);

	// The mode currently applied to the local player's tank view, or Normal with no tank.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Commander Screen")
	ETSVisionMode GetSelectedVisionMode() const;

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Which station's view this selector filters. Commander by default, so existing Commander screens
	// are unchanged.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Commander Screen")
	ETSVisionViewTarget ViewTarget = ETSVisionViewTarget::Commander;

	// Pin the selector to a specific tank. Unset, it follows the local player's assigned tank.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Commander Screen")
	void SetTargetTank(ATSTankControllerBase* InTank);

	// Labels for the tree this class builds when used bare. A WBP-authored tree keeps its own text.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Commander Screen")
	FText HeaderLabel = NSLOCTEXT("TankSim", "VisionHeader", "VIEW MODE");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Commander Screen")
	FText DayLabel = NSLOCTEXT("TankSim", "VisionDay", "DAY");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Commander Screen")
	FText NightVisionLabel = NSLOCTEXT("TankSim", "VisionNight", "NIGHT");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Commander Screen")
	FText HeatmapLabel = NSLOCTEXT("TankSim", "VisionHeatmap", "HEATMAP");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Commander Screen", meta = (ClampMin = "6", ClampMax = "48"))
	int32 FontSize = 14;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Commander Screen")
	FLinearColor IdleColour = FLinearColor(0.10f, 0.10f, 0.12f, 0.90f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Commander Screen")
	FLinearColor SelectedColour = FLinearColor(0.13f, 0.45f, 0.62f, 0.95f);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Commander Screen", meta = (BindWidgetOptional))
	TObjectPtr<UButton> DayButton;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Commander Screen", meta = (BindWidgetOptional))
	TObjectPtr<UButton> NightVisionButton;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Commander Screen", meta = (BindWidgetOptional))
	TObjectPtr<UButton> HeatmapButton;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HeaderText;

private:
	TWeakObjectPtr<ATSTankControllerBase> TargetTank;

	UFUNCTION() void OnDayClicked();
	UFUNCTION() void OnNightVisionClicked();
	UFUNCTION() void OnHeatmapClicked();

	ATSTankControllerBase* GetLocalTank() const;
	void RefreshHighlight();

	UButton* MakeOptionButton(const FString& Label);
	UTextBlock* MakeText(const FString& Text, int32 InFontSize, const FLinearColor& Colour);
};
