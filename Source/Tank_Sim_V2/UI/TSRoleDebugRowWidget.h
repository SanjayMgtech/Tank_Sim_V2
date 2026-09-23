// One player's card in the lobby console (UTSRoleDebugWidget): who they are, badges for their team /
// seat / body, and the host's grouped buttons to assign them.
//
// The default tree is built in C++, so a Widget Blueprint needs to author nothing. The console hands
// every card its FTSLobbyConsoleStyle before the tree is built, so restyling the console's Blueprint
// restyles the cards too. UButton::OnClicked carries no payload, so rather than one handler object per
// button this widget owns a fixed set of buttons and a single TargetPlayerState - each handler is a
// plain UFUNCTION that knows its own team/role.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/TSTypes.h"
#include "Styling/SlateBrush.h"
#include "TSRoleDebugRowWidget.generated.h"

class APlayerState;
class UBorder;
class UButton;
class UHorizontalBox;
class UTextBlock;
class UWrapBox;

// Every colour and size the lobby console uses, in one place. Lives on the console
// (UTSRoleDebugWidget::ConsoleStyle) and is copied into each player card, so a Widget Blueprint
// restyles the whole console from its Class Defaults.
USTRUCT(BlueprintType)
struct FTSLobbyConsoleStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panel")
	FLinearColor PanelColor = FLinearColor(0.015f, 0.02f, 0.028f, 0.93f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panel")
	FLinearColor HeaderColor = FLinearColor(0.045f, 0.055f, 0.075f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panel")
	FLinearColor CardColor = FLinearColor(0.06f, 0.07f, 0.09f, 0.96f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panel")
	FLinearColor AccentColor = FLinearColor(1.f, 0.72f, 0.2f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panel", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float CornerRadius = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FLinearColor TextColor = FLinearColor(0.93f, 0.95f, 0.97f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text")
	FLinearColor MutedTextColor = FLinearColor(0.52f, 0.57f, 0.65f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (ClampMin = "6", ClampMax = "48"))
	int32 FontSize = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text", meta = (ClampMin = "6", ClampMax = "64"))
	int32 TitleFontSize = 18;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons")
	FLinearColor ButtonIdleColor = FLinearColor(0.13f, 0.15f, 0.19f, 1.f);

	// The value this player currently HAS (their team, their seat, their mode).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons")
	FLinearColor ButtonSelectedColor = FLinearColor(0.13f, 0.52f, 0.26f, 1.f);

	// Unavailable for a reason worth showing - seat held by someone else, VR with no headset.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons")
	FLinearColor ButtonBlockedColor = FLinearColor(0.42f, 0.11f, 0.11f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons")
	FLinearColor ButtonDisabledColor = FLinearColor(0.08f, 0.09f, 0.11f, 0.9f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons")
	FLinearColor StartMatchColor = FLinearColor(0.12f, 0.5f, 0.22f, 1.f);

	// Team A..D, in that order. Used for the card stripe, team badge and team card header.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teams")
	TArray<FLinearColor> TeamColors = {
		FLinearColor(0.22f, 0.56f, 1.f, 1.f),
		FLinearColor(1.f, 0.32f, 0.26f, 1.f),
		FLinearColor(0.3f, 0.84f, 0.42f, 1.f),
		FLinearColor(0.96f, 0.76f, 0.2f, 1.f) };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Teams")
	FLinearColor NoTeamColor = FLinearColor(0.34f, 0.37f, 0.42f, 1.f);

	FLinearColor GetTeamColor(ETSTeamId Team) const;
};

// Small builders shared by the console and its cards. A NAMED namespace on purpose: this module is a
// unity build, and anonymous-namespace helpers with the same name in two .cpp files collide there.
namespace TSLobbyConsoleUI
{
	// A solid rounded rectangle - no texture, tinted by Color.
	FSlateBrush MakeRoundedBrush(const FLinearColor& Color, float Radius);

	// Idle/hover/pressed/disabled all as rounded fills derived from one base colour.
	void ApplyButtonColor(UButton* Button, const FLinearColor& Base, const FTSLobbyConsoleStyle& Style);

	void SetFont(UTextBlock* Text, int32 Size, bool bBold, int32 LetterSpacing = 0);
}

UCLASS()
class UTSRoleDebugRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Shared by both lobby widgets. A focusable Slate button takes keyboard focus when clicked, and
	// in FInputModeGameAndUI that swallows WASD until the player clicks back on the world - so every
	// button in the lobby console is made non-focusable right after ConstructWidget, before its
	// SWidget is built. UButton has no public setter in UE 5.7 (InitIsFocusable is protected and
	// direct field access is UE_DEPRECATED, fatal under -WarningsAsErrors), so this goes through the
	// reflection system.
	static void MakeButtonNonFocusable(UButton* Button);

	UTSRoleDebugRowWidget(const FObjectInitializer& ObjectInitializer);

	// Points this card at a player. Cards are reused across refreshes so the buttons keep their
	// identity (and don't flicker out from under the cursor) while the roster is stable.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Lobby")
	void SetTargetPlayerState(APlayerState* InPlayerState);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Lobby")
	APlayerState* GetTargetPlayerState() const;

	// Repaints the labels and re-evaluates which buttons are usable. Cheap - safe to call every tick.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Lobby")
	void RefreshRow();

	// Must be called before the card is added to a panel - the tree is built from it.
	void SetConsoleStyle(const FTSLobbyConsoleStyle& InStyle) { ConsoleStyle = InStyle; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Lobby")
	FTSLobbyConsoleStyle ConsoleStyle;

	// How many team buttons to show, TeamA first. Mirrors the GameMode's MaxTeams, which clients
	// cannot read (the GameMode only exists on the server).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Lobby", meta = (ClampMin = "1", ClampMax = "4"))
	int32 NumTeamButtons = 4;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> CardBorder;

	// Thin bar down the card's left edge in the player's team colour.
	UPROPERTY(Transient)
	TObjectPtr<UBorder> TeamStripe;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> TeamBadge;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> TeamBadgeText;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> SeatBadge;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SeatBadgeText;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> ModeBadge;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ModeBadgeText;

	UPROPERTY(Transient)
	TObjectPtr<UWrapBox> ControlsBox;

	// Each labelled button group, so whole groups can be hidden from viewers who cannot use them.
	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> TeamGroup;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> SeatGroup;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> BodyGroup;

	UPROPERTY(Transient)
	TObjectPtr<UHorizontalBox> DriveGroup;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> TeamButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> RoleButtons;

	// Play in Desktop / Play in VR, beside the team and seat buttons: which body this player takes is
	// part of the same assignment, and the host picks it in the same place.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> PlayModeButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> DriveModeButtons;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ClearButton;

private:
	UFUNCTION() void OnTeamAClicked();
	UFUNCTION() void OnTeamBClicked();
	UFUNCTION() void OnTeamCClicked();
	UFUNCTION() void OnTeamDClicked();
	UFUNCTION() void OnDriverClicked();
	UFUNCTION() void OnGunnerClicked();
	UFUNCTION() void OnCommanderClicked();
	UFUNCTION() void OnDesktopClicked();
	UFUNCTION() void OnVRClicked();
	UFUNCTION() void OnAnalogClicked();
	UFUNCTION() void OnManualClicked();
	UFUNCTION() void OnClearClicked();

	void AssignTeam(ETSTeamId Team);
	void AssignRole(ETSCrewRole Role);

	// Host-driven for anyone, self-serve for yourself. The host assigns modes in the lobby, but a
	// player is always allowed to move their own body between the headset and the keyboard - so this
	// picks the host RPC or the self-serve one from who is clicking, and the server re-checks both.
	void AssignPlayMode(ETSPlayMode PlayMode);
	void AssignDriveControlMode(ETSDriveControlMode Mode);

	UButton* MakeButton(const FString& Label, float MinWidth);
	UHorizontalBox* MakeGroup(const FString& Label);
	UBorder* MakeBadge(TObjectPtr<UTextBlock>& OutText);
	void SetBadge(UBorder* Badge, UTextBlock* Text, const FString& Label, const FLinearColor& Color);
	class ATSTankPlayerController* GetOwningTankController() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerState> TargetPlayerState;
};
