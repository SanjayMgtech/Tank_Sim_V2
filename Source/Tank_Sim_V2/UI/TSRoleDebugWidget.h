// The lobby console: match state, a card per team showing its tank and who holds each seat, and a
// card per connected player.
//
// On the host every player card carries grouped buttons to put that player on a team and into a seat,
// which is the assignment flow - the host builds the crews, rather than every player racing to
// self-select. On a client the same cards render read-only (apart from the player's own Desktop/VR
// choice), so everyone can see the crews forming.
//
// Blueprint: the shipped look is WBP_HostLobbyConsole, a Widget Blueprint deriving from this class and
// picked up through ATSTankPlayerController::HostLobbyConsoleClass (DefaultGame.ini). Two ways to
// restyle it:
//   1. Class Defaults > ConsoleStyle - every colour and font size; the player cards inherit it.
//   2. Author a designer tree. When the Blueprint has a root widget, C++ builds nothing and instead
//      binds the optional widgets below BY NAME (TitleText, TeamCardsBox, PlayerRowsBox, ...). Only
//      the ones present are driven, so a design can drop any of them.
// With an empty designer tree (the default) the whole layout is built here, so a fresh clone with no
// Blueprint at all still gets a working console.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/TSRoleDebugRowWidget.h"
#include "TSRoleDebugWidget.generated.h"

class UBorder;
class UButton;
class UPanelWidget;
class UScrollBox;
class USizeBox;
class UTextBlock;

UCLASS()
class UTSRoleDebugWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UTSRoleDebugWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Rebuilds the panel immediately instead of waiting for the next refresh interval.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Lobby")
	void RefreshNow();

	// One-line summary of net/match state and the local player - handy for logging.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Lobby")
	FString BuildDebugString() const;

	// "VK1602Leopard" from "BP_VK1602Leopard_Controller_Chaos_C_0" - a readable name for a team tank.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Lobby")
	static FString GetTankDisplayName(const APawn* Tank);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	// Runs after every refresh, for Blueprint additions on top of the C++ layout.
	UFUNCTION(BlueprintImplementableEvent, Category = "Tank Simulation|Lobby")
	void OnConsoleRefreshed();

	// ---- Tuning ---------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Lobby")
	FTSLobbyConsoleStyle ConsoleStyle;

	// Player card class. A Blueprint subclass of UTSRoleDebugRowWidget may restyle it further.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tank Simulation|Lobby")
	TSubclassOf<UTSRoleDebugRowWidget> PlayerRowClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Lobby", meta = (ClampMin = "0.0"))
	float RefreshInterval = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Lobby")
	bool bShowAllPlayers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Lobby")
	bool bShowTeamTanks = true;

	// How many team cards (and team buttons) to show, TeamA first. Mirrors the GameMode's MaxTeams,
	// which clients cannot read.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Lobby", meta = (ClampMin = "1", ClampMax = "4"))
	int32 NumTeams = 4;

	// Show the host's "START MATCH" button. The cursor this panel is clicked with is NOT owned here -
	// ATSTankPlayerController::SetLobbyConsoleFocused owns it, toggled by the player (F1 by default).
	// This widget used to take the cursor itself for as long as the match was not InProgress, which
	// meant the host had no camera look or reliable WASD for the whole lobby.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Lobby")
	bool bShowStartMatchButton = true;

	// Widest the console may get. Also capped against the viewport below.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Lobby", meta = (ClampMin = "300.0"))
	float PanelMaxWidth = 860.f;

	// Fraction of the viewport the panel may occupy before it starts scrolling. A fixed pixel layout
	// is fine at 1080p and clips the moment the window is small, which is exactly what a second PIE
	// window is.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Lobby", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float MaxViewportWidthFraction = 0.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Lobby", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float MaxViewportHeightFraction = 0.85f;

	// ---- Widgets (built in C++, or bound by name from a Blueprint designer tree) -----------------

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> RootSizeBox;

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	// Map, net mode and the F1 hint.
	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SubtitleText;

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> MatchStateBadge;

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MatchStateText;

	// What the local player is, and what they should do next.
	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> YouText;

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TeamsHeaderText;

	// Team cards are created into this panel (a wrap box in the default layout).
	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> TeamCardsBox;

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PlayersHeaderText;

	// Player cards are created into this panel (a vertical box in the default layout).
	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> PlayerRowsBox;

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EmptyPlayersText;

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UButton> StartMatchButton;

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StartMatchText;

private:
	void BuildDefaultLayout();
	UTextBlock* MakeText(const FString& Text, int32 Size, bool bBold, const FLinearColor& Color, int32 LetterSpacing = 0);
	void EnsureTeamCards();

	FString DescribeNetContext() const;
	FString DescribeLocalPlayer() const;

	// Re-fits the panel to the viewport. Cheap, and called on refresh because the window can be
	// resized at any time.
	void UpdateResponsiveLayout();

	void RefreshHeader();
	void RefreshTeamCards();

	// Cards are pooled and re-pointed rather than rebuilt, so buttons keep their identity and do not
	// flicker out from under the cursor on every refresh.
	void RefreshPlayerRows();
	void RefreshStartMatchButton();

	UFUNCTION()
	void OnStartMatchClicked();

	// Team alert (Danger / No Danger) buttons on each team card. Host only - clients see just the
	// status line. UButton::OnClicked carries no payload, hence one handler per team and state.
	UButton* MakeCardButton(const FString& Label);
	void RequestTeamAlert(int32 TeamIndex, ETSTeamAlertState NewState);
	UFUNCTION() void OnTeamAClearClicked();
	UFUNCTION() void OnTeamADangerClicked();
	UFUNCTION() void OnTeamBClearClicked();
	UFUNCTION() void OnTeamBDangerClicked();
	UFUNCTION() void OnTeamCClearClicked();
	UFUNCTION() void OnTeamCDangerClicked();
	UFUNCTION() void OnTeamDClearClicked();
	UFUNCTION() void OnTeamDDangerClicked();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTSRoleDebugRowWidget>> PlayerRows;

	// One card per team: header strip, title, tank, three seat lines, and a crewed count. Seat arrays
	// are flattened team-major (team * 3 + seat).
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> TeamCards;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> TeamCardStripes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> TeamTitleTexts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> TeamTankTexts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> TeamSeatDots;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> TeamSeatNames;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> TeamCountTexts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> TeamAlertTexts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPanelWidget>> TeamAlertButtonRows;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> TeamClearButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> TeamDangerButtons;

	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> RootScrollBox;

	int32 SeatedPlayerCount = 0;
	int32 ListedPlayerCount = 0;
	float TimeSinceRefresh = 0.f;
};
