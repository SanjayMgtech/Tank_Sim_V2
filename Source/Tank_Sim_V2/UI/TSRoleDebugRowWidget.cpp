#include "UI/TSRoleDebugRowWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Core/TSGameState.h"
#include "GameFramework/PlayerState.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCrewComponent.h"

namespace
{
	const TArray<ETSTeamId> RowTeams = { ETSTeamId::TeamA, ETSTeamId::TeamB, ETSTeamId::TeamC, ETSTeamId::TeamD };
	const TArray<ETSCrewRole> RowRoles = { ETSCrewRole::Driver, ETSCrewRole::Gunner, ETSCrewRole::Commander };

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

UButton* UTSRoleDebugRowWidget::MakeButton(const FString& Label, float MinWidth)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());

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
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TSDebugRow"));
		WidgetTree->RootWidget = Row;

		NameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		NameText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		{
			FSlateFontInfo Font = NameText->GetFont();
			Font.Size = FontSize;
			NameText->SetFont(Font);
		}
		NameText->SetMinDesiredWidth(150.f);
		if (UHorizontalBoxSlot* NameSlot = Row->AddChildToHorizontalBox(NameText))
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
		StatusText->SetMinDesiredWidth(190.f);
		if (UHorizontalBoxSlot* StatusSlot = Row->AddChildToHorizontalBox(StatusText))
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

			if (UHorizontalBoxSlot* ButtonSlot = Row->AddChildToHorizontalBox(Button))
			{
				ButtonSlot->SetPadding(FMargin(2.f, 1.f));
				ButtonSlot->SetVerticalAlignment(VAlign_Center);
			}
		}

		UTextBlock* Divider = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Divider->SetText(FText::FromString(TEXT("  ")));
		Row->AddChildToHorizontalBox(Divider);

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

			if (UHorizontalBoxSlot* ButtonSlot = Row->AddChildToHorizontalBox(Button))
			{
				ButtonSlot->SetPadding(FMargin(2.f, 1.f));
				ButtonSlot->SetVerticalAlignment(VAlign_Center);
			}
		}

		ClearButton = MakeButton(TEXT("Clear"), 42.f);
		ClearButton->OnClicked.AddDynamic(this, &UTSRoleDebugRowWidget::OnClearClicked);
		if (UHorizontalBoxSlot* ClearSlot = Row->AddChildToHorizontalBox(ClearButton))
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

	if (NameText)
	{
		NameText->SetText(FText::FromString(FString::Printf(TEXT("%s %s"),
			bIsLocalPlayer ? TEXT(">") : TEXT(" "), *TargetPS->GetPlayerName())));
	}

	if (StatusText)
	{
		StatusText->SetText(FText::FromString(FString::Printf(TEXT("%s / %s"),
			*UTSTypeUtils::TeamIdToString(CurrentTeam),
			*UTSTypeUtils::CrewRoleToString(CurrentRole))));
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

	if (ClearButton)
	{
		ClearButton->SetIsEnabled(bIsHost && CurrentTeam != ETSTeamId::None);
	}
}
