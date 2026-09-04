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
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCrewComponent.h"

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
	// Purely informational - it must never eat a click or a VR laser trace.
	SetVisibility(ESlateVisibility::HitTestInvisible);
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
		RootBorder->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.65f));
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
		HeaderText->SetText(FText::FromString(TEXT("TANK SIM - ROLE DEBUG")));
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
	if (BodyText)
	{
		BodyText->SetText(FText::FromString(BuildDebugString()));
	}
}

FString UTSRoleDebugWidget::BuildDebugString() const
{
	TArray<FString> Lines;
	Lines.Add(DescribeNetContext());
	Lines.Add(DescribeLocalPlayer());

	if (bShowAllPlayers)
	{
		Lines.Add(FString());
		Lines.Add(DescribeAllPlayers());
	}

	if (bShowTeamTanks)
	{
		Lines.Add(FString());
		Lines.Add(DescribeTeamTanks());
	}

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

FString UTSRoleDebugWidget::DescribeAllPlayers() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	if (!GS)
	{
		return TEXT("Players: <no GameState>");
	}

	const APlayerState* LocalPS = GetOwningPlayerState();

	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Players (%d)"), GS->PlayerArray.Num()));

	for (const APlayerState* PlayerState : GS->PlayerArray)
	{
		const ATSTankPlayerState* TankPS = Cast<ATSTankPlayerState>(PlayerState);
		if (!TankPS)
		{
			continue;
		}

		const APawn* Tank = TankPS->GetAssignedTank();
		Lines.Add(FString::Printf(TEXT("  %s %-16s  %-8s  %-10s  %s"),
			(PlayerState == LocalPS) ? TEXT(">") : TEXT(" "),
			*TankPS->GetPlayerName(),
			*UTSTypeUtils::TeamIdToString(TankPS->GetTeamId()),
			*UTSTypeUtils::CrewRoleToString(TankPS->GetCrewRole()),
			Tank ? *Tank->GetName() : TEXT("no tank")));
	}

	return FString::Join(Lines, TEXT("\n"));
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
	Lines.Add(TEXT("Team tanks (D / G / C)"));

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
		Lines.Add(TEXT("  <none replicated - check the GameMode's Default Tank Class>"));
	}

	return FString::Join(Lines, TEXT("\n"));
}
