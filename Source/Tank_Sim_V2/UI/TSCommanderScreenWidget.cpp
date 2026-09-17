#include "UI/TSCommanderScreenWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Tank_Sim_V2.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "UI/TSRadarWidget.h"
#include "UI/TSTankAttitudeWidget.h"
#include "UI/TSVisionFeedWidget.h"
#include "UI/TSVoiceChannelPanelWidget.h"
#include "UI/TSVisionModeSelectorWidget.h"
#include "UI/TSRoleDebugRowWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Player/TSTankPlayerController.h"

UTSCommanderScreenWidget::UTSCommanderScreenWidget()
{
	// Class defaults, not asset loads - RULE 2 is about ConstructorHelpers reaching into Content,
	// which none of these do.
	VisionFeedWidgetClass = nullptr;
	VisionModeSelectorWidgetClass = UTSVisionModeSelectorWidget::StaticClass();
	RadarWidgetClass = UTSRadarWidget::StaticClass();
	AttitudeWidgetClass = UTSTankAttitudeWidget::StaticClass();
	VoicePanelWidgetClass = UTSVoiceChannelPanelWidget::StaticClass();
}

TSharedRef<SWidget> UTSCommanderScreenWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}

	BindPanelsFromWidgetTree();

	if (SwitchStationButton)
	{
		SwitchStationButton->OnClicked.AddUniqueDynamic(this, &UTSCommanderScreenWidget::OnSwitchStationClicked);
	}

	return Super::RebuildWidget();
}

void UTSCommanderScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(LogTankSim, Log, TEXT("[CmdScreen] NativeConstruct - radar=%s attitude=%s vision=%s voice=%s"),
		RadarWidget ? TEXT("yes") : TEXT("NO"),
		AttitudeWidget ? TEXT("yes") : TEXT("NO"),
		VisionFeedWidget ? TEXT("yes") : TEXT("NO"),
		VoicePanelWidget ? TEXT("yes") : TEXT("NO"));
}

int32 UTSCommanderScreenWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!bLoggedFirstPaint)
	{
		bLoggedFirstPaint = true;
		UE_LOG(LogTankSim, Log, TEXT("[CmdScreen] first NativePaint - size %s"),
			*AllottedGeometry.GetLocalSize().ToString());
	}

	return Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

void UTSCommanderScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bLoggedFirstTick)
	{
		bLoggedFirstTick = true;
		UE_LOG(LogTankSim, Log, TEXT("[CmdScreen] first NativeTick - size %s"), *MyGeometry.GetLocalSize().ToString());
	}

	++TickCount;

	// Order matters only in that the feed is refreshed last: it reads the render target the tank's
	// capture wrote, and nothing here changes that. The other two are independent.
	if (RadarWidget)
	{
		RadarWidget->RefreshInstrument(InDeltaTime);
	}
	if (AttitudeWidget)
	{
		AttitudeWidget->RefreshInstrument(InDeltaTime);
	}
	if (VisionFeedWidget)
	{
		VisionFeedWidget->RefreshInstrument(InDeltaTime);
	}

	// Driven from here like the other three. The panel also ticks itself when used standalone, and
	// that double call is deliberately harmless - RefreshPanel is idempotent and takes its pulse
	// from a clock rather than from an accumulated delta.
	if (VoicePanelWidget)
	{
		VoicePanelWidget->RefreshPanel();
	}

	RefreshSwitchStationLabel();
}

void UTSCommanderScreenWidget::OnSwitchStationClicked()
{
	if (ATSTankPlayerController* PC = Cast<ATSTankPlayerController>(GetOwningPlayer()))
	{
		PC->ToggleCommanderStation();
	}
}

void UTSCommanderScreenWidget::RefreshSwitchStationLabel()
{
	if (!SwitchStationLabel)
	{
		return;
	}

	const ATSTankPlayerController* PC = Cast<ATSTankPlayerController>(GetOwningPlayer());
	const bool bAtScreen = PC && PC->GetCommanderStation() == ETSCommanderStation::Screen;
	SwitchStationLabel->SetText(FText::FromString(bAtScreen ? TEXT("TO SCOPE  [C]") : TEXT("TO SCREEN  [C]")));
}

void UTSCommanderScreenWidget::BuildDefaultLayout()
{
	// [ optional feed ] [ RADAR / view modes ] [ ATTITUDE / radio / seat switch (bottom right) ]
	UHorizontalBox* Root = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CommanderSplit"));
	WidgetTree->RootWidget = Root;

	auto AddColumn = [this, Root](const TCHAR* Name, float Fill) -> UVerticalBox*
	{
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), Name);
		if (UHorizontalBoxSlot* ColumnSlot = Root->AddChildToHorizontalBox(Column))
		{
			FSlateChildSize Size(ESlateSizeRule::Fill);
			Size.Value = Fill;
			ColumnSlot->SetSize(Size);
			ColumnSlot->SetHorizontalAlignment(HAlign_Fill);
			ColumnSlot->SetVerticalAlignment(VAlign_Fill);
		}
		return Column;
	};

	auto AddToColumn = [this](UVerticalBox* Column, UWidget* Widget, float Fill, EVerticalAlignment VAlign)
	{
		if (UVerticalBoxSlot* BoxSlot = Column->AddChildToVerticalBox(Widget))
		{
			// Fill <= 0 means auto-size: a strip of buttons has a natural height and must not stretch.
			FSlateChildSize Size(Fill > 0.f ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic);
			Size.Value = Fill > 0.f ? Fill : 1.f;
			BoxSlot->SetSize(Size);
			BoxSlot->SetPadding(PanelPadding);
			BoxSlot->SetHorizontalAlignment(HAlign_Fill);
			BoxSlot->SetVerticalAlignment(VAlign);
		}
	};

	if (VisionFeedWidgetClass)
	{
		UVerticalBox* FeedColumn = AddColumn(TEXT("FeedColumn"), VisionFill);
		UTSVisionFeedWidget* Feed = WidgetTree->ConstructWidget<UTSVisionFeedWidget>(VisionFeedWidgetClass, TEXT("VisionFeed"));
		Feed->SetVisibility(ESlateVisibility::Visible);
		AddToColumn(FeedColumn, Feed, 1.f, VAlign_Fill);
	}

	UVerticalBox* LeftColumn = AddColumn(TEXT("LeftColumn"), InstrumentFill);
	if (RadarWidgetClass)
	{
		AddToColumn(LeftColumn, WidgetTree->ConstructWidget<UTSRadarWidget>(RadarWidgetClass, TEXT("Radar")), RadarFill, VAlign_Fill);
	}
	if (VisionModeSelectorWidgetClass)
	{
		AddToColumn(LeftColumn,
			WidgetTree->ConstructWidget<UTSVisionModeSelectorWidget>(VisionModeSelectorWidgetClass, TEXT("VisionModes")), 0.f, VAlign_Bottom);
	}

	UVerticalBox* RightColumn = AddColumn(TEXT("RightColumn"), InstrumentFill);
	if (AttitudeWidgetClass)
	{
		AddToColumn(RightColumn, WidgetTree->ConstructWidget<UTSTankAttitudeWidget>(AttitudeWidgetClass, TEXT("Attitude")), AttitudeFill, VAlign_Fill);
	}
	if (bShowVoicePanel && VoicePanelWidgetClass)
	{
		AddToColumn(RightColumn,
			WidgetTree->ConstructWidget<UTSVoiceChannelPanelWidget>(VoicePanelWidgetClass, TEXT("VoicePanel")), 0.f, VAlign_Bottom);
	}

	SwitchStationButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("SwitchStationButton"));
	UTSRoleDebugRowWidget::MakeButtonNonFocusable(SwitchStationButton);
	SwitchStationButton->SetBackgroundColor(FLinearColor(0.13f, 0.45f, 0.62f, 0.95f));

	SwitchStationLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SwitchStationLabel"));
	SwitchStationLabel->SetText(FText::FromString(TEXT("SWITCH SEAT  [C]")));
	SwitchStationLabel->SetJustification(ETextJustify::Center);
	SwitchStationButton->SetContent(SwitchStationLabel);

	if (UVerticalBoxSlot* ButtonSlot = RightColumn->AddChildToVerticalBox(SwitchStationButton))
	{
		ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		ButtonSlot->SetPadding(PanelPadding);
		ButtonSlot->SetHorizontalAlignment(HAlign_Right);
		ButtonSlot->SetVerticalAlignment(VAlign_Bottom);
	}
}

void UTSCommanderScreenWidget::BindPanelsFromWidgetTree()
{
	RadarWidget = nullptr;
	AttitudeWidget = nullptr;
	VisionFeedWidget = nullptr;
	VoicePanelWidget = nullptr;
	VisionModeSelector = nullptr;

	if (!WidgetTree)
	{
		return;
	}

	// By class, not by name: a hand-authored WBP is free to call its panels whatever it likes.
	WidgetTree->ForEachWidget([this](UWidget* Widget)
	{
		if (!RadarWidget)
		{
			RadarWidget = Cast<UTSRadarWidget>(Widget);
		}
		if (!AttitudeWidget)
		{
			AttitudeWidget = Cast<UTSTankAttitudeWidget>(Widget);
		}
		if (!VisionFeedWidget)
		{
			VisionFeedWidget = Cast<UTSVisionFeedWidget>(Widget);
		}
		if (!VoicePanelWidget)
		{
			VoicePanelWidget = Cast<UTSVoiceChannelPanelWidget>(Widget);
		}
		if (!VisionModeSelector)
		{
			VisionModeSelector = Cast<UTSVisionModeSelectorWidget>(Widget);
		}
	});
}
