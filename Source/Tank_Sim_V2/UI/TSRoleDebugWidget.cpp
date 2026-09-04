#include "UI/TSRoleDebugWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
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
			return TEXT("-");
		}
		const APlayerState* Occupant = Crew->GetOccupant(Role);
		return Occupant ? Occupant->GetPlayerName() : TEXT("-");
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

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TSDebugColumn"));
		RootBorder->SetContent(Column);

		HeaderText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TSDebugHeader"));
		HeaderText->SetText(FText::FromString(TEXT("TANK SIM - CREW ASSIGNMENT")));
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
		if (UVerticalBoxSlot* TankSlot = Column->AddChildToVerticalBox(TankText))
		{
			TankSlot->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
		}
	}

	return Super::RebuildWidget();
}

void UTSRoleDebugWidget::NativeDestruct()
{
	// Hand input back if we were holding the cursor for assignment.
	if (bCursorTakenForAssignment)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			PC->SetShowMouseCursor(false);
			PC->SetInputMode(FInputModeGameOnly());
		}
		bCursorTakenForAssignment = false;
	}

	Super::NativeDestruct();
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
	UpdateInputModeForAssignment();
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
			Players.Add(TankPS);
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

void UTSRoleDebugWidget::UpdateInputModeForAssignment()
{
	if (!bTakeMouseCursorForAssignment)
	{
		return;
	}

	ATSTankPlayerController* PC = GetOwningPlayer<ATSTankPlayerController>();
	if (!PC || !PC->IsMatchHost())
	{
		return;
	}

	const ATSGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATSGameState>() : nullptr;
	const bool bAssignmentPhase = !GS || GS->GetMatchState() != ETSMatchState::InProgress;

	if (bAssignmentPhase == bCursorTakenForAssignment)
	{
		return;
	}

	bCursorTakenForAssignment = bAssignmentPhase;
	PC->SetShowMouseCursor(bAssignmentPhase);

	if (bAssignmentPhase)
	{
		// The defaults hide the cursor while a click is held and can lock it to the viewport, which
		// makes the assignment buttons awkward to hit. Keep it visible and free.
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
	}
	else
	{
		PC->SetInputMode(FInputModeGameOnly());
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
	return FString::Printf(TEXT("Me: %s  |  Team: %s  |  Role: %s  |  Tank: %s"),
		*PS->GetPlayerName(),
		*UTSTypeUtils::TeamIdToString(PS->GetTeamId()),
		*UTSTypeUtils::CrewRoleToString(PS->GetCrewRole()),
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
	Lines.Add(TEXT("TEAM TANKS (Driver / Gunner / Commander)"));

	for (ETSTeamId TeamId : DebugTeams)
	{
		const APawn* Tank = GS->FindTankForTeam(TeamId);
		if (!Tank)
		{
			continue;
		}

		const UTSTankCrewComponent* Crew = Tank->FindComponentByClass<UTSTankCrewComponent>();
		Lines.Add(FString::Printf(TEXT("  %-8s %-22s %s / %s / %s"),
			*UTSTypeUtils::TeamIdToString(TeamId),
			*Tank->GetName(),
			*OccupantName(Crew, ETSCrewRole::Driver),
			*OccupantName(Crew, ETSCrewRole::Gunner),
			*OccupantName(Crew, ETSCrewRole::Commander)));
	}

	if (Lines.Num() == 1)
	{
		Lines.Add(TEXT("  <none yet - a team's tank spawns when you put someone on that team>"));
	}

	return FString::Join(Lines, TEXT("\n"));
}
