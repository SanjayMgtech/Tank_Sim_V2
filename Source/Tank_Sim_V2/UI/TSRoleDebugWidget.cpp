#include "UI/TSRoleDebugWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Overlay.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/TSGameState.h"
#include "Core/TSTypes.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCrewComponent.h"
#include "UI/TSRoleDebugRowWidget.h"

namespace
{
	const TArray<ETSTeamId> DebugTeams = { ETSTeamId::TeamA, ETSTeamId::TeamB, ETSTeamId::TeamC, ETSTeamId::TeamD };

	FString NetModeToString(ENetMode NetMode)
	{
		switch (NetMode)
		{
		case NM_Standalone:		return TEXT("Standalone");
		case NM_ListenServer:	return TEXT("Listen Server (host)");
		case NM_DedicatedServer:return TEXT("Dedicated Server");
		case NM_Client:			return TEXT("Client");
		default:				return TEXT("?");
		}
	}

	FString OccupantName(const UTSTankCrewComponent* Crew, ETSCrewRole Role)
	{
		if (!Crew)
		{
			return TEXT("<no crew component>");
		}
		const APlayerState* Occupant = Crew->GetOccupant(Role);
		return Occupant ? Occupant->GetPlayerName() : TEXT("(empty)");
	}
}

UTSRoleDebugWidget::UTSRoleDebugWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// SelfHitTestInvisible, not HitTestInvisible: the panel background must not eat clicks, but the
	// assignment buttons in the rows have to stay clickable.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	SetIsFocusable(false);
}

TSharedRef<SWidget> UTSRoleDebugWidget::RebuildWidget()
{
	// A UUserWidget with no WidgetTree root renders nothing, so build the tree here rather than
	// requiring a WBP asset. Guarded because RebuildWidget runs again on every reconstruct.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UOverlay* RootOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("TSDebugRoot"));
		WidgetTree->RootWidget = RootOverlay;

		RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TSDebugBorder"));
		RootBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.72f));
		RootBorder->SetPadding(FMargin(12.f, 8.f));

		if (UOverlaySlot* OverlaySlot = RootOverlay->AddChildToOverlay(RootBorder))
		{
			// Pinned to the top-left so it never overlaps the crew HUD in the centre of the screen.
			OverlaySlot->SetHorizontalAlignment(HAlign_Left);
			OverlaySlot->SetVerticalAlignment(VAlign_Top);
			OverlaySlot->SetPadding(FMargin(24.f, 24.f, 0.f, 0.f));
		}

		// Border -> SizeBox -> ScrollBox -> Column. The SizeBox bounds the panel against the viewport
		// and the ScrollBox turns "too tall" into a scroll instead of content disappearing off the
		// bottom edge.
		RootSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("TSDebugSizeBox"));
		RootBorder->SetContent(RootSizeBox);

		RootScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("TSDebugScroll"));
		RootSizeBox->AddChild(RootScrollBox);

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TSDebugColumn"));
		RootScrollBox->AddChild(Column);

		HeaderText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TSDebugHeader"));
		HeaderText->SetText(FText::FromString(TEXT("TANK SIM - CREW ASSIGNMENT   [F1 = cursor]")));
		HeaderText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.82f, 0.25f)));
		{
			FSlateFontInfo Font = HeaderText->GetFont();
			Font.Size = FontSize + 2;
			Font.TypefaceFontName = FName(TEXT("Bold"));
			HeaderText->SetFont(Font);
		}
		Column->AddChildToVerticalBox(HeaderText);

		BodyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TSDebugBody"));
		BodyText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		{
			FSlateFontInfo Font = BodyText->GetFont();
			Font.Size = FontSize;
			BodyText->SetFont(Font);
		}
		BodyText->SetAutoWrapText(true);
		BodyText->SetWrapTextAt(PanelWrapWidth);
		if (UVerticalBoxSlot* BodySlot = Column->AddChildToVerticalBox(BodyText))
		{
			BodySlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
		}

		PlayersHeaderText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TSDebugPlayersHeader"));
		PlayersHeaderText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.82f, 0.25f)));
		{
			FSlateFontInfo Font = PlayersHeaderText->GetFont();
			Font.Size = FontSize;
			Font.TypefaceFontName = FName(TEXT("Bold"));
			PlayersHeaderText->SetFont(Font);
		}
		if (UVerticalBoxSlot* PlayersHeaderSlot = Column->AddChildToVerticalBox(PlayersHeaderText))
		{
			PlayersHeaderSlot->SetPadding(FMargin(0.f, 10.f, 0.f, 2.f));
		}

		PlayerRowsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TSDebugPlayerRows"));
		Column->AddChildToVerticalBox(PlayerRowsBox);

		TankText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TSDebugTanks"));
		TankText->SetColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.78f, 0.86f)));
		{
			FSlateFontInfo Font = TankText->GetFont();
			Font.Size = FontSize;
			TankText->SetFont(Font);
		}
		TankText->SetAutoWrapText(true);
		TankText->SetWrapTextAt(PanelWrapWidth);
		if (UVerticalBoxSlot* TankSlot = Column->AddChildToVerticalBox(TankText))
		{
			TankSlot->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
		}

		// Host only (RefreshStartMatchButton collapses it otherwise). Without this the match could
		// only reach InProgress when every seat on every team was filled, so a short-handed lobby had
		// no way to start at all.
		StartMatchButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("TSDebugStartMatch"));
		StartMatchButton->SetVisibility(ESlateVisibility::Collapsed);
		UTSRoleDebugRowWidget::MakeButtonNonFocusable(StartMatchButton);
		{
			FButtonStyle Style = StartMatchButton->GetStyle();
			const FLinearColor Green(0.12f, 0.45f, 0.18f, 0.95f);
			Style.Normal.TintColor = FSlateColor(Green);
			Style.Hovered.TintColor = FSlateColor(Green * 1.6f);
			Style.Pressed.TintColor = FSlateColor(Green * 2.0f);
			StartMatchButton->SetStyle(Style);
		}
		StartMatchButton->OnClicked.AddDynamic(this, &UTSRoleDebugWidget::OnStartMatchClicked);

		UTextBlock* StartLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TSDebugStartMatchLabel"));
		StartLabel->SetText(FText::FromString(TEXT("START MATCH")));
		StartLabel->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		StartLabel->SetJustification(ETextJustify::Center);
		{
			FSlateFontInfo Font = StartLabel->GetFont();
			Font.Size = FontSize;
			Font.TypefaceFontName = FName(TEXT("Bold"));
			StartLabel->SetFont(Font);
		}
		StartMatchButton->SetContent(StartLabel);

		if (UVerticalBoxSlot* StartSlot = Column->AddChildToVerticalBox(StartMatchButton))
		{
			StartSlot->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));
			StartSlot->SetHorizontalAlignment(HAlign_Left);
		}
	}

	return Super::RebuildWidget();
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
	// Re-fit first: the window may have been resized since the last refresh, and the text below is
	// wrapped to whatever width this decides.
	UpdateResponsiveLayout();

	if (BodyText)
	{
		BodyText->SetText(FText::FromString(BuildDebugString()));
	}

	if (TankText)
	{
		TankText->SetVisibility(bShowTeamTanks ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (bShowTeamTanks)
		{
			TankText->SetText(FText::FromString(DescribeTeamTanks()));
		}
	}

	if (PlayersHeaderText)
	{
		const ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>();
		const bool bIsHost = PC && PC->IsMatchHost();
		PlayersHeaderText->SetText(FText::FromString(bIsHost
			? TEXT("PLAYERS - pick a team, then a seat")
			: TEXT("PLAYERS - the host assigns crews")));
	}

	RefreshPlayerRows();
	RefreshStartMatchButton();
}

void UTSRoleDebugWidget::RefreshPlayerRows()
{
	if (!PlayerRowsBox)
	{
		return;
	}

	if (!bShowAllPlayers)
	{
		PlayerRowsBox->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	PlayerRowsBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

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
			// give a row whose every assignment the GameMode rejects.
			if (!TankPS->IsHost())
			{
				Players.Add(TankPS);
			}
		}
	}

	// PlayerArray order is not stable across replication updates; sorting by the immutable player id
	// keeps each player on the same line so a button never moves out from under the cursor mid-click.
	Players.Sort([](const ATSTankPlayerState& A, const ATSTankPlayerState& B)
	{
		return A.GetPlayerId() < B.GetPlayerId();
	});

	while (PlayerRows.Num() < Players.Num())
	{
		UTSRoleDebugRowWidget* Row = CreateWidget<UTSRoleDebugRowWidget>(GetOwningPlayer(), UTSRoleDebugRowWidget::StaticClass());
		if (!Row)
		{
			break;
		}
		PlayerRows.Add(Row);
		PlayerRowsBox->AddChildToVerticalBox(Row);
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
			// Only re-point a row when it actually changed, so RefreshRow does not churn the buttons.
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
			// Surplus row from a player who left - keep it pooled but hidden.
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

FString UTSRoleDebugWidget::BuildDebugString() const
{
	TArray<FString> Lines;
	Lines.Add(DescribeNetContext());
	Lines.Add(DescribeLocalPlayer());
	return FString::Join(Lines, TEXT("\n"));
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

FString UTSRoleDebugWidget::DescribeTeamTanks() const
{
	const UWorld* World = GetWorld();
	const ATSGameState* GS = World ? World->GetGameState<ATSGameState>() : nullptr;
	if (!GS)
	{
		return TEXT("Team tanks: <no ATSGameState>");
	}

	TArray<FString> Lines;
	Lines.Add(TEXT("TEAM TANKS"));

	for (ETSTeamId TeamId : DebugTeams)
	{
		const APawn* Tank = GS->FindTankForTeam(TeamId);
		if (!Tank)
		{
			continue;
		}

		Lines.Add(FString::Printf(TEXT("  %s  -  %s"),
			*UTSTypeUtils::TeamIdToString(TeamId), *Tank->GetName()));

		const UTSTankCrewComponent* Crew = Tank->FindComponentByClass<UTSTankCrewComponent>();
		if (!Crew)
		{
			// Without the component there are no seats at all, so say that rather than printing three
			// empty ones and leaving it looking like nobody has picked a role yet.
			Lines.Add(TEXT("      <tank has no TSTankCrewComponent - add one to the tank Blueprint>"));
			continue;
		}

		// One line per seat, labelled. The previous single line relied on space padding to line the
		// names up under a "(Driver / Gunner / Commander)" heading, which a proportional font does not
		// honour - so which name sat in which seat was anyone's guess.
		for (const ETSCrewRole Role : { ETSCrewRole::Driver, ETSCrewRole::Gunner, ETSCrewRole::Commander })
		{
			Lines.Add(FString::Printf(TEXT("      %s: %s"),
				*UTSTypeUtils::CrewRoleToString(Role), *OccupantName(Crew, Role)));
		}
	}

	if (Lines.Num() == 1)
	{
		Lines.Add(TEXT("  <none yet - a team's tank spawns when you put someone on that team>"));
	}

	return FString::Join(Lines, TEXT("\n"));
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

	const float MaxWidth = FMath::Min(PanelWrapWidth, UsableWidth * MaxViewportWidthFraction);
	RootSizeBox->SetMaxDesiredWidth(MaxWidth);
	RootSizeBox->SetMaxDesiredHeight(UsableHeight * MaxViewportHeightFraction);

	// Wrap the prose to the width the panel actually got, not the design-time constant - otherwise
	// long lines still push past the edge inside a correctly-sized box.
	const float WrapAt = FMath::Max(MaxWidth - 24.f, 120.f);
	if (BodyText)
	{
		BodyText->SetWrapTextAt(WrapAt);
	}
	if (TankText)
	{
		TankText->SetWrapTextAt(WrapAt);
	}
}
