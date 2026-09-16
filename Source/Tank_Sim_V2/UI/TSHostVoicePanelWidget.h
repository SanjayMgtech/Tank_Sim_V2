// The host's command net: one row per team, selectable, with a receive lamp each.
//
// The host is a match admin with no seat and no crew, so their whole voice interface is "which
// commanders am I talking to". Selection is a MULTI-select and not a radio button: the requirement
// is that the host can address one commander or several at once, so ticking Team A and Team C means
// both hear the next thing said and nobody else does.
//
// Two things the rows deliberately show that a simpler panel would not:
//
//   * a commander who is NOT currently tuned to the command net is still selectable and will still
//     hear the host - the host outranks the channel selector (see ETSVoiceChannel). The row says
//     which net that commander is sitting on so the host knows whether a reply is likely.
//
//   * a commander transmitting on the command net lights their own RX lamp EVEN IF THE HOST HAS NOT
//     SELECTED THEM. The host always hears a commander calling; the selection only governs which
//     way the host's own voice goes. A panel that hid an incoming call until the host happened to
//     have ticked that team would be actively misleading.
//
// Built entirely in C++ like the rest of this project's UI - no WBP asset required. Derive a
// Blueprint from it only to restyle.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/TSTypes.h"
#include "TSHostVoicePanelWidget.generated.h"

class ATSTankPlayerController;
class ATSTankPlayerState;
class UBorder;
class UButton;
class UTextBlock;
class UTSVoiceIndicatorWidget;
class UVerticalBox;

UCLASS()
class UTSHostVoicePanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UTSHostVoicePanelWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Idempotent, for the same reason as the Commander panel's: the pulse comes from a clock rather
	// than an accumulated delta, so an extra call in a frame changes nothing.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Voice")
	void RefreshPanel();

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Voice")
	FString BuildPanelDebugString() const;

	// How many team rows to draw, TeamA first. Mirrors ATSGameMode::MaxTeams, which a client cannot
	// read - the GameMode only exists on the server. Same compromise UTSRoleDebugRowWidget makes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Voice", meta = (ClampMin = "1", ClampMax = "4"))
	int32 NumTeamRows = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Voice", meta = (ClampMin = "6", ClampMax = "32"))
	int32 FontSize = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Voice", meta = (ClampMin = "20.0"))
	float IndicatorWidth = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Voice", meta = (ClampMin = "10.0"))
	float IndicatorHeight = 20.f;

	// Where the panel sits. Top-right by default, clear of the lobby console on the left.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Voice")
	FVector2D ScreenPosition = FVector2D(-360.f, 24.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Voice", meta = (ClampMin = "160.0"))
	float PanelWidth = 330.f;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> RootBorder;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HeaderText;

	UPROPERTY(Transient)
	TObjectPtr<UTSVoiceIndicatorWidget> SelfIndicator;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> RowsBox;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FooterText;

	// Four parallel fixed arrays rather than a row widget class, because UButton::OnClicked carries
	// no payload: a fixed set of buttons with one handler each is how UTSRoleDebugRowWidget already
	// solves this, and a whole widget class for three controls would not earn itself.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> TeamButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> TeamLabels;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTSVoiceIndicatorWidget>> TeamIndicators;

private:
	UFUNCTION() void OnTeamAClicked();
	UFUNCTION() void OnTeamBClicked();
	UFUNCTION() void OnTeamCClicked();
	UFUNCTION() void OnTeamDClicked();

	UFUNCTION() void OnSelectAllClicked();
	UFUNCTION() void OnClearClicked();

	UPROPERTY(Transient)
	TObjectPtr<UButton> SelectAllButton;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ClearButton;

	void ToggleTeam(ETSTeamId Team);

	ATSTankPlayerController* GetOwningTankController() const;
	ATSTankPlayerState* GetOwningTankPlayerState() const;
	float GetPulseSeconds() const;

	void StyleSelectionButton(UButton* Button, bool bSelected) const;
	UButton* MakeButton(const FString& Label, float MinWidth);
	UTextBlock* MakeText(const FString& Text, int32 InFontSize, const FLinearColor& Colour);
};
