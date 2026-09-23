#include "UI/TSRoleDebugWidget.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Core/TSGameState.h"
#include "Core/TSTypes.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCrewComponent.h"

namespace
{
	const TArray<ETSTeamId> ConsoleTeams = { ETSTeamId::TeamA, ETSTeamId::TeamB, ETSTeamId::TeamC, ETSTeamId::TeamD };
	const TArray<ETSCrewRole> ConsoleSeats = { ETSCrewRole::Driver, ETSCrewRole::Gunner, ETSCrewRole::Commander };
	constexpr int32 SeatsPerTank = 3;
	constexpr float TeamCardWidth = 196.f;

	FString NetModeToString(ENetMode NetMode)
	{
		switch (NetMode)
		{
		case NM_Standalone:		return TEXT("Standalone");
		case NM_ListenServer:	return TEXT("Listen server");
		case NM_DedicatedServer:return TEXT("Dedicated server");
		case NM_Client:			return TEXT("Client");
		default:				return TEXT("?");
		}
	}

	FLinearColor MatchStateColor(ETSMatchState State)
	{
		switch (State)
		{
		case ETSMatchState::WaitingForPlayers:		return FLinearColor(0.96f, 0.72f, 0.2f);
		case ETSMatchState::TeamAndRoleSelection:	return FLinearColor(0.3f, 0.62f, 1.f);
		case ETSMatchState::InProgress:				return FLinearColor(0.3f, 0.86f, 0.42f);
		default:									return FLinearColor(0.55f, 0.58f, 0.64f);
		}
	}

	void PadVerticalSlot(UWidget* Widget, const FMargin& Padding)
	{
		if (UVerticalBoxSlot* BoxSlot = Widget ? Cast<UVerticalBoxSlot>(Widget->Slot) : nullptr)
		{
			BoxSlot->SetPadding(Padding);
		}
	}
}

UTSRoleDebugWidget::UTSRoleDebugWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// SelfHitTestInvisible, not HitTestInvisible: the panel background must not eat clicks, but the
	// assignment buttons in the cards have to stay clickable.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	SetIsFocusable(false);
	PlayerRowClass = UTSRoleDebugRowWidget::StaticClass();
}

UTextBlock* UTSRoleDebugWidget::MakeText(const FString& Text, int32 Size, bool bBold, const FLinearColor& Color, int32 LetterSpacing)
{
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Block->SetText(FText::FromString(Text));
	Block->SetColorAndOpacity(FSlateColor(Color));
	TSLobbyConsoleUI::SetFont(Block, Size, bBold, LetterSpacing);
	return Block;
}

TSharedRef<SWidget> UTSRoleDebugWidget::RebuildWidget()
{
	// A UUserWidget with no WidgetTree root renders nothing, so build the default tree here rather
	// than requiring a designer layout. A Blueprint that authored its own tree keeps it, and the
	// BindWidgetOptional members above are bound from it by name. Guarded because RebuildWidget runs
	// again on every reconstruct.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}

	if (StartMatchButton)
	{
		UTSRoleDebugRowWidget::MakeButtonNonFocusable(StartMatchButton);
		StartMatchButton->OnClicked.AddUniqueDynamic(this, &UTSRoleDebugWidget::OnStartMatchClicked);
	}

	return Super::RebuildWidget();
}

void UTSRoleDebugWidget::BuildDefaultLayout()
{
	const FTSLobbyConsoleStyle& S = ConsoleStyle;

	UOverlay* RootOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("ConsoleRoot"));
	WidgetTree->RootWidget = RootOverlay;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ConsolePanel"));
	Panel->SetBrush(TSLobbyConsoleUI::MakeRoundedBrush(S.PanelColor, S.CornerRadius + 4.f));
	Panel->SetPadding(FMargin(0.f));
	Panel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UOverlaySlot* OverlaySlot = RootOverlay->AddChildToOverlay(Panel))
	{
		// Pinned to the top-left so it never overlaps the crew HUD in the centre of the screen.
		OverlaySlot->SetHorizontalAlignment(HAlign_Left);
		OverlaySlot->SetVerticalAlignment(VAlign_Top);
		OverlaySlot->SetPadding(FMargin(24.f, 24.f, 0.f, 0.f));
	}

	// Panel -> SizeBox -> Column[header, ScrollBox -> body]. The SizeBox bounds the panel against the
	// viewport and the ScrollBox turns "too tall" into a scroll, while the header stays in view.
	RootSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("RootSizeBox"));
	Panel->SetContent(RootSizeBox);

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	RootSizeBox->AddChild(Column);

	// ---- Header bar ------------------------------------------------------------------------------
	UBorder* Header = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ConsoleHeader"));
	Header->SetBrush(TSLobbyConsoleUI::MakeRoundedBrush(S.HeaderColor, S.CornerRadius + 4.f));
	Header->SetPadding(FMargin(16.f, 12.f));
	Column->AddChildToVerticalBox(Header);

	UVerticalBox* HeaderColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Header->SetContent(HeaderColumn);

	UHorizontalBox* TitleLine = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	HeaderColumn->AddChildToVerticalBox(TitleLine);

	// A short accent bar before the title - the one splash of colour that marks this as the console.
	USizeBox* AccentSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	AccentSize->SetWidthOverride(4.f);
	AccentSize->SetHeightOverride(S.TitleFontSize + 6.f);
	UBorder* Accent = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Accent->SetBrush(TSLobbyConsoleUI::MakeRoundedBrush(S.AccentColor, 2.f));
	AccentSize->AddChild(Accent);
	if (UHorizontalBoxSlot* AccentSlot = TitleLine->AddChildToHorizontalBox(AccentSize))
	{
		AccentSlot->SetVerticalAlignment(VAlign_Center);
		AccentSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
	}

	TitleText = MakeText(TEXT("CREW ASSIGNMENT"), S.TitleFontSize, true, S.TextColor, 80);
	if (UHorizontalBoxSlot* TitleSlot = TitleLine->AddChildToHorizontalBox(TitleText))
	{
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TitleSlot->SetVerticalAlignment(VAlign_Center);
	}

	MatchStateBadge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MatchStateBadge"));
	MatchStateBadge->SetPadding(FMargin(10.f, 3.f));
	MatchStateText = MakeText(TEXT(""), S.FontSize - 1, true, S.TextColor, 100);
	MatchStateBadge->SetContent(MatchStateText);
	if (UHorizontalBoxSlot* BadgeSlot = TitleLine->AddChildToHorizontalBox(MatchStateBadge))
	{
		BadgeSlot->SetVerticalAlignment(VAlign_Center);
		BadgeSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
	}

	SubtitleText = MakeText(TEXT(""), S.FontSize - 1, false, S.MutedTextColor);
	HeaderColumn->AddChildToVerticalBox(SubtitleText);
	PadVerticalSlot(SubtitleText, FMargin(14.f, 4.f, 0.f, 0.f));

	YouText = MakeText(TEXT(""), S.FontSize, false, S.TextColor);
	YouText->SetAutoWrapText(true);
	HeaderColumn->AddChildToVerticalBox(YouText);
	PadVerticalSlot(YouText, FMargin(14.f, 8.f, 0.f, 0.f));

	// ---- Scrolling body --------------------------------------------------------------------------
	RootScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ConsoleScroll"));
	if (UVerticalBoxSlot* ScrollSlot = Column->AddChildToVerticalBox(RootScrollBox))
	{
		ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}

	UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	RootScrollBox->AddChild(Body);

	TeamsHeaderText = MakeText(TEXT("TEAMS"), S.FontSize - 1, true, S.AccentColor, 150);
	Body->AddChildToVerticalBox(TeamsHeaderText);
	PadVerticalSlot(TeamsHeaderText, FMargin(16.f, 14.f, 16.f, 6.f));

	UWrapBox* TeamWrap = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("TeamCardsBox"));
	TeamWrap->SetInnerSlotPadding(FVector2D(8.f, 8.f));
	TeamCardsBox = TeamWrap;
	Body->AddChildToVerticalBox(TeamWrap);
	PadVerticalSlot(TeamWrap, FMargin(16.f, 0.f, 16.f, 0.f));

	PlayersHeaderText = MakeText(TEXT("PLAYERS"), S.FontSize - 1, true, S.AccentColor, 150);
	Body->AddChildToVerticalBox(PlayersHeaderText);
	PadVerticalSlot(PlayersHeaderText, FMargin(16.f, 16.f, 16.f, 6.f));

	UVerticalBox* Rows = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PlayerRowsBox"));
	PlayerRowsBox = Rows;
	Body->AddChildToVerticalBox(Rows);
	PadVerticalSlot(Rows, FMargin(16.f, 0.f, 16.f, 0.f));

	EmptyPlayersText = MakeText(TEXT("No players connected yet - they appear here as they join."),
		S.FontSize, false, S.MutedTextColor);
	EmptyPlayersText->SetAutoWrapText(true);
	Body->AddChildToVerticalBox(EmptyPlayersText);
	PadVerticalSlot(EmptyPlayersText, FMargin(16.f, 2.f, 16.f, 0.f));

	// Host only (RefreshStartMatchButton collapses it otherwise). Without this the match could only
	// reach InProgress when every seat on every team was filled, so a short-handed lobby had no way
	// to start at all.
	StartMatchButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("StartMatchButton"));
	StartMatchButton->SetVisibility(ESlateVisibility::Collapsed);
	TSLobbyConsoleUI::ApplyButtonColor(StartMatchButton, S.StartMatchColor, S);
	{
		FButtonStyle Style = StartMatchButton->GetStyle();
		Style.NormalPadding = FMargin(16.f, 9.f);
		Style.PressedPadding = FMargin(16.f, 9.f);
		StartMatchButton->SetStyle(Style);
	}
	StartMatchText = MakeText(TEXT("START MATCH"), S.FontSize + 2, true, FLinearColor::White, 120);
	StartMatchText->SetJustification(ETextJustify::Center);
	StartMatchButton->SetContent(StartMatchText);
	Body->AddChildToVerticalBox(StartMatchButton);
	PadVerticalSlot(StartMatchButton, FMargin(16.f, 16.f, 16.f, 16.f));
}

void UTSRoleDebugWidget::EnsureTeamCards()
{
	if (!TeamCardsBox || TeamCards.Num() >= ConsoleTeams.Num())
	{
		return;
	}

	const FTSLobbyConsoleStyle& S = ConsoleStyle;
	for (int32 TeamIndex = TeamCards.Num(); TeamIndex < ConsoleTeams.Num(); ++TeamIndex)
	{
		USizeBox* CardSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		CardSize->SetWidthOverride(TeamCardWidth);

		UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Card->SetBrush(TSLobbyConsoleUI::MakeRoundedBrush(S.CardColor, S.CornerRadius));
		Card->SetPadding(FMargin(0.f));
		CardSize->AddChild(Card);

		UVerticalBox* CardColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Card->SetContent(CardColumn);

		// Team-coloured strip across the top of the card.
		USizeBox* StripeSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		StripeSize->SetHeightOverride(4.f);
		UBorder* Stripe = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		StripeSize->AddChild(Stripe);
		CardColumn->AddChildToVerticalBox(StripeSize);

		UVerticalBox* Inner = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		CardColumn->AddChildToVerticalBox(Inner);
		PadVerticalSlot(Inner, FMargin(12.f, 8.f, 12.f, 10.f));

		UTextBlock* Title = MakeText(TEXT(""), S.FontSize + 2, true, S.TextColor, 100);
		Inner->AddChildToVerticalBox(Title);

		UTextBlock* Tank = MakeText(TEXT(""), S.FontSize - 1, false, S.MutedTextColor);
		Tank->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		Inner->AddChildToVerticalBox(Tank);
		PadVerticalSlot(Tank, FMargin(0.f, 1.f, 0.f, 6.f));

		static const TCHAR* SeatLabels[] = { TEXT("DRIVER"), TEXT("GUNNER"), TEXT("CMDR") };
		for (int32 Seat = 0; Seat < SeatsPerTank; ++Seat)
		{
			UHorizontalBox* SeatLine = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			Inner->AddChildToVerticalBox(SeatLine);
			PadVerticalSlot(SeatLine, FMargin(0.f, 2.f));

			UTextBlock* Dot = MakeText(TEXT("○"), S.FontSize, false, S.MutedTextColor);
			if (UHorizontalBoxSlot* DotSlot = SeatLine->AddChildToHorizontalBox(Dot))
			{
				DotSlot->SetVerticalAlignment(VAlign_Center);
				DotSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
			}

			UTextBlock* Label = MakeText(SeatLabels[Seat], S.FontSize - 3, true, S.MutedTextColor, 80);
			Label->SetMinDesiredWidth(56.f);
			if (UHorizontalBoxSlot* LabelSlot = SeatLine->AddChildToHorizontalBox(Label))
			{
				LabelSlot->SetVerticalAlignment(VAlign_Center);
			}

			UTextBlock* Name = MakeText(TEXT(""), S.FontSize, false, S.TextColor);
			Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
			if (UHorizontalBoxSlot* NameSlot = SeatLine->AddChildToHorizontalBox(Name))
			{
				NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				NameSlot->SetVerticalAlignment(VAlign_Center);
			}

			TeamSeatDots.Add(Dot);
			TeamSeatNames.Add(Name);
		}

		UTextBlock* Count = MakeText(TEXT(""), S.FontSize - 3, true, S.MutedTextColor, 100);
		Inner->AddChildToVerticalBox(Count);
		PadVerticalSlot(Count, FMargin(0.f, 7.f, 0.f, 0.f));

		TeamCardsBox->AddChild(CardSize);

		TeamCards.Add(Card);
		TeamCardStripes.Add(Stripe);
		TeamTitleTexts.Add(Title);
		TeamTankTexts.Add(Tank);
		TeamCountTexts.Add(Count);
	}
}

void UTSRoleDebugWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	TimeSinceRefresh += InDeltaTime;
	if (TimeSinceRefresh >= RefreshInterval)
	{
		TimeSinceRefresh = 0.f;
		RefreshNow();
	}
}

void UTSRoleDebugWidget::RefreshNow()
{
	// Re-fit first: the window may have been resized since the last refresh.
	UpdateResponsiveLayout();

	// Players before the header: the header's counts come from the roster pass.
	RefreshPlayerRows();
	RefreshHeader();
	RefreshTeamCards();
	RefreshStartMatchButton();

	OnConsoleRefreshed();
}

void UTSRoleDebugWidget::RefreshHeader()
{
	const UWorld* World = GetWorld();
	const ATSGameState* GS = World ? World->GetGameState<ATSGameState>() : nullptr;
	const ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>();
	const bool bIsHost = PC && PC->IsMatchHost();

	if (SubtitleText && World)
	{
		FString MapName = World->GetMapName();
		MapName.RemoveFromStart(World->StreamingLevelsPrefix);
		SubtitleText->SetText(FText::FromString(FString::Printf(TEXT("%s  ·  %s  ·  F1 toggles the mouse cursor"),
			*MapName, *NetModeToString(World->GetNetMode()))));
	}

	if (MatchStateBadge && MatchStateText)
	{
		const ETSMatchState State = GS ? GS->GetMatchState() : ETSMatchState::WaitingForPlayers;
		const FLinearColor Color = MatchStateColor(State);
		FLinearColor Fill = Color * 0.25f;
		Fill.A = 1.f;
		MatchStateBadge->SetBrush(TSLobbyConsoleUI::MakeRoundedBrush(Fill, 10.f));
		MatchStateText->SetColorAndOpacity(FSlateColor(Color));
		MatchStateText->SetText(FText::FromString(GS
			? UTSTypeUtils::MatchStateToString(State).ToUpper()
			: TEXT("NO GAME STATE")));
	}

	if (YouText)
	{
		const ATSTankPlayerState* PS = GetOwningPlayerState<ATSTankPlayerState>();
		FString Line;
		if (bIsHost)
		{
			Line = TEXT("You are the HOST (free camera). Give every player a team and a seat, then start the match.");
		}
		else if (!PS)
		{
			Line = TEXT("Connecting...");
		}
		else if (PS->GetTeamId() == ETSTeamId::None)
		{
			Line = TEXT("Waiting for the host to put you on a team.");
		}
		else if (PS->GetCrewRole() == ETSCrewRole::None)
		{
			Line = FString::Printf(TEXT("You are on %s - waiting for the host to give you a seat."),
				*UTSTypeUtils::TeamIdToString(PS->GetTeamId()));
		}
		else
		{
			Line = FString::Printf(TEXT("You are the %s of %s, playing on %s."),
				*UTSTypeUtils::CrewRoleToString(PS->GetCrewRole()),
				*UTSTypeUtils::TeamIdToString(PS->GetTeamId()),
				PS->GetPlayMode() == ETSPlayMode::VR ? TEXT("VR") : TEXT("Desktop"));
		}
		YouText->SetText(FText::FromString(Line));
	}

	if (PlayersHeaderText)
	{
		const int32 Unseated = ListedPlayerCount - SeatedPlayerCount;
		FString Header = FString::Printf(TEXT("PLAYERS  ·  %d CONNECTED"), ListedPlayerCount);
		if (Unseated > 0)
		{
			Header += FString::Printf(TEXT("  ·  %d WITHOUT A SEAT"), Unseated);
		}
		else if (ListedPlayerCount > 0)
		{
			Header += TEXT("  ·  ALL SEATED");
		}
		PlayersHeaderText->SetText(FText::FromString(Header));
	}
}

void UTSRoleDebugWidget::RefreshTeamCards()
{
	if (TeamsHeaderText)
	{
		TeamsHeaderText->SetVisibility(bShowTeamTanks ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (!TeamCardsBox)
	{
		return;
	}
	TeamCardsBox->SetVisibility(bShowTeamTanks ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (!bShowTeamTanks)
	{
		return;
	}

	EnsureTeamCards();

	const ATSGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATSGameState>() : nullptr;
	const FTSLobbyConsoleStyle& S = ConsoleStyle;

	for (int32 TeamIndex = 0; TeamIndex < TeamCards.Num(); ++TeamIndex)
	{
		// The card's parent is its SizeBox; hide that so a hidden team leaves no gap in the wrap box.
		UWidget* CardRoot = TeamCards[TeamIndex] ? TeamCards[TeamIndex]->GetParent() : nullptr;
		const bool bVisible = TeamIndex < NumTeams;
		if (CardRoot)
		{
			CardRoot->SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
		if (!bVisible)
		{
			continue;
		}

		const ETSTeamId TeamId = ConsoleTeams[TeamIndex];
		const FLinearColor TeamColor = S.GetTeamColor(TeamId);
		const APawn* Tank = GS ? GS->FindTankForTeam(TeamId) : nullptr;
		const UTSTankCrewComponent* Crew = Tank ? Tank->FindComponentByClass<UTSTankCrewComponent>() : nullptr;

		if (TeamCardStripes.IsValidIndex(TeamIndex) && TeamCardStripes[TeamIndex])
		{
			TeamCardStripes[TeamIndex]->SetBrush(TSLobbyConsoleUI::MakeRoundedBrush(Tank ? TeamColor : TeamColor * 0.45f, 2.f));
		}
		if (UTextBlock* Title = TeamTitleTexts.IsValidIndex(TeamIndex) ? TeamTitleTexts[TeamIndex].Get() : nullptr)
		{
			Title->SetText(FText::FromString(UTSTypeUtils::TeamIdToString(TeamId).ToUpper()));
			Title->SetColorAndOpacity(FSlateColor(TeamColor));
		}
		if (UTextBlock* TankText = TeamTankTexts.IsValidIndex(TeamIndex) ? TeamTankTexts[TeamIndex].Get() : nullptr)
		{
			TankText->SetText(FText::FromString(!Tank
				? TEXT("No tank yet")
				: (Crew ? GetTankDisplayName(Tank) : FString::Printf(TEXT("%s (no crew component!)"), *GetTankDisplayName(Tank)))));
		}

		int32 Filled = 0;
		for (int32 Seat = 0; Seat < SeatsPerTank; ++Seat)
		{
			const int32 Flat = TeamIndex * SeatsPerTank + Seat;
			const APlayerState* Occupant = Crew ? Crew->GetOccupant(ConsoleSeats[Seat]) : nullptr;
			Filled += Occupant ? 1 : 0;

			if (UTextBlock* Dot = TeamSeatDots.IsValidIndex(Flat) ? TeamSeatDots[Flat].Get() : nullptr)
			{
				Dot->SetText(FText::FromString(Occupant ? TEXT("●") : TEXT("○")));
				Dot->SetColorAndOpacity(FSlateColor(Occupant ? TeamColor : S.MutedTextColor * 0.8f));
			}
			if (UTextBlock* Name = TeamSeatNames.IsValidIndex(Flat) ? TeamSeatNames[Flat].Get() : nullptr)
			{
				Name->SetText(FText::FromString(Occupant ? Occupant->GetPlayerName() : TEXT("empty")));
				Name->SetColorAndOpacity(FSlateColor(Occupant ? S.TextColor : S.MutedTextColor * 0.8f));
			}
		}

		if (UTextBlock* Count = TeamCountTexts.IsValidIndex(TeamIndex) ? TeamCountTexts[TeamIndex].Get() : nullptr)
		{
			if (!Tank)
			{
				// A team's tank spawns on the first assignment to that team, so an empty card is not a fault.
				Count->SetText(FText::FromString(TEXT("ASSIGN A PLAYER TO SPAWN IT")));
				Count->SetColorAndOpacity(FSlateColor(S.MutedTextColor * 0.8f));
			}
			else
			{
				Count->SetText(FText::FromString(Filled == SeatsPerTank
					? TEXT("FULLY CREWED")
					: FString::Printf(TEXT("%d / %d SEATS FILLED"), Filled, SeatsPerTank)));
				Count->SetColorAndOpacity(FSlateColor(Filled == SeatsPerTank ? S.ButtonSelectedColor * 1.7f : S.MutedTextColor));
			}
		}
	}
}

void UTSRoleDebugWidget::RefreshPlayerRows()
{
	ListedPlayerCount = 0;
	SeatedPlayerCount = 0;

	if (!PlayerRowsBox)
	{
		return;
	}

	if (!bShowAllPlayers)
	{
		PlayerRowsBox->SetVisibility(ESlateVisibility::Collapsed);
		if (PlayersHeaderText) { PlayersHeaderText->SetVisibility(ESlateVisibility::Collapsed); }
		if (EmptyPlayersText) { EmptyPlayersText->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}
	PlayerRowsBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (PlayersHeaderText) { PlayersHeaderText->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }

	const UWorld* World = GetWorld();
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	if (!GS)
	{
		return;
	}

	TArray<ATSTankPlayerState*> Players;
	Players.Reserve(GS->PlayerArray.Num());
	for (APlayerState* PlayerState : GS->PlayerArray)
	{
		if (ATSTankPlayerState* TankPS = Cast<ATSTankPlayerState>(PlayerState))
		{
			// The host runs this console; it is not one of the players it assigns. Listing it would
			// give a card whose every assignment the GameMode rejects.
			if (!TankPS->IsHost())
			{
				Players.Add(TankPS);
				SeatedPlayerCount += TankPS->GetCrewRole() != ETSCrewRole::None ? 1 : 0;
			}
		}
	}
	ListedPlayerCount = Players.Num();

	if (EmptyPlayersText)
	{
		EmptyPlayersText->SetVisibility(Players.Num() == 0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	// PlayerArray order is not stable across replication updates; sorting by the immutable player id
	// keeps each player on the same card so a button never moves out from under the cursor mid-click.
	Players.Sort([](const ATSTankPlayerState& A, const ATSTankPlayerState& B)
	{
		return A.GetPlayerId() < B.GetPlayerId();
	});

	TSubclassOf<UTSRoleDebugRowWidget> RowClass = PlayerRowClass;
	if (!RowClass)
	{
		RowClass = UTSRoleDebugRowWidget::StaticClass();
	}

	while (PlayerRows.Num() < Players.Num())
	{
		UTSRoleDebugRowWidget* Row = CreateWidget<UTSRoleDebugRowWidget>(GetOwningPlayer(), RowClass);
		if (!Row)
		{
			break;
		}
		// Before AddChild, which is what builds the card's tree.
		Row->SetConsoleStyle(ConsoleStyle);
		Row->NumTeamButtons = NumTeams;
		PlayerRows.Add(Row);
		PlayerRowsBox->AddChild(Row);
		PadVerticalSlot(Row, FMargin(0.f, 0.f, 0.f, 6.f));
	}

	for (int32 Index = 0; Index < PlayerRows.Num(); ++Index)
	{
		UTSRoleDebugRowWidget* Row = PlayerRows[Index];
		if (!Row)
		{
			continue;
		}

		if (Players.IsValidIndex(Index))
		{
			// Only re-point a card when it actually changed, so RefreshRow does not churn the buttons.
			if (Row->GetTargetPlayerState() != Players[Index])
			{
				Row->SetTargetPlayerState(Players[Index]);
			}
			else
			{
				Row->RefreshRow();
			}
		}
		else
		{
			// Surplus card from a player who left - keep it pooled but hidden.
			Row->SetTargetPlayerState(nullptr);
		}
	}
}

void UTSRoleDebugWidget::RefreshStartMatchButton()
{
	if (!StartMatchButton)
	{
		return;
	}

	const ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>();
	const ATSGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATSGameState>() : nullptr;
	const bool bInProgress = GS && GS->GetMatchState() == ETSMatchState::InProgress;

	// Only the host can start a match, and only one that has not started.
	const bool bVisible = bShowStartMatchButton && PC && PC->IsMatchHost() && !bInProgress;
	StartMatchButton->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);

	if (StartMatchText && bVisible)
	{
		// Still clickable short-handed - but the host should see that they are starting short-handed.
		StartMatchText->SetText(FText::FromString(ListedPlayerCount > 0
			? FString::Printf(TEXT("START MATCH   ·   %d / %d SEATED"), SeatedPlayerCount, ListedPlayerCount)
			: TEXT("START MATCH")));
	}
}

void UTSRoleDebugWidget::OnStartMatchClicked()
{
	if (ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>())
	{
		PC->ServerRequestStartMatch();

		// Hand the cursor back so the host drops straight into camera control. F1 brings it back.
		PC->SetLobbyConsoleFocused(false);
	}
}

FString UTSRoleDebugWidget::GetTankDisplayName(const APawn* Tank)
{
	if (!Tank)
	{
		return TEXT("none");
	}

	// The class, not the actor name: a placed actor can keep a stale object name (the demo level's
	// VK1602 is called BP_PZV_...), and a spawned one ends in a _C_0 counter nobody needs to read.
	FString Name = Tank->GetClass()->GetName();
	Name.RemoveFromEnd(TEXT("_C"));
	Name.RemoveFromStart(TEXT("BP_"));
	Name.RemoveFromEnd(TEXT("_Controller_Chaos"));
	return Name;
}

FString UTSRoleDebugWidget::BuildDebugString() const
{
	return DescribeNetContext() + TEXT("\n") + DescribeLocalPlayer();
}

FString UTSRoleDebugWidget::DescribeNetContext() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return TEXT("No world");
	}

	FString MapName = World->GetMapName();
	MapName.RemoveFromStart(World->StreamingLevelsPrefix);

	const ATSGameState* GS = World->GetGameState<ATSGameState>();
	const FString MatchState = GS ? UTSTypeUtils::MatchStateToString(GS->GetMatchState()) : TEXT("no ATSGameState");

	return FString::Printf(TEXT("%s  |  Map: %s  |  Match: %s"), *NetModeToString(World->GetNetMode()), *MapName, *MatchState);
}

FString UTSRoleDebugWidget::DescribeLocalPlayer() const
{
	const ATSTankPlayerState* PS = GetOwningPlayerState<ATSTankPlayerState>();
	if (!PS)
	{
		// Almost always means the GameMode's PlayerStateClass is not ATSTankPlayerState.
		return TEXT("Me: <no ATSTankPlayerState>");
	}

	const APawn* Tank = PS->GetAssignedTank();
	return FString::Printf(TEXT("Me: %s  |  Team: %s  |  Role: %s  |  Mode: %s  |  Tank: %s"),
		*PS->GetPlayerName(),
		*UTSTypeUtils::TeamIdToString(PS->GetTeamId()),
		*UTSTypeUtils::CrewRoleToString(PS->GetCrewRole()),
		PS->IsHost() ? TEXT("host (flat)") : (PS->GetPlayMode() == ETSPlayMode::VR ? TEXT("VR") : TEXT("Desktop")),
		Tank ? *Tank->GetName() : TEXT("none"));
}

void UTSRoleDebugWidget::UpdateResponsiveLayout()
{
	if (!RootSizeBox)
	{
		return;
	}

	// Viewport size is in PIXELS; widget coordinates are DPI-scaled, so dividing by the scale is
	// what makes the cap mean the same thing at any resolution. Without it the panel is bounded
	// correctly at 100% and wrongly everywhere else.
	const FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);
	const float Scale = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(this), KINDA_SMALL_NUMBER);
	const float UsableWidth = (ViewportSize.X / Scale) - 48.f;   // the 24px margin on each side
	const float UsableHeight = (ViewportSize.Y / Scale) - 48.f;

	if (UsableWidth <= 0.f || UsableHeight <= 0.f)
	{
		// The viewport has no size yet (first frame, or a hidden window). Leave the last good fit
		// rather than collapsing the panel to nothing.
		return;
	}

	const float MaxWidth = FMath::Min(PanelMaxWidth, FMath::Max(UsableWidth * MaxViewportWidthFraction, 320.f));
	RootSizeBox->SetMaxDesiredWidth(MaxWidth);
	RootSizeBox->SetMaxDesiredHeight(UsableHeight * MaxViewportHeightFraction);

	// Wrap the prose to the width the panel actually got, not a design-time constant - otherwise long
	// lines still push past the edge inside a correctly-sized box.
	if (YouText)
	{
		YouText->SetWrapTextAt(FMath::Max(MaxWidth - 48.f, 120.f));
	}
	if (EmptyPlayersText)
	{
		EmptyPlayersText->SetWrapTextAt(FMath::Max(MaxWidth - 40.f, 120.f));
	}
}
