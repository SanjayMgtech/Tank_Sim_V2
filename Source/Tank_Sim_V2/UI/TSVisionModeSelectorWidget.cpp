#include "UI/TSVisionModeSelectorWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankControllerBase.h"
#include "UI/TSRoleDebugRowWidget.h"

UTSVisionModeSelectorWidget::UTSVisionModeSelectorWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The panel body lets clicks through; the buttons inside stay clickable.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

UTextBlock* UTSVisionModeSelectorWidget::MakeText(const FString& Text, int32 InFontSize, const FLinearColor& Colour)
{
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Block->SetText(FText::FromString(Text));
	Block->SetColorAndOpacity(FSlateColor(Colour));

	FSlateFontInfo Font = Block->GetFont();
	Font.Size = InFontSize;
	Block->SetFont(Font);
	return Block;
}

UButton* UTSVisionModeSelectorWidget::MakeOptionButton(const FString& Label)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());

	// A focusable button steals keyboard focus on click and swallows gameplay keys afterwards.
	UTSRoleDebugRowWidget::MakeButtonNonFocusable(Button);

	UTextBlock* Text = MakeText(Label, FontSize, FLinearColor::White);
	Text->SetJustification(ETextJustify::Center);
	Button->SetContent(Text);
	return Button;
}

TSharedRef<SWidget> UTSVisionModeSelectorWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
		WidgetTree->RootWidget = Column;

		HeaderText = MakeText(HeaderLabel.ToString(), FontSize - 2, FLinearColor(0.72f, 0.78f, 0.80f, 1.f));
		Column->AddChildToVerticalBox(HeaderText);

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Options"));
		if (UVerticalBoxSlot* RowSlot = Column->AddChildToVerticalBox(Row))
		{
			RowSlot->SetPadding(FMargin(0.f, 2.f));
		}

		auto AddOption = [this, Row](TObjectPtr<UButton>& OutButton, const FString& Label)
		{
			OutButton = MakeOptionButton(Label);
			if (UHorizontalBoxSlot* BoxSlot = Row->AddChildToHorizontalBox(OutButton))
			{
				BoxSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				BoxSlot->SetPadding(FMargin(2.f, 0.f));
			}
		};

		AddOption(DayButton, DayLabel.ToString());
		AddOption(NightVisionButton, NightVisionLabel.ToString());
		AddOption(HeatmapButton, HeatmapLabel.ToString());
	}

	// Outside the build branch so a WBP-authored tree (bound by name) gets the handlers too.
	if (DayButton)
	{
		DayButton->OnClicked.AddUniqueDynamic(this, &UTSVisionModeSelectorWidget::OnDayClicked);
	}
	if (NightVisionButton)
	{
		NightVisionButton->OnClicked.AddUniqueDynamic(this, &UTSVisionModeSelectorWidget::OnNightVisionClicked);
	}
	if (HeatmapButton)
	{
		HeatmapButton->OnClicked.AddUniqueDynamic(this, &UTSVisionModeSelectorWidget::OnHeatmapClicked);
	}

	return Super::RebuildWidget();
}

void UTSVisionModeSelectorWidget::SetTargetTank(ATSTankControllerBase* InTank)
{
	TargetTank = InTank;
}

ATSTankControllerBase* UTSVisionModeSelectorWidget::GetLocalTank() const
{
	if (TargetTank.IsValid())
	{
		return TargetTank.Get();
	}

	const APlayerController* PC = GetOwningPlayer();
	const ATSTankPlayerState* PS = PC ? PC->GetPlayerState<ATSTankPlayerState>() : nullptr;
	return PS ? Cast<ATSTankControllerBase>(PS->GetAssignedTank()) : nullptr;
}

void UTSVisionModeSelectorWidget::SelectVisionMode(ETSVisionMode NewMode)
{
	if (ATSTankControllerBase* Tank = GetLocalTank())
	{
		if (ViewTarget == ETSVisionViewTarget::Driver)
		{
			Tank->SetDriverViewVisionMode(NewMode);
		}
		else
		{
			Tank->SetCommanderViewVisionMode(NewMode);
		}
	}
	RefreshHighlight();
}

ETSVisionMode UTSVisionModeSelectorWidget::GetSelectedVisionMode() const
{
	const ATSTankControllerBase* Tank = GetLocalTank();
	if (!Tank)
	{
		return ETSVisionMode::Normal;
	}
	return ViewTarget == ETSVisionViewTarget::Driver ? Tank->GetDriverViewVisionMode() : Tank->GetCommanderViewVisionMode();
}

void UTSVisionModeSelectorWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshHighlight();
}

void UTSVisionModeSelectorWidget::RefreshHighlight()
{
	const ETSVisionMode Current = GetSelectedVisionMode();
	auto Paint = [this](UButton* Button, bool bSelected)
	{
		if (Button)
		{
			Button->SetBackgroundColor(bSelected ? SelectedColour : IdleColour);
		}
	};

	Paint(DayButton, Current == ETSVisionMode::Normal);
	Paint(NightVisionButton, Current == ETSVisionMode::NightVision);
	Paint(HeatmapButton, Current == ETSVisionMode::Thermal);
}

void UTSVisionModeSelectorWidget::OnDayClicked()
{
	SelectVisionMode(ETSVisionMode::Normal);
}

void UTSVisionModeSelectorWidget::OnNightVisionClicked()
{
	SelectVisionMode(ETSVisionMode::NightVision);
}

void UTSVisionModeSelectorWidget::OnHeatmapClicked()
{
	SelectVisionMode(ETSVisionMode::Thermal);
}
