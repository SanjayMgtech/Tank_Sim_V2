// The Driver's instrument panel: speed, position, and the view-mode buttons for the Driver's screen.
//
// Meant for a world-space UWidgetComponent in the driver's compartment (DriverPanel on the VK1602),
// where the crew pawn's UTSVRPointerComponent makes it clickable by mouse or VR laser like the
// Commander screen. Works as a screen-space widget too.
//
// Readouts:
//   * speed - signed along the hull's forward axis, shown in km/h, REV while reversing
//   * position - the tank's world X / Y in metres. A stand-in for latitude / longitude until the maps
//     carry a geographic reference; the conversion will belong in GetPositionMetres.
//   * view mode - a UTSVisionModeSelectorWidget targeting the Driver's view (M_Driver_View).
//
// Like the Commander screen panels it builds its own tree in C++ when used bare. A WBP subclass that
// authors its own tree keeps it and gets its parts bound by NAME (SpeedText, SpeedUnitText,
// PositionXText, PositionYText, VisionSelector) - leave any out and that readout is simply not shown.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TSDriverPanelWidget.generated.h"

class ATSTankControllerBase;
class UTextBlock;
class UTSVisionModeSelectorWidget;

UCLASS()
class UTSDriverPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UTSDriverPanelWidget(const FObjectInitializer& ObjectInitializer);

	// Pin the panel to the tank it is mounted in. ATSTankControllerBase does this at BeginPlay for a
	// panel on one of its own widget components; unset, the panel follows the local player's
	// assigned tank.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Driver Panel")
	void SetTank(ATSTankControllerBase* InTank);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Driver Panel")
	ATSTankControllerBase* GetTank() const;

	// Signed speed along the hull's forward axis, km/h. Negative while reversing.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Driver Panel")
	float GetForwardSpeedKmh() const;

	// The tank's world X / Y in metres.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Driver Panel")
	FVector2D GetPositionMetres() const;

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// How often the text readouts are rewritten. Text re-layout every frame buys nothing a driver
	// can read.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Driver Panel", meta = (ClampMin = "1", ClampMax = "60"))
	float ReadoutRefreshHz = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Driver Panel", meta = (ClampMin = "6", ClampMax = "96"))
	int32 FontSize = 18;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Driver Panel", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SpeedText;

	// "km/h" normally, "km/h REV" while reversing.
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Driver Panel", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SpeedUnitText;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Driver Panel", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PositionXText;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Driver Panel", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PositionYText;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Driver Panel", meta = (BindWidgetOptional))
	TObjectPtr<UTSVisionModeSelectorWidget> VisionSelector;

private:
	TWeakObjectPtr<ATSTankControllerBase> Tank;
	float TimeSinceReadout = 0.f;

	void ConfigureVisionSelector();
	void RefreshReadouts();

	UTextBlock* MakeText(const FString& Text, int32 InFontSize, const FLinearColor& Colour, FName Name = NAME_None);
};
