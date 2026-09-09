#include "UI/TSRoleDebugRowWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/TextBlock.h"
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

	const FLinearColor ButtonIdle(0.10f, 0.10f, 0.12f, 0.90f);
	const FLinearColor ButtonCurrent(0.15f, 0.55f, 0.22f, 0.95f);
	const FLinearColor ButtonBlocked(0.35f, 0.10f, 0.10f, 0.75f);
}

UTSRoleDebugRowWidget::UTSRoleDebugRowWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// SelfHitTestInvisible, not HitTestInvisible: the row itself must not eat clicks, but its buttons
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

	FButtonStyle Style = Button->GetStyle();
	Style.Normal.TintColor = FSlateColor(ButtonIdle);
	Style.Hovered.TintColor = FSlateColor(ButtonIdle * 1.8f);
	Style.Pressed.TintColor = FSlateColor(ButtonCurrent);
	Button->SetStyle(Style);

	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(FText::FromString(Label));
	Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	{
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = FontSize - 1;
		Text->SetFont(Font);
	}
	Text->SetMinDesiredWidth(MinWidth);
	Text->SetJustification(ETextJustify::Center);
	Button->SetContent(Text);

	return Button;
}

TSharedRef<SWidget> UTSRoleDebugRowWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// A WRAP box, not a horizontal one. The fixed minimum widths here total ~730px (name 150 +
		// status 190 + four team + three role + two mode + clear), which cannot fit the panel once it
		// is capped to a fraction of a small window - the buttons simply ran off the right edge and
		// C/D and the role buttons were unreachable. A wrap box moves the overflow onto the next line
		// instead, so every button stays clickable at any width.
		UWrapBox* Row = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("TSDebugRow"));
		WidgetTree->RootWidget = Row;

		NameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		NameText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		{
			FSlateFontInfo Font = NameText->GetFont();
			Font.Size = FontSize;
			NameText->SetFont(Font);
		}
		// Reserve less, and ellipsize: a long machine name used to widen the whole row.
		NameText->SetMinDesiredWidth(110.f);
		NameText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
		if (UWrapBoxSlot* NameSlot = Row->AddChildToWrapBox(NameText))
		{
			NameSlot->SetVerticalAlignment(VAlign_Center);
			NameSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		}

		StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.78f, 0.86f)));
		{
			FSlateFontInfo Font = StatusText->GetFont();
			Font.Size = FontSize;
			StatusText->SetFont(Font);
		}
		StatusText->SetMinDesiredWidth(150.f);
		if (UWrapBoxSlot* StatusSlot = Row->AddChildToWrapBox(StatusText))
		{
			StatusSlot->SetVerticalAlignment(VAlign_Center);
			StatusSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
		}

		// One UFUNCTION per button rather than a payload-carrying delegate: UButton::OnClicked takes
		// no parameters, and a fixed set of handlers is less machinery than a handler UObject each.
		static const TCHAR* TeamLabels[] = { TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D") };
		for (int32 Index = 0; Index < RowTeams.Num(); ++Index)
		{
			UButton* Button = MakeButton(TeamLabels[Index], 22.f);
			switch (Index)
			{
			case 0: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnTeamAClicked); break;
			case 1: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnTeamBClicked); break;
			case 2: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnTeamCClicked); break;
			default: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnTeamDClicked); break;
			}
			TeamButtons.Add(Button);

			if (UWrapBoxSlot* ButtonSlot = Row->AddChildToWrapBox(Button))
			{
				ButtonSlot->SetPadding(FMargin(2.f, 1.f));
				ButtonSlot->SetVerticalAlignment(VAlign_Center);
			}
		}

		UTextBlock* Divider = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Divider->SetText(FText::FromString(TEXT("  ")));
		Row->AddChildToWrapBox(Divider);

		static const TCHAR* RoleLabels[] = { TEXT("Driver"), TEXT("Gunner"), TEXT("Cmdr") };
		for (int32 Index = 0; Index < RowRoles.Num(); ++Index)
		{
			UButton* Button = MakeButton(RoleLabels[Index], 52.f);
			switch (Index)
			{
			case 0: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnDriverClicked); break;
			case 1: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnGunnerClicked); break;
			default: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnCommanderClicked); break;
			}
			RoleButtons.Add(Button);

			if (UWrapBoxSlot* ButtonSlot = Row->AddChildToWrapBox(Button))
			{
				ButtonSlot->SetPadding(FMargin(2.f, 1.f));
				ButtonSlot->SetVerticalAlignment(VAlign_Center);
			}
		}

		UTextBlock* ModeDivider = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		ModeDivider->SetText(FText::FromString(TEXT("  ")));
		Row->AddChildToWrapBox(ModeDivider);

		static const TCHAR* PlayModeLabels[] = { TEXT("Desktop"), TEXT("VR") };
		for (int32 Index = 0; Index < RowPlayModes.Num(); ++Index)
		{
			UButton* Button = MakeButton(PlayModeLabels[Index], 52.f);
			switch (Index)
			{
			case 0: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnDesktopClicked); break;
			default: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnVRClicked); break;
			}
			PlayModeButtons.Add(Button);

			if (UWrapBoxSlot* ButtonSlot = Row->AddChildToWrapBox(Button))
			{
				ButtonSlot->SetPadding(FMargin(2.f, 1.f));
				ButtonSlot->SetVerticalAlignment(VAlign_Center);
			}
		}

		UTextBlock* DriveDivider = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		DriveDivider->SetText(FText::FromString(TEXT("  ")));
		Row->AddChildToWrapBox(DriveDivider);

		static const TCHAR* DriveModeLabels[] = { TEXT("Stick"), TEXT("Levers") };
		for (int32 Index = 0; Index < RowDriveModes.Num(); ++Index)
		{
			UButton* Button = MakeButton(DriveModeLabels[Index], 46.f);
			switch (Index)
			{
			case 0: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnAnalogClicked); break;
			default: Button->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnManualClicked); break;
			}
			DriveModeButtons.Add(Button);

			if (UWrapBoxSlot* ButtonSlot = Row->AddChildToWrapBox(Button))
			{
				ButtonSlot->SetPadding(FMargin(2.f, 1.f));
				ButtonSlot->SetVerticalAlignment(VAlign_Center);
			}
		}

		ClearButton = MakeButton(TEXT("Clear"), 42.f);
		ClearButton->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnClearClicked);
		if (UWrapBoxSlot* ClearSlot = Row->AddChildToWrapBox(ClearButton))
		{
			ClearSlot->SetPadding(FMargin(10.f, 1.f, 0.f, 1.f));
			ClearSlot->SetVerticalAlignment(VAlign_Center);
		}
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

	// Not the host, so the only row whose buttons are enabled is this player's own (see RefreshRow).
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

	if (NameText)
	{
		NameText->SetText(FText::FromString(FString::Printf(TEXT("%s %s"),
			bIsLocalPlayer ? TEXT(">") : TEXT(" "), *TargetPS->GetPlayerName())));
	}

	if (StatusText)
	{
		// The host has no body of either kind, so showing it a play mode would be a lie.
		StatusText->SetText(FText::FromString(TargetPS->IsHost()
			? FString::Printf(TEXT("%s / %s"),
				*UTSTypeUtils::TeamIdToString(CurrentTeam),
				*UTSTypeUtils::CrewRoleToString(CurrentRole))
			: FString::Printf(TEXT("%s / %s / %s"),
				*UTSTypeUtils::TeamIdToString(CurrentTeam),
				*UTSTypeUtils::CrewRoleToString(CurrentRole),
				CurrentPlayMode == ETSPlayMode::VR ? TEXT("VR") : TEXT("Desktop"))));
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

		FButtonStyle Style = Button->GetStyle();
		Style.Normal.TintColor = FSlateColor(RowTeams[Index] == CurrentTeam ? ButtonCurrent : ButtonIdle);
		Button->SetStyle(Style);
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

		FButtonStyle Style = Button->GetStyle();
		Style.Normal.TintColor = FSlateColor(bIsMine ? ButtonCurrent : (bTakenByOther ? ButtonBlocked : ButtonIdle));
		Button->SetStyle(Style);
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

		// Clickable by the host for anyone, and by a player for themselves - that second case is the
		// mid-match switch. The host's own row stays disabled: it holds no crew pawn to switch.
		Button->SetIsEnabled((bIsHost || bIsLocalPlayer) && !TargetPS->IsHost());

		FButtonStyle Style = Button->GetStyle();
		Style.Normal.TintColor = FSlateColor(bIsMine ? ButtonCurrent : ButtonIdle);
		Button->SetStyle(Style);
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
		const bool bModeAvailable = (Mode == ETSDriveControlMode::Analog)
			|| TargetPS->GetPlayMode() == ETSPlayMode::VR;

		Button->SetIsEnabled((bIsHost || bIsLocalPlayer) && !TargetPS->IsHost() && bModeAvailable);

		FButtonStyle Style = Button->GetStyle();
		Style.Normal.TintColor = FSlateColor(bIsMine ? ButtonCurrent : ButtonIdle);
		Button->SetStyle(Style);
	}

	if (ClearButton)
	{
		ClearButton->SetIsEnabled(bIsHost && CurrentTeam != ETSTeamId::None);
	}
}
