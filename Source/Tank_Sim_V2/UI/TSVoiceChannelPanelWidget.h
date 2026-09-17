// The Commander's radio: two channel rows, each with its own TX and RX lamps.
//
//   CREW   the tank intercom. Selecting it puts the Commander back in the vehicle with the Driver
//          and Gunner. Its RX lamp lights whenever a crewmate is talking - AND KEEPS DOING SO WHILE
//          THE COMMANDER IS ON THE OTHER CHANNEL, which is the entire reason that lamp exists: it
//          is how a Commander on the host's net knows their crew is trying to reach them.
//
//   HOST   the command net. Its RX lamp lights when the host is addressing this Commander's team,
//          which the host can do whether or not this Commander is tuned here - so this lamp also
//          works as "the host wants you".
//
// Lives inside UTSCommanderScreenWidget beside the radar and attitude dial, which is where the
// Commander is already looking. It is a plain UUserWidget though, so it can equally be dropped into
// a hand-authored WBP or shown on its own.
//
// Built entirely in C++ (RebuildWidget populates the WidgetTree), like the rest of this project's
// UI: there is no WBP asset to create, reparent or keep in sync. Derive a Blueprint from it only to
// restyle.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/TSTypes.h"
#include "TSVoiceChannelPanelWidget.generated.h"

class ATSTankPlayerController;
class ATSTankPlayerState;
class UBorder;
class UButton;
class UTextBlock;
class UTSVoiceIndicatorWidget;

UCLASS()
class UTSVoiceChannelPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UTSVoiceChannelPanelWidget(const FObjectInitializer& ObjectInitializer);

	// Re-reads the replicated voice state and repaints the rows. Cheap and idempotent - the pulse
	// phase comes from the real-time clock rather than an accumulator precisely so that calling this
	// twice in one frame is harmless. That matters because the Commander screen drives its panels
	// explicitly AND this widget ticks itself when used standalone.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Voice")
	void RefreshPanel();

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// A one-line summary of what the panel is currently showing. Exists for the same reason the
	// lobby console has one: an automated test can assert on it without a screenshot.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	FString BuildPanelDebugString() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Voice", meta = (ClampMin = "6", ClampMax = "32"))
	int32 FontSize = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Voice", meta = (ClampMin = "20.0"))
	float IndicatorWidth = 62.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Voice", meta = (ClampMin = "10.0"))
	float IndicatorHeight = 20.f;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	// BindWidgetOptional: a WBP subclass that authors its own tree gets these bound by NAME (CrewButton,
	// HostButton, CrewIndicator, ...). Any it leaves out simply stay null and that part of the panel
	// is not updated - nothing here requires all of them.
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Voice", meta = (BindWidgetOptional))
	TObjectPtr<UButton> CrewButton;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Voice", meta = (BindWidgetOptional))
	TObjectPtr<UButton> HostButton;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Voice", meta = (BindWidgetOptional))
	TObjectPtr<UTSVoiceIndicatorWidget> CrewIndicator;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Voice", meta = (BindWidgetOptional))
	TObjectPtr<UTSVoiceIndicatorWidget> HostIndicator;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HeaderText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CrewLabel;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HostLabel;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FooterText;

	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> RootBorder;

private:
	UFUNCTION() void OnCrewChannelClicked();
	UFUNCTION() void OnHostChannelClicked();

	ATSTankPlayerController* GetOwningTankController() const;
	ATSTankPlayerState* GetOwningTankPlayerState() const;

	// Real time, not world time: a UI pulse should keep breathing while the game is paused, and
	// using a clock rather than an accumulator is what makes RefreshPanel idempotent.
	float GetPulseSeconds() const;

	// Tints one channel button to show whether it is the selected net, and greys it out entirely for
	// a seat that has no choice of channel.
	void StyleChannelButton(UButton* Button, bool bSelected, bool bEnabled) const;

	UButton* MakeChannelButton(const FString& Label);
	UTextBlock* MakeText(const FString& Text, int32 InFontSize, const FLinearColor& Colour);
};
