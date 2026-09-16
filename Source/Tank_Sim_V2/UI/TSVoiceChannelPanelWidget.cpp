#include "UI/TSVoiceChannelPanelWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
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
	const FLinearColor ChannelIdle(0.10f, 0.10f, 0.12f, 0.90f);
	const FLinearColor ChannelSelected(0.13f, 0.45f, 0.62f, 0.95f);
	const FLinearColor ChannelDisabled(0.08f, 0.08f, 0.09f, 0.55f);
	const FLinearColor PanelBackground(0.02f, 0.03f, 0.03f, 0.78f);
	const FLinearColor LabelColour(0.72f, 0.78f, 0.80f, 1.f);
	const FLinearColor DimColour(0.45f, 0.47f, 0.50f, 1.f);
}

UTSVoiceChannelPanelWidget::UTSVoiceChannelPanelWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// SelfHitTestInvisible, not HitTestInvisible: the panel body must not eat clicks aimed at the
	// world behind it, but its two channel buttons have to stay clickable.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

UTextBlock* UTSVoiceChannelPanelWidget::MakeText(const FString& Text, int32 InFontSize, const FLinearColor& Colour)
{
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Block->SetText(FText::FromString(Text));
	Block->SetColorAndOpacity(FSlateColor(Colour));

	FSlateFontInfo Font = Block->GetFont();
	Font.Size = InFontSize;
	Block->SetFont(Font);
	return Block;
}

UButton* UTSVoiceChannelPanelWidget::MakeChannelButton(const FString& Label)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());

	// Shared with the lobby console for the reason documented there: a focusable Slate button steals
	// keyboard focus when clicked, and under FInputModeGameAndUI that swallows WASD until the player
	// clicks back on the world. A Commander losing their movement keys to a radio button would be a
	// miserable bug to track down.
	UTSRoleDebugRowWidget::MakeButtonNonFocusable(Button);

	UTextBlock* Text = MakeText(Label, FontSize, FLinearColor::White);
	Text->SetJustification(ETextJustify::Center);
	Text->SetMinDesiredWidth(58.f);
	Button->SetContent(Text);

	return Button;
}

TSharedRef<SWidget> UTSVoiceChannelPanelWidget::RebuildWidget()
{
	// Only build the default layout when nothing else has. A Blueprint subclass that authors its own
	// tree keeps it, exactly as UTSCommanderScreenWidget does - the C++ default costs no asset, and
	// hand-authoring later costs no code change.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		RootBorder->SetBrushColor(PanelBackground);
		RootBorder->SetPadding(FMargin(6.f, 4.f));
		WidgetTree->RootWidget = RootBorder;

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		RootBorder->AddChild(Column);

		HeaderText = MakeText(TEXT("VOICE"), FontSize, LabelColour);
		Column->AddChildToVerticalBox(HeaderText);

		// One row per channel: [button][status text][lamps].
		// Out-params are TObjectPtr references, not raw pointer references: the members being filled
		// are TObjectPtr, and a raw UButton*& simply will not bind to one.
		auto BuildRow = [this, Column](TObjectPtr<UButton>& OutButton, TObjectPtr<UTextBlock>& OutLabel,
			TObjectPtr<UTSVoiceIndicatorWidget>& OutIndicator, const FString& ButtonLabel)
		{
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());

			OutButton = MakeChannelButton(ButtonLabel);
			Row->AddChildToHorizontalBox(OutButton);

			OutLabel = MakeText(TEXT(""), FontSize - 1, DimColour);
			// Named BoxSlot, never Slot: UWidget already declares a member called Slot, and shadowing
			// it is a hard error under -WarningsAsErrors (C4458).
			if (UHorizontalBoxSlot* BoxSlot = Cast<UHorizontalBoxSlot>(Row->AddChildToHorizontalBox(OutLabel)))
			{
				BoxSlot->SetPadding(FMargin(6.f, 0.f));
				BoxSlot->SetVerticalAlignment(VAlign_Center);
				BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}

			// The indicator paints and has no desired size of its own, so it MUST be given one here
			// or it collapses to nothing inside the row. See TSVoiceIndicatorWidget.h.
			USizeBox* IndicatorBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			IndicatorBox->SetWidthOverride(IndicatorWidth);
			IndicatorBox->SetHeightOverride(IndicatorHeight);

			OutIndicator = WidgetTree->ConstructWidget<UTSVoiceIndicatorWidget>(UTSVoiceIndicatorWidget::StaticClass());
			IndicatorBox->AddChild(OutIndicator);

			if (UHorizontalBoxSlot* BoxSlot = Cast<UHorizontalBoxSlot>(Row->AddChildToHorizontalBox(IndicatorBox)))
			{
				BoxSlot->SetVerticalAlignment(VAlign_Center);
			}

			if (UVerticalBoxSlot* ColumnSlot = Cast<UVerticalBoxSlot>(Column->AddChildToVerticalBox(Row)))
			{
				ColumnSlot->SetPadding(FMargin(0.f, 2.f));
			}
		};

		BuildRow(CrewButton, CrewLabel, CrewIndicator, TEXT("CREW"));
		BuildRow(HostButton, HostLabel, HostIndicator, TEXT("HOST"));

		FooterText = MakeText(TEXT(""), FontSize - 2, DimColour);
		Column->AddChildToVerticalBox(FooterText);

		if (CrewButton)
		{
			CrewButton->OnClicked.AddUniqueDynamic(this, &UTSVoiceChannelPanelWidget::OnCrewChannelClicked);
		}
		if (HostButton)
		{
			HostButton->OnClicked.AddUniqueDynamic(this, &UTSVoiceChannelPanelWidget::OnHostChannelClicked);
		}
	}

	return Super::RebuildWidget();
}

ATSTankPlayerController* UTSVoiceChannelPanelWidget::GetOwningTankController() const
{
	return Cast<ATSTankPlayerController>(GetOwningPlayer());
}

ATSTankPlayerState* UTSVoiceChannelPanelWidget::GetOwningTankPlayerState() const
{
	const APlayerController* PlayerController = GetOwningPlayer();
	return PlayerController ? Cast<ATSTankPlayerState>(PlayerController->PlayerState) : nullptr;
}

float UTSVoiceChannelPanelWidget::GetPulseSeconds() const
{
	if (const UWorld* World = GetWorld())
	{
		return World->GetRealTimeSeconds();
	}
	return static_cast<float>(FApp::GetCurrentTime());
}

void UTSVoiceChannelPanelWidget::OnCrewChannelClicked()
{
	if (ATSTankPlayerController* PlayerController = GetOwningTankController())
	{
		PlayerController->SetVoiceChannel(ETSVoiceChannel::Crew);
	}
}

void UTSVoiceChannelPanelWidget::OnHostChannelClicked()
{
	if (ATSTankPlayerController* PlayerController = GetOwningTankController())
	{
		PlayerController->SetVoiceChannel(ETSVoiceChannel::Command);
	}
}

void UTSVoiceChannelPanelWidget::StyleChannelButton(UButton* Button, bool bSelected, bool bEnabled) const
{
	if (!Button)
	{
		return;
	}

	const FLinearColor Tint = !bEnabled ? ChannelDisabled : (bSelected ? ChannelSelected : ChannelIdle);

	FButtonStyle Style = Button->GetStyle();
	Style.Normal.TintColor = FSlateColor(Tint);
	Style.Hovered.TintColor = FSlateColor(bEnabled ? Tint * 1.7f : Tint);
	Style.Pressed.TintColor = FSlateColor(ChannelSelected);
	Button->SetStyle(Style);
	Button->SetIsEnabled(bEnabled);
}

void UTSVoiceChannelPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Safe even when the Commander screen is also driving this panel explicitly: RefreshPanel reads
	// state and writes widget properties, and takes its pulse from a clock rather than a delta.
	RefreshPanel();
}

void UTSVoiceChannelPanelWidget::RefreshPanel()
{
	const ATSTankPlayerState* PlayerState = GetOwningTankPlayerState();
	const ATSTankPlayerController* PlayerController = GetOwningTankController();
	const UTSVoiceRouterSubsystem* Router = UTSVoiceRouterSubsystem::Get(this);

	const ETSVoiceChannel Channel = PlayerState ? PlayerState->GetVoiceChannel() : ETSVoiceChannel::None;
	const bool bCanChoose = PlayerController && PlayerController->CanChooseVoiceChannel();
	const bool bTransmitting = PlayerState && PlayerState->IsVoiceTransmitting();

	// The crew lamp is asked of the ROUTER, not of "am I hearing anything": IsCrewNetBusy answers
	// for a Commander who has stepped off the intercom too, which is the whole point of the symbol.
	const bool bCrewBusy = Router && Router->IsCrewNetBusy(PlayerState);
	const bool bHostCalling = Router && Router->IsHostTransmittingTo(PlayerState);

	const float Pulse = GetPulseSeconds();

	if (CrewIndicator)
	{
		CrewIndicator->SetIndicatorState(bTransmitting && Channel == ETSVoiceChannel::Crew, bCrewBusy, Pulse);
	}
	if (HostIndicator)
	{
		HostIndicator->SetIndicatorState(bTransmitting && Channel == ETSVoiceChannel::Command, bHostCalling, Pulse);
	}

	StyleChannelButton(CrewButton, Channel == ETSVoiceChannel::Crew, bCanChoose);
	StyleChannelButton(HostButton, Channel == ETSVoiceChannel::Command, bCanChoose);

	if (CrewLabel)
	{
		CrewLabel->SetText(FText::FromString(bCrewBusy ? TEXT("crew talking") : TEXT("tank intercom")));
		CrewLabel->SetColorAndOpacity(FSlateColor(bCrewBusy ? FLinearColor(0.35f, 0.95f, 0.45f, 1.f) : DimColour));
	}

	if (HostLabel)
	{
		HostLabel->SetText(FText::FromString(bHostCalling ? TEXT("host calling") : TEXT("command net")));
		HostLabel->SetColorAndOpacity(FSlateColor(bHostCalling ? FLinearColor(0.35f, 0.95f, 0.45f, 1.f) : DimColour));
	}

	if (FooterText)
	{
		FString Footer;
		if (!bCanChoose)
		{
			// Only a Commander chooses. Saying so beats two dead buttons and no explanation.
			Footer = TEXT("open mic - this seat has no channel selector");
		}
		else
		{
			Footer = FString::Printf(TEXT("hold %s to talk on %s"),
				PlayerController ? *PlayerController->PushToTalkKeyDisplayName() : TEXT("PTT"),
				*UTSTypeUtils::VoiceChannelToString(Channel));
		}
		FooterText->SetText(FText::FromString(Footer));
	}
}

FString UTSVoiceChannelPanelWidget::BuildPanelDebugString() const
{
	const ATSTankPlayerState* PlayerState = GetOwningTankPlayerState();
	return FString::Printf(TEXT("[VoicePanel] net=%s tx=%d crewRX=%d hostRX=%d canChoose=%d"),
		PlayerState ? *UTSTypeUtils::VoiceChannelToString(PlayerState->GetVoiceChannel()) : TEXT("-"),
		PlayerState && PlayerState->IsVoiceTransmitting() ? 1 : 0,
		CrewIndicator && CrewIndicator->IsReceiving() ? 1 : 0,
		HostIndicator && HostIndicator->IsReceiving() ? 1 : 0,
		GetOwningTankController() && GetOwningTankController()->CanChooseVoiceChannel() ? 1 : 0);
}
