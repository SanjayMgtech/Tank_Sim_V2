#include "UI/TSCommanderScreenWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "UI/TSRadarWidget.h"
#include "UI/TSTankAttitudeWidget.h"
#include "UI/TSVisionFeedWidget.h"

UTSCommanderScreenWidget::UTSCommanderScreenWidget()
{
	// Class defaults, not asset loads - RULE 2 is about ConstructorHelpers reaching into Content,
	// which none of these do.
	VisionFeedWidgetClass = UTSVisionFeedWidget::StaticClass();
	RadarWidgetClass = UTSRadarWidget::StaticClass();
	AttitudeWidgetClass = UTSTankAttitudeWidget::StaticClass();
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

void UTSCommanderScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

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
}

void UTSCommanderScreenWidget::BindPanelsFromWidgetTree()
{
	RadarWidget = nullptr;
	AttitudeWidget = nullptr;
	VisionFeedWidget = nullptr;

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
	});
}
