#include "UI/TSHostVoicePanelWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "UI/TSRoleDebugRowWidget.h"
#include "UI/TSVoiceIndicatorWidget.h"
#include "Voice/TSVoiceRouterSubsystem.h"

namespace
{
	const TArray<ETSTeamId> PanelTeams = { ETSTeamId::TeamA, ETSTeamId::TeamB, ETSTeamId::TeamC, ETSTeamId::TeamD };

	const FLinearColor SelectionIdle(0.10f, 0.10f, 0.12f, 0.90f);
	const FLinearColor SelectionOn(0.62f, 0.32f, 0.10f, 0.95f);
	const FLinearColor HostPanelBackground(0.02f, 0.03f, 0.04f, 0.80f);
	const FLinearColor HostLabelColour(0.74f, 0.80f, 0.82f, 1.f);
	const FLinearColor HostDimColour(0.45f, 0.47f, 0.50f, 1.f);
	const FLinearColor HostLiveColour(0.35f, 0.95f, 0.45f, 1.f);
}

UTSHostVoicePanelWidget::UTSHostVoicePanelWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

UTextBlock* UTSHostVoicePanelWidget::MakeText(const FString& Text, int32 InFontSize, const FLinearColor& Colour)
{
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Block->SetText(FText::FromString(Text));
	Block->SetColorAndOpacity(FSlateColor(Colour));

	FSlateFontInfo Font = Block->GetFont();
	Font.Size = InFontSize;
	Block->SetFont(Font);
	return Block;
}

UButton* UTSHostVoicePanelWidget::MakeButton(const FString& Label, float MinWidth)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());

	// See UTSRoleDebugRowWidget: a focusable button swallows WASD under FInputModeGameAndUI once
	// clicked, which for the host means losing free-camera movement to a radio button.
	UTSRoleDebugRowWidget::MakeButtonNonFocusable(Button);

	UTextBlock* Text = MakeText(Label, FontSize - 1, FLinearColor::White);
	Text->SetJustification(ETextJustify::Center);
	Text->SetMinDesiredWidth(MinWidth);
	Button->SetContent(Text);

	return Button;
}

TSharedRef<SWidget> UTSHostVoicePanelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// A CanvasPanel so the panel can be anchored to the top RIGHT. The lobby console already owns
		// the top left, and the two overlapping would make both unreadable.
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass());
		WidgetTree->RootWidget = Canvas;

		RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		RootBorder->SetBrushColor(HostPanelBackground);
		RootBorder->SetPadding(FMargin(8.f, 6.f));

		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Canvas->AddChild(RootBorder)))
		{
			// Anchored to the top-right corner, so a negative X in ScreenPosition reads as "this far
			// in from the right edge" and the panel stays put at any resolution.
			CanvasSlot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
			CanvasSlot->SetAlignment(FVector2D(0.f, 0.f));
			CanvasSlot->SetPosition(ScreenPosition);
			CanvasSlot->SetSize(FVector2D(PanelWidth, 0.f));
			CanvasSlot->SetAutoSize(true);
		}

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		RootBorder->AddChild(Column);

		// Header row: title on the left, the host's own TX lamp on the right.
		{
			UHorizontalBox* HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

			HeaderText = MakeText(TEXT("COMMAND NET"), FontSize, HostLabelColour);
			if (UHorizontalBoxSlot* BoxSlot = Cast<UHorizontalBoxSlot>(HeaderRow->AddChildToHorizontalBox(HeaderText)))
			{
				BoxSlot->SetVerticalAlignment(VAlign_Center);
				BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}

			USizeBox* IndicatorBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			IndicatorBox->SetWidthOverride(IndicatorWidth + 20.f);
			IndicatorBox->SetHeightOverride(IndicatorHeight);

			SelfIndicator = WidgetTree->ConstructWidget<UTSVoiceIndicatorWidget>(UTSVoiceIndicatorWidget::StaticClass());
			// The host's own row is transmit-only: "am I keyed". Whether anyone is calling IN is
			// shown per commander, on their own row, which is more useful than one merged lamp.
			SelfIndicator->bShowReceive = false;
			IndicatorBox->AddChild(SelfIndicator);

			HeaderRow->AddChildToHorizontalBox(IndicatorBox);
			Column->AddChildToVerticalBox(HeaderRow);
		}

		RowsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Column->AddChildToVerticalBox(RowsBox);

		const int32 RowCount = FMath::Clamp(NumTeamRows, 1, PanelTeams.Num());
		TeamButtons.Reset();
		TeamLabels.Reset();
		TeamIndicators.Reset();

		for (int32 Index = 0; Index < RowCount; ++Index)
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

			UButton* Button = MakeButton(UTSTypeUtils::TeamIdToString(PanelTeams[Index]), 62.f);
			TeamButtons.Add(Button);
			Row->AddChildToHorizontalBox(Button);

			UTextBlock* Label = MakeText(TEXT(""), FontSize - 1, HostDimColour);
			TeamLabels.Add(Label);
			if (UHorizontalBoxSlot* BoxSlot = Cast<UHorizontalBoxSlot>(Row->AddChildToHorizontalBox(Label)))
			{
				BoxSlot->SetPadding(FMargin(6.f, 0.f));
				BoxSlot->SetVerticalAlignment(VAlign_Center);
				BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}

			// The indicator paints and carries no desired size, so the USizeBox is mandatory.
			USizeBox* IndicatorBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			IndicatorBox->SetWidthOverride(IndicatorWidth);
			IndicatorBox->SetHeightOverride(IndicatorHeight);

			UTSVoiceIndicatorWidget* Indicator = WidgetTree->ConstructWidget<UTSVoiceIndicatorWidget>(UTSVoiceIndicatorWidget::StaticClass());
			// Receive only: this lamp is that commander calling the host. What the host is sending
			// is one decision for the whole net, shown once in the header.
			Indicator->bShowTransmit = false;
			TeamIndicators.Add(Indicator);
			IndicatorBox->AddChild(Indicator);

			if (UHorizontalBoxSlot* BoxSlot = Cast<UHorizontalBoxSlot>(Row->AddChildToHorizontalBox(IndicatorBox)))
			{
				BoxSlot->SetVerticalAlignment(VAlign_Center);
			}

			if (UVerticalBoxSlot* ColumnSlot = Cast<UVerticalBoxSlot>(RowsBox->AddChildToVerticalBox(Row)))
			{
				ColumnSlot->SetPadding(FMargin(0.f, 2.f));
			}
		}

		// UButton::OnClicked carries no payload, so each team gets its own handler.
		if (TeamButtons.IsValidIndex(0) && TeamButtons[0]) { TeamButtons[0]->OnClicked.AddUniqueDynamic(this, &UTSHostVoicePanelWidget::OnTeamAClicked); }
		if (TeamButtons.IsValidIndex(1) && TeamButtons[1]) { TeamButtons[1]->OnClicked.AddUniqueDynamic(this, &UTSHostVoicePanelWidget::OnTeamBClicked); }
		if (TeamButtons.IsValidIndex(2) && TeamButtons[2]) { TeamButtons[2]->OnClicked.AddUniqueDynamic(this, &UTSHostVoicePanelWidget::OnTeamCClicked); }
		if (TeamButtons.IsValidIndex(3) && TeamButtons[3]) { TeamButtons[3]->OnClicked.AddUniqueDynamic(this, &UTSHostVoicePanelWidget::OnTeamDClicked); }

		// All / none, because "address every commander at once" is the most common thing a host
		// wants and ticking four boxes for it would be silly.
		{
			UHorizontalBox* ActionRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

			SelectAllButton = MakeButton(TEXT("ALL"), 52.f);
			SelectAllButton->OnClicked.AddUniqueDynamic(this, &UTSHostVoicePanelWidget::OnSelectAllClicked);
			ActionRow->AddChildToHorizontalBox(SelectAllButton);

			ClearButton = MakeButton(TEXT("NONE"), 52.f);
			ClearButton->OnClicked.AddUniqueDynamic(this, &UTSHostVoicePanelWidget::OnClearClicked);
			if (UHorizontalBoxSlot* BoxSlot = Cast<UHorizontalBoxSlot>(ActionRow->AddChildToHorizontalBox(ClearButton)))
			{
				BoxSlot->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
			}

			if (UVerticalBoxSlot* ColumnSlot = Cast<UVerticalBoxSlot>(Column->AddChildToVerticalBox(ActionRow)))
			{
				ColumnSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
			}
		}

		FooterText = MakeText(TEXT(""), FontSize - 2, HostDimColour);
		Column->AddChildToVerticalBox(FooterText);
	}

	return Super::RebuildWidget();
}

ATSTankPlayerController* UTSHostVoicePanelWidget::GetOwningTankController() const
{
	return Cast<ATSTankPlayerController>(GetOwningPlayer());
}

ATSTankPlayerState* UTSHostVoicePanelWidget::GetOwningTankPlayerState() const
{
	const APlayerController* PlayerController = GetOwningPlayer();
	return PlayerController ? Cast<ATSTankPlayerState>(PlayerController->PlayerState) : nullptr;
}

float UTSHostVoicePanelWidget::GetPulseSeconds() const
{
	if (const UWorld* World = GetWorld())
	{
		return World->GetRealTimeSeconds();
	}
	return static_cast<float>(FApp::GetCurrentTime());
}

void UTSHostVoicePanelWidget::ToggleTeam(ETSTeamId Team)
{
	if (ATSTankPlayerController* PlayerController = GetOwningTankController())
	{
		PlayerController->ToggleCommandVoiceTarget(Team);
	}
}

void UTSHostVoicePanelWidget::OnTeamAClicked() { ToggleTeam(ETSTeamId::TeamA); }
void UTSHostVoicePanelWidget::OnTeamBClicked() { ToggleTeam(ETSTeamId::TeamB); }
void UTSHostVoicePanelWidget::OnTeamCClicked() { ToggleTeam(ETSTeamId::TeamC); }
void UTSHostVoicePanelWidget::OnTeamDClicked() { ToggleTeam(ETSTeamId::TeamD); }

void UTSHostVoicePanelWidget::OnSelectAllClicked()
{
	if (ATSTankPlayerController* PlayerController = GetOwningTankController())
	{
		TArray<ETSTeamId> All;
		for (int32 Index = 0; Index < FMath::Clamp(NumTeamRows, 1, PanelTeams.Num()); ++Index)
		{
			All.Add(PanelTeams[Index]);
		}
		PlayerController->SetCommandVoiceTargets(All);
	}
}

void UTSHostVoicePanelWidget::OnClearClicked()
{
	if (ATSTankPlayerController* PlayerController = GetOwningTankController())
	{
		PlayerController->SetCommandVoiceTargets(TArray<ETSTeamId>());
	}
}

void UTSHostVoicePanelWidget::StyleSelectionButton(UButton* Button, bool bSelected) const
{
	if (!Button)
	{
		return;
	}

	const FLinearColor Tint = bSelected ? SelectionOn : SelectionIdle;

	FButtonStyle Style = Button->GetStyle();
	Style.Normal.TintColor = FSlateColor(Tint);
	Style.Hovered.TintColor = FSlateColor(Tint * 1.7f);
	Style.Pressed.TintColor = FSlateColor(SelectionOn);
	Button->SetStyle(Style);
}

void UTSHostVoicePanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshPanel();
}

void UTSHostVoicePanelWidget::RefreshPanel()
{
	const ATSTankPlayerController* PlayerController = GetOwningTankController();
	const ATSTankPlayerState* PlayerState = GetOwningTankPlayerState();
	const UTSVoiceRouterSubsystem* Router = UTSVoiceRouterSubsystem::Get(this);

	const float Pulse = GetPulseSeconds();
	const bool bTransmitting = PlayerState && PlayerState->IsVoiceTransmitting();

	if (SelfIndicator)
	{
		SelfIndicator->SetIndicatorState(bTransmitting, false, Pulse);
	}

	int32 SelectedCount = 0;

	for (int32 Index = 0; Index < TeamButtons.Num(); ++Index)
	{
		const ETSTeamId Team = PanelTeams[Index];
		const bool bSelected = PlayerState && PlayerState->IsAddressingTeam(Team);
		SelectedCount += bSelected ? 1 : 0;

		StyleSelectionButton(TeamButtons[Index], bSelected);

		const ATSTankPlayerState* Commander = Router ? Router->FindCommanderForTeam(Team) : nullptr;

		// Receiving is asked of the commander's own state, NOT of the host's selection: the host
		// always hears a commander who keys up on the command net, and hiding that until the team
		// happened to be ticked would make the panel lie about an incoming call.
		const bool bCommanderCalling = Router && Router->IsTeamCommanderTransmitting(Team);

		if (TeamIndicators.IsValidIndex(Index) && TeamIndicators[Index])
		{
			TeamIndicators[Index]->SetIndicatorState(false, bCommanderCalling, Pulse);
		}

		if (TeamLabels.IsValidIndex(Index) && TeamLabels[Index])
		{
			FString RowText;
			FLinearColor RowColour = HostDimColour;

			if (!Commander)
			{
				RowText = TEXT("no commander");
			}
			else if (bCommanderCalling)
			{
				RowText = Commander->GetPlayerName() + TEXT(" - calling");
				RowColour = HostLiveColour;
			}
			else
			{
				// Saying which net they are on matters: a commander sitting on CREW will still HEAR
				// the host (the host outranks the channel selector), but is listening to their tank
				// and may not answer straight away. That is worth knowing before wondering why.
				const bool bOnCommandNet = Commander->GetVoiceChannel() == ETSVoiceChannel::Command;
				RowText = FString::Printf(TEXT("%s - on %s"),
					*Commander->GetPlayerName(),
					bOnCommandNet ? TEXT("command") : TEXT("crew"));
				RowColour = bOnCommandNet ? HostLabelColour : HostDimColour;
			}

			TeamLabels[Index]->SetText(FText::FromString(RowText));
			TeamLabels[Index]->SetColorAndOpacity(FSlateColor(RowColour));
		}
	}

	if (FooterText)
	{
		const FString KeyName = PlayerController ? PlayerController->PushToTalkKeyDisplayName() : TEXT("PTT");
		FooterText->SetText(FText::FromString(SelectedCount == 0
			? FString::Printf(TEXT("select a team, then hold %s to talk"), *KeyName)
			: FString::Printf(TEXT("hold %s to talk to %d commander%s"), *KeyName, SelectedCount, SelectedCount == 1 ? TEXT("") : TEXT("s"))));
	}
}

FString UTSHostVoicePanelWidget::BuildPanelDebugString() const
{
	const ATSTankPlayerState* PlayerState = GetOwningTankPlayerState();

	FString Selected;
	if (PlayerState)
	{
		for (const ETSTeamId Team : PlayerState->GetCommandVoiceTargets())
		{
			Selected += UTSTypeUtils::TeamIdToString(Team) + TEXT(" ");
		}
	}

	return FString::Printf(TEXT("[HostVoicePanel] tx=%d rows=%d selected=[%s]"),
		PlayerState && PlayerState->IsVoiceTransmitting() ? 1 : 0,
		TeamButtons.Num(),
		*Selected.TrimEnd());
}
