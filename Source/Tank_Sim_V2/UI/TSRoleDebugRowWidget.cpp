#include "UI/TSRoleDebugRowWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Core/TSGameState.h"
#include "GameFramework/PlayerState.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCrewComponent.h"
#include "UObject/UnrealType.h"

namespace
{
	const TArray<ETSTeamId> RowTeams = { ETSTeamId::TeamA, ETSTeamId::TeamB, ETSTeamId::TeamC, ETSTeamId::TeamD };
	const TArray<ETSCrewRole> RowRoles = { ETSCrewRole::Driver, ETSCrewRole::Gunner, ETSCrewRole::Commander };
	const TArray<ETSPlayMode> RowPlayModes = { ETSPlayMode::Desktop, ETSPlayMode::VR };
	const TArray<ETSDriveControlMode> RowDriveModes = { ETSDriveControlMode::Analog, ETSDriveControlMode::Manual };
}

FLinearColor FTSLobbyConsoleStyle::GetTeamColor(ETSTeamId Team) const
{
	// ETSTeamId: None = 0, TeamA = 1 ... TeamD = 4.
	const int32 Index = static_cast<int32>(Team) - 1;
	return TeamColors.IsValidIndex(Index) ? TeamColors[Index] : NoTeamColor;
}

FSlateBrush TSLobbyConsoleUI::MakeRoundedBrush(const FLinearColor& Color, float Radius)
{
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(Color);
	Brush.OutlineSettings.CornerRadii = FVector4(Radius, Radius, Radius, Radius);
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Brush.OutlineSettings.Width = 0.f;
	return Brush;
}

void TSLobbyConsoleUI::ApplyButtonColor(UButton* Button, const FLinearColor& Base, const FTSLobbyConsoleStyle& Style)
{
	if (!Button)
	{
		return;
	}

	const float Radius = FMath::Max(Style.CornerRadius - 2.f, 0.f);
	FButtonStyle ButtonStyle = Button->GetStyle();
	ButtonStyle.Normal = MakeRoundedBrush(Base, Radius);
	ButtonStyle.Hovered = MakeRoundedBrush(Base * 1.45f, Radius);
	ButtonStyle.Pressed = MakeRoundedBrush(Base * 0.75f, Radius);
	ButtonStyle.Disabled = MakeRoundedBrush(Style.ButtonDisabledColor, Radius);
	ButtonStyle.NormalPadding = FMargin(8.f, 3.f);
	// Same as Normal, so the label does not jump when pressed.
	ButtonStyle.PressedPadding = FMargin(8.f, 3.f);
	Button->SetStyle(ButtonStyle);
}

void TSLobbyConsoleUI::SetFont(UTextBlock* Text, int32 Size, bool bBold, int32 LetterSpacing)
{
	if (!Text)
	{
		return;
	}
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = Size;
	Font.TypefaceFontName = bBold ? FName(TEXT("Bold")) : FName(TEXT("Regular"));
	Font.LetterSpacing = LetterSpacing;
	Text->SetFont(Font);
}

UTSRoleDebugRowWidget::UTSRoleDebugRowWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// SelfHitTestInvisible, not HitTestInvisible: the card itself must not eat clicks, but its buttons
	// have to stay clickable.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UTSRoleDebugRowWidget::MakeButtonNonFocusable(UButton* Button)
{
	if (!Button)
	{
		return;
	}
	if (FBoolProperty* Prop = FindFProperty<FBoolProperty>(UButton::StaticClass(), TEXT("IsFocusable")))
	{
		Prop->SetPropertyValue_InContainer(Button, false);
	}
}

UButton* UTSRoleDebugRowWidget::MakeButton(const FString& Label, float MinWidth)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	MakeButtonNonFocusable(Button);
	TSLobbyConsoleUI::ApplyButtonColor(Button, ConsoleStyle.ButtonIdleColor, ConsoleStyle);

	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(FText::FromString(Label));
	Text->SetColorAndOpacity(FSlateColor(ConsoleStyle.TextColor));
	TSLobbyConsoleUI::SetFont(Text, ConsoleStyle.FontSize - 1, false);
	Text->SetMinDesiredWidth(MinWidth);
	Text->SetJustification(ETextJustify::Center);
	Button->SetContent(Text);

	return Button;
}

UHorizontalBox* UTSRoleDebugRowWidget::MakeGroup(const FString& Label)
{
	// Label and its buttons as ONE wrap-box child, so a narrow window wraps whole groups and a
	// "SEAT" label never ends up on one line with its buttons on the next.
	UHorizontalBox* Group = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(FText::FromString(Label));
	Text->SetColorAndOpacity(FSlateColor(ConsoleStyle.MutedTextColor));
	TSLobbyConsoleUI::SetFont(Text, ConsoleStyle.FontSize - 3, true, 120);
	if (UHorizontalBoxSlot* LabelSlot = Group->AddChildToHorizontalBox(Text))
	{
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetPadding(FMargin(0.f, 0.f, 5.f, 0.f));
	}

	return Group;
}

UBorder* UTSRoleDebugRowWidget::MakeBadge(TObjectPtr<UTextBlock>& OutText)
{
	UBorder* Badge = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Badge->SetPadding(FMargin(7.f, 1.f));
	Badge->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	OutText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	TSLobbyConsoleUI::SetFont(OutText, ConsoleStyle.FontSize - 2, true, 60);
	Badge->SetContent(OutText);
	return Badge;
}

void UTSRoleDebugRowWidget::SetBadge(UBorder* Badge, UTextBlock* Text, const FString& Label, const FLinearColor& Color)
{
	if (!Badge || !Text)
	{
		return;
	}
	// A dim fill in the badge colour with the label in the full colour reads as a tag, not a button.
	FLinearColor Fill = Color * 0.28f;
	Fill.A = 0.95f;
	Badge->SetBrush(TSLobbyConsoleUI::MakeRoundedBrush(Fill, 9.f));
	Text->SetColorAndOpacity(FSlateColor(Color));
	Text->SetText(FText::FromString(Label));
}

TSharedRef<SWidget> UTSRoleDebugRowWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// Card: [stripe | name + badges / grouped controls]
		CardBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TSLobbyCard"));
		CardBorder->SetBrush(TSLobbyConsoleUI::MakeRoundedBrush(ConsoleStyle.CardColor, ConsoleStyle.CornerRadius));
		CardBorder->SetPadding(FMargin(0.f));
		CardBorder->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = CardBorder;

		UHorizontalBox* CardRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		CardBorder->SetContent(CardRow);

		USizeBox* StripeSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		StripeSize->SetWidthOverride(5.f);
		TeamStripe = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		StripeSize->AddChild(TeamStripe);
		if (UHorizontalBoxSlot* StripeSlot = CardRow->AddChildToHorizontalBox(StripeSize))
		{
			StripeSlot->SetVerticalAlignment(VAlign_Fill);
		}

		UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		if (UHorizontalBoxSlot* BodySlot = CardRow->AddChildToHorizontalBox(Body))
		{
			BodySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			BodySlot->SetPadding(FMargin(10.f, 7.f, 10.f, 8.f));
		}

		// --- Line 1: name, then the three badges that answer "where is this player?" at a glance.
		UHorizontalBox* TitleLine = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Body->AddChildToVerticalBox(TitleLine);

		NameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		NameText->SetColorAndOpacity(FSlateColor(ConsoleStyle.TextColor));
		TSLobbyConsoleUI::SetFont(NameText, ConsoleStyle.FontSize + 1, true);
		// Ellipsize: a long machine name used to widen the whole console.
		NameText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		if (UHorizontalBoxSlot* NameSlot = TitleLine->AddChildToHorizontalBox(NameText))
		{
			NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			NameSlot->SetVerticalAlignment(VAlign_Center);
			NameSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
		}

		for (TPair<TObjectPtr<UBorder>*, TObjectPtr<UTextBlock>*> Badge : {
			TPair<TObjectPtr<UBorder>*, TObjectPtr<UTextBlock>*>(&TeamBadge, &TeamBadgeText),
			TPair<TObjectPtr<UBorder>*, TObjectPtr<UTextBlock>*>(&SeatBadge, &SeatBadgeText),
			TPair<TObjectPtr<UBorder>*, TObjectPtr<UTextBlock>*>(&ModeBadge, &ModeBadgeText) })
		{
			*Badge.Key = MakeBadge(*Badge.Value);
			if (UHorizontalBoxSlot* BadgeSlot = TitleLine->AddChildToHorizontalBox(*Badge.Key))
			{
				BadgeSlot->SetVerticalAlignment(VAlign_Center);
				BadgeSlot->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
			}
		}

		// --- Line 2: labelled button groups. A WRAP box: the groups total far more than a small
		// window's width, and a horizontal box simply ran the last buttons off the edge.
		ControlsBox = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass());
		ControlsBox->SetInnerSlotPadding(FVector2D(14.f, 4.f));
		if (UVerticalBoxSlot* ControlsSlot = Body->AddChildToVerticalBox(ControlsBox))
		{
			ControlsSlot->SetPadding(FMargin(0.f, 7.f, 0.f, 0.f));
		}

		auto AddButtonToGroup = [](UHorizontalBox* Group, UButton* Button)
		{
			if (UHorizontalBoxSlot* ButtonSlot = Group->AddChildToHorizontalBox(Button))
			{
				ButtonSlot->SetPadding(FMargin(2.f, 0.f));
				ButtonSlot->SetVerticalAlignment(VAlign_Center);
			}
		};

		// One UFUNCTION per button rather than a payload-carrying delegate: UButton::OnClicked takes
		// no parameters, and a fixed set of handlers is less machinery than a handler UObject each.
		TeamGroup = MakeGroup(TEXT("TEAM"));
		static const TCHAR* TeamLabels[] = { TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D") };
		for (int32 Index = 0; Index < RowTeams.Num(); ++Index)
		{
			UButton* Button = MakeButton(TeamLabels[Index], 18.f);
			switch (Index)
			{
			case 0: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnTeamAClicked); break;
			case 1: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnTeamBClicked); break;
			case 2: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnTeamCClicked); break;
			default: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnTeamDClicked); break;
			}
			TeamButtons.Add(Button);
			AddButtonToGroup(TeamGroup, Button);
		}
		ControlsBox->AddChildToWrapBox(TeamGroup);

		SeatGroup = MakeGroup(TEXT("SEAT"));
		static const TCHAR* RoleLabels[] = { TEXT("Driver"), TEXT("Gunner"), TEXT("Commander") };
		for (int32 Index = 0; Index < RowRoles.Num(); ++Index)
		{
			UButton* Button = MakeButton(RoleLabels[Index], 50.f);
			switch (Index)
			{
			case 0: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnDriverClicked); break;
			case 1: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnGunnerClicked); break;
			default: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnCommanderClicked); break;
			}
			RoleButtons.Add(Button);
			AddButtonToGroup(SeatGroup, Button);
		}
		ControlsBox->AddChildToWrapBox(SeatGroup);

		BodyGroup = MakeGroup(TEXT("PLAY ON"));
		static const TCHAR* PlayModeLabels[] = { TEXT("Desktop"), TEXT("VR") };
		for (int32 Index = 0; Index < RowPlayModes.Num(); ++Index)
		{
			UButton* Button = MakeButton(PlayModeLabels[Index], 44.f);
			switch (Index)
			{
			case 0: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnDesktopClicked); break;
			default: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnVRClicked); break;
			}
			PlayModeButtons.Add(Button);
			AddButtonToGroup(BodyGroup, Button);
		}
		ControlsBox->AddChildToWrapBox(BodyGroup);

		DriveGroup = MakeGroup(TEXT("DRIVE WITH"));
		static const TCHAR* DriveModeLabels[] = { TEXT("Stick"), TEXT("Levers") };
		for (int32 Index = 0; Index < RowDriveModes.Num(); ++Index)
		{
			UButton* Button = MakeButton(DriveModeLabels[Index], 40.f);
			switch (Index)
			{
			case 0: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnAnalogClicked); break;
			default: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnManualClicked); break;
			}
			DriveModeButtons.Add(Button);
			AddButtonToGroup(DriveGroup, Button);
		}
		ControlsBox->AddChildToWrapBox(DriveGroup);

		ClearButton = MakeButton(TEXT("Unassign"), 50.f);
		TSLobbyConsoleUI::ApplyButtonColor(ClearButton, ConsoleStyle.ButtonBlockedColor * 0.8f, ConsoleStyle);
		ClearButton->SetToolTipText(FText::FromString(TEXT("Remove this player from their team and seat.")));
		ClearButton->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnClearClicked);
		ControlsBox->AddChildToWrapBox(ClearButton);
	}

	return Super::RebuildWidget();
}

void UTSRoleDebugRowWidget::SetTargetPlayerState(APlayerState* InPlayerState)
{
	TargetPlayerState = InPlayerState;
	RefreshRow();
}

APlayerState* UTSRoleDebugRowWidget::GetTargetPlayerState() const
{
	return TargetPlayerState.Get();
}

ATSTankPlayerController* UTSRoleDebugRowWidget::GetOwningTankController() const
{
	return GetOwningPlayer<ATSTankPlayerController>();
}

void UTSRoleDebugRowWidget::AssignTeam(ETSTeamId Team)
{
	if (ATSTankPlayerController* PC = GetOwningTankController())
	{
		if (APlayerState* Target = TargetPlayerState.Get())
		{
			PC->ServerHostAssignPlayerToTeam(Target, Team);
		}
	}
}

void UTSRoleDebugRowWidget::AssignRole(ETSCrewRole Role)
{
	if (ATSTankPlayerController* PC = GetOwningTankController())
	{
		if (APlayerState* Target = TargetPlayerState.Get())
		{
			PC->ServerHostAssignPlayerToRole(Target, Role);
		}
	}
}

void UTSRoleDebugRowWidget::OnTeamAClicked() { AssignTeam(ETSTeamId::TeamA); }
void UTSRoleDebugRowWidget::OnTeamBClicked() { AssignTeam(ETSTeamId::TeamB); }
void UTSRoleDebugRowWidget::OnTeamCClicked() { AssignTeam(ETSTeamId::TeamC); }
void UTSRoleDebugRowWidget::OnTeamDClicked() { AssignTeam(ETSTeamId::TeamD); }
void UTSRoleDebugRowWidget::OnDriverClicked() { AssignRole(ETSCrewRole::Driver); }
void UTSRoleDebugRowWidget::OnGunnerClicked() { AssignRole(ETSCrewRole::Gunner); }
void UTSRoleDebugRowWidget::OnCommanderClicked() { AssignRole(ETSCrewRole::Commander); }

void UTSRoleDebugRowWidget::AssignPlayMode(ETSPlayMode PlayMode)
{
	ATSTankPlayerController* PC = GetOwningTankController();
	APlayerState* Target = TargetPlayerState.Get();
	if (!PC || !Target)
	{
		return;
	}

	if (PC->IsMatchHost())
	{
		PC->ServerHostAssignPlayerToPlayMode(Target, PlayMode);
		return;
	}

	// Not the host, so the only card whose buttons are enabled is this player's own (see RefreshRow).
	// Their own body is theirs to move, and ServerSetPlayMode changes nobody else's.
	if (PC->PlayerState == Target)
	{
		PC->ServerSetPlayMode(PlayMode);
	}
}

void UTSRoleDebugRowWidget::OnDesktopClicked() { AssignPlayMode(ETSPlayMode::Desktop); }
void UTSRoleDebugRowWidget::OnVRClicked() { AssignPlayMode(ETSPlayMode::VR); }

void UTSRoleDebugRowWidget::OnAnalogClicked() { AssignDriveControlMode(ETSDriveControlMode::Analog); }
void UTSRoleDebugRowWidget::OnManualClicked() { AssignDriveControlMode(ETSDriveControlMode::Manual); }

void UTSRoleDebugRowWidget::AssignDriveControlMode(ETSDriveControlMode Mode)
{
	ATSTankPlayerController* PC = GetOwningTankController();
	APlayerState* Target = TargetPlayerState.Get();
	if (!PC || !Target)
	{
		return;
	}

	if (PC->IsMatchHost())
	{
		PC->ServerHostAssignPlayerToDriveControlMode(Target, Mode);
		return;
	}

	// A player may switch their OWN scheme; the server refuses anything else.
	if (Target == PC->PlayerState)
	{
		PC->ServerSetDriveControlMode(Mode);
	}
}

void UTSRoleDebugRowWidget::OnClearClicked()
{
	if (ATSTankPlayerController* PC = GetOwningTankController())
	{
		if (APlayerState* Target = TargetPlayerState.Get())
		{
			PC->ServerHostClearPlayerAssignment(Target);
		}
	}
}

void UTSRoleDebugRowWidget::RefreshRow()
{
	const ATSTankPlayerState* TargetPS = Cast<ATSTankPlayerState>(TargetPlayerState.Get());
	if (!TargetPS)
	{
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	const ATSTankPlayerController* PC = GetOwningTankController();
	const bool bIsHost = PC && PC->IsMatchHost();
	const bool bIsLocalPlayer = PC && PC->PlayerState == TargetPS;

	const ETSTeamId CurrentTeam = TargetPS->GetTeamId();
	const ETSCrewRole CurrentRole = TargetPS->GetCrewRole();
	const ETSPlayMode CurrentPlayMode = TargetPS->GetPlayMode();
	const bool bHasHeadset = TargetPS->HasHeadsetConnected();
	const FLinearColor TeamColor = ConsoleStyle.GetTeamColor(CurrentTeam);

	if (NameText)
	{
		NameText->SetText(FText::FromString(bIsLocalPlayer
			? FString::Printf(TEXT("%s  (you)"), *TargetPS->GetPlayerName())
			: TargetPS->GetPlayerName()));
	}

	if (TeamStripe)
	{
		TeamStripe->SetBrush(TSLobbyConsoleUI::MakeRoundedBrush(TeamColor, 2.f));
	}

	// Three tags that answer the host's actual question - "is this player ready to play?" - without
	// reading a sentence. Unset values are shown as such, in the muted colour.
	SetBadge(TeamBadge, TeamBadgeText,
		CurrentTeam == ETSTeamId::None ? TEXT("NO TEAM") : UTSTypeUtils::TeamIdToString(CurrentTeam).ToUpper(),
		TeamColor);
	SetBadge(SeatBadge, SeatBadgeText,
		CurrentRole == ETSCrewRole::None ? TEXT("NO SEAT") : UTSTypeUtils::CrewRoleToString(CurrentRole).ToUpper(),
		CurrentRole == ETSCrewRole::None ? ConsoleStyle.NoTeamColor : ConsoleStyle.ButtonSelectedColor * 1.7f);
	SetBadge(ModeBadge, ModeBadgeText,
		CurrentPlayMode == ETSPlayMode::VR ? TEXT("VR") : (bHasHeadset ? TEXT("DESKTOP") : TEXT("DESKTOP · NO HMD")),
		CurrentPlayMode == ETSPlayMode::VR ? FLinearColor(0.72f, 0.5f, 1.f) : ConsoleStyle.MutedTextColor);

	// Viewers only see controls they can use. A client watching the lobby sees the roster forming,
	// and their own card keeps the Desktop/VR and drive-scheme choices, which are theirs to make.
	const bool bCanEditBody = (bIsHost || bIsLocalPlayer) && !TargetPS->IsHost();
	const ESlateVisibility HostOnly = bIsHost ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	if (TeamGroup) { TeamGroup->SetVisibility(HostOnly); }
	if (SeatGroup) { SeatGroup->SetVisibility(HostOnly); }
	if (ClearButton) { ClearButton->SetVisibility(bIsHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	const ESlateVisibility BodyVisibility = bCanEditBody ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	if (BodyGroup) { BodyGroup->SetVisibility(BodyVisibility); }
	// Stick/Levers is only a choice for someone who drives.
	if (DriveGroup) { DriveGroup->SetVisibility(CurrentRole == ETSCrewRole::Driver ? BodyVisibility : ESlateVisibility::Collapsed); }
	if (ControlsBox)
	{
		ControlsBox->SetVisibility((bIsHost || bCanEditBody) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}

	// Seat occupancy on this player's team tank, so a seat somebody else already holds reads as
	// blocked instead of inviting a click the server will only reject.
	const ATSGameState* GS = GetWorld() ? GetWorld()->GetGameState<ATSGameState>() : nullptr;
	const APawn* TeamTank = (GS && CurrentTeam != ETSTeamId::None) ? GS->FindTankForTeam(CurrentTeam) : nullptr;
	const UTSTankCrewComponent* Crew = TeamTank ? TeamTank->FindComponentByClass<UTSTankCrewComponent>() : nullptr;

	for (int32 Index = 0; Index < TeamButtons.Num(); ++Index)
	{
		UButton* Button = TeamButtons[Index];
		if (!Button)
		{
			continue;
		}

		const bool bVisible = Index < NumTeamButtons;
		Button->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		Button->SetIsEnabled(bVisible && bIsHost);

		// The chosen team lights in ITS OWN colour, so the button, stripe and badge all agree.
		const bool bIsMine = RowTeams[Index] == CurrentTeam;
		TSLobbyConsoleUI::ApplyButtonColor(Button,
			bIsMine ? ConsoleStyle.GetTeamColor(RowTeams[Index]) * 0.7f : ConsoleStyle.ButtonIdleColor, ConsoleStyle);
	}

	for (int32 Index = 0; Index < RoleButtons.Num(); ++Index)
	{
		UButton* Button = RoleButtons[Index];
		if (!Button)
		{
			continue;
		}

		const ETSCrewRole Role = RowRoles[Index];
		const bool bIsMine = Role == CurrentRole;
		// Held by somebody else on the same tank.
		const bool bTakenByOther = Crew && Crew->IsRoleOccupied(Role) && !bIsMine;
		// A seat only exists once the player is on a team.
		const bool bSelectable = bIsHost && CurrentTeam != ETSTeamId::None && !bTakenByOther;

		Button->SetIsEnabled(bSelectable);
		TSLobbyConsoleUI::ApplyButtonColor(Button,
			bIsMine ? ConsoleStyle.ButtonSelectedColor : (bTakenByOther ? ConsoleStyle.ButtonBlockedColor : ConsoleStyle.ButtonIdleColor),
			ConsoleStyle);

		const APlayerState* Occupant = (Crew && bTakenByOther) ? Crew->GetOccupant(Role) : nullptr;
		Button->SetToolTipText(CurrentTeam == ETSTeamId::None
			? FText::FromString(TEXT("Put this player on a team first."))
			: (Occupant ? FText::FromString(FString::Printf(TEXT("Taken by %s."), *Occupant->GetPlayerName())) : FText::GetEmpty()));
	}

	for (int32 Index = 0; Index < PlayModeButtons.Num(); ++Index)
	{
		UButton* Button = PlayModeButtons[Index];
		if (!Button)
		{
			continue;
		}

		const ETSPlayMode PlayMode = RowPlayModes[Index];
		const bool bIsMine = PlayMode == CurrentPlayMode;

		// VR needs a headset on THAT player's machine, which their client reports up. Without one the
		// button is disabled rather than merely refused - a misclick that puts somebody into a stereo
		// mode they cannot render is what hung the GPU, so the safest place to stop it is before the
		// click. Shown blocked (red), not hidden, so the host can see why it is unavailable.
		const bool bNeedsHeadset = (PlayMode == ETSPlayMode::VR) && !bHasHeadset;

		// Clickable by the host for anyone, and by a player for themselves - that second case is the
		// mid-match switch. The host's own card stays disabled: it holds no crew pawn to switch.
		Button->SetIsEnabled(bCanEditBody && !bNeedsHeadset);
		TSLobbyConsoleUI::ApplyButtonColor(Button,
			bIsMine ? ConsoleStyle.ButtonSelectedColor : (bNeedsHeadset ? ConsoleStyle.ButtonBlockedColor : ConsoleStyle.ButtonIdleColor),
			ConsoleStyle);
		Button->SetToolTipText(bNeedsHeadset
			? FText::FromString(TEXT("No headset connected on that player's machine."))
			: FText::GetEmpty());
	}

	for (int32 Index = 0; Index < DriveModeButtons.Num(); ++Index)
	{
		UButton* Button = DriveModeButtons[Index];
		if (!Button)
		{
			continue;
		}

		const ETSDriveControlMode Mode = RowDriveModes[Index];
		const bool bIsMine = Mode == TargetPS->GetDriveControlMode();

		// Manual needs VR hands, so it is disabled for a Desktop player rather than offered and then
		// refused by the server - a button that does nothing when clicked reads as a bug.
		const bool bModeAvailable = (Mode == ETSDriveControlMode::Analog) || CurrentPlayMode == ETSPlayMode::VR;

		Button->SetIsEnabled(bCanEditBody && bModeAvailable);
		TSLobbyConsoleUI::ApplyButtonColor(Button,
			bIsMine ? ConsoleStyle.ButtonSelectedColor : ConsoleStyle.ButtonIdleColor, ConsoleStyle);
		Button->SetToolTipText(bModeAvailable
			? FText::GetEmpty()
			: FText::FromString(TEXT("Levers need VR hands - switch this player to VR first.")));
	}

	if (ClearButton)
	{
		ClearButton->SetIsEnabled(bIsHost && CurrentTeam != ETSTeamId::None);
	}
}
