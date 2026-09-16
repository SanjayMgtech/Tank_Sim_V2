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

UTSCommanderScreenWidget::UTSCommanderScreenWidget()
{
	// Class defaults, not asset loads - RULE 2 is about ConstructorHelpers reaching into Content,
	// which none of these do.
	VisionFeedWidgetClass = UTSVisionFeedWidget::StaticClass();
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
}

void UTSCommanderScreenWidget::BuildDefaultLayout()
{
	UHorizontalBox* Root = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CommanderSplit"));
	WidgetTree->RootWidget = Root;

	if (VisionFeedWidgetClass)
	{
		UTSVisionFeedWidget* Feed = WidgetTree->ConstructWidget<UTSVisionFeedWidget>(VisionFeedWidgetClass, TEXT("VisionFeed"));

		// Visible, not SelfHitTestInvisible: the feed is the one panel that accepts a click (to cycle
		// vision mode), and a widget cannot be clicked if it is not hit-testable.
		Feed->SetVisibility(ESlateVisibility::Visible);

		if (UHorizontalBoxSlot* BoxSlot = Root->AddChildToHorizontalBox(Feed))
		{
			FSlateChildSize FeedSize(ESlateSizeRule::Fill);
			FeedSize.Value = VisionFill;
			BoxSlot->SetSize(FeedSize);
			BoxSlot->SetPadding(PanelPadding);
			BoxSlot->SetHorizontalAlignment(HAlign_Fill);
			BoxSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("InstrumentColumn"));
	if (UHorizontalBoxSlot* ColumnSlot = Root->AddChildToHorizontalBox(Column))
	{
		FSlateChildSize ColumnSize(ESlateSizeRule::Fill);
		ColumnSize.Value = InstrumentFill;
		ColumnSlot->SetSize(ColumnSize);
		ColumnSlot->SetHorizontalAlignment(HAlign_Fill);
		ColumnSlot->SetVerticalAlignment(VAlign_Fill);
	}

	if (RadarWidgetClass)
	{
		UTSRadarWidget* Radar = WidgetTree->ConstructWidget<UTSRadarWidget>(RadarWidgetClass, TEXT("Radar"));
		if (UVerticalBoxSlot* BoxSlot = Column->AddChildToVerticalBox(Radar))
		{
			FSlateChildSize RadarSize(ESlateSizeRule::Fill);
			RadarSize.Value = RadarFill;
			BoxSlot->SetSize(RadarSize);
			BoxSlot->SetPadding(PanelPadding);
			BoxSlot->SetHorizontalAlignment(HAlign_Fill);
			BoxSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}

	if (AttitudeWidgetClass)
	{
		UTSTankAttitudeWidget* Attitude = WidgetTree->ConstructWidget<UTSTankAttitudeWidget>(AttitudeWidgetClass, TEXT("Attitude"));
		if (UVerticalBoxSlot* BoxSlot = Column->AddChildToVerticalBox(Attitude))
		{
			FSlateChildSize AttitudeSize(ESlateSizeRule::Fill);
			AttitudeSize.Value = AttitudeFill;
			BoxSlot->SetSize(AttitudeSize);
			BoxSlot->SetPadding(PanelPadding);
			BoxSlot->SetHorizontalAlignment(HAlign_Fill);
			BoxSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}

	if (bShowVoicePanel && VoicePanelWidgetClass)
	{
		UTSVoiceChannelPanelWidget* VoicePanel =
			WidgetTree->ConstructWidget<UTSVoiceChannelPanelWidget>(VoicePanelWidgetClass, TEXT("VoicePanel"));

		if (UVerticalBoxSlot* BoxSlot = Column->AddChildToVerticalBox(VoicePanel))
		{
			// Auto, not Fill: two rows of buttons have a natural height, and a fill weight would
			// stretch them over the instruments they sit beneath.
			BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			BoxSlot->SetPadding(PanelPadding);
			BoxSlot->SetHorizontalAlignment(HAlign_Fill);
			BoxSlot->SetVerticalAlignment(VAlign_Bottom);
		}
	}
}

void UTSCommanderScreenWidget::BindPanelsFromWidgetTree()
{
	RadarWidget = nullptr;
	AttitudeWidget = nullptr;
	VisionFeedWidget = nullptr;
	VoicePanelWidget = nullptr;

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
	});
}
