#include "UI/TSDriverPanelWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankControllerBase.h"
#include "UI/TSVisionModeSelectorWidget.h"

namespace
{
	// cm/s -> km/h
	constexpr float CmPerSecondToKmh = 0.036f;

	// Below this the tank reads as stopped, so settling on a slope does not flicker REV.
	constexpr float ReverseThresholdKmh = 0.5f;
}

UTSDriverPanelWidget::UTSDriverPanelWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The panel body lets clicks through; the view-mode buttons inside stay clickable.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

UTextBlock* UTSDriverPanelWidget::MakeText(const FString& Text, int32 InFontSize, const FLinearColor& Colour, FName Name)
{
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Block->SetText(FText::FromString(Text));
	Block->SetColorAndOpacity(FSlateColor(Colour));

	FSlateFontInfo Font = Block->GetFont();
	Font.Size = InFontSize;
	Block->SetFont(Font);
	return Block;
}

TSharedRef<SWidget> UTSDriverPanelWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		const FLinearColor Label(0.72f, 0.78f, 0.80f, 1.f);
		const FLinearColor Value(0.55f, 1.f, 0.60f, 1.f);

		UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Background"));
		Background->SetBrushColor(FLinearColor(0.02f, 0.03f, 0.03f, 0.92f));
		Background->SetPadding(FMargin(12.f));
		WidgetTree->RootWidget = Background;

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
		Background->SetContent(Column);

		Column->AddChildToVerticalBox(MakeText(TEXT("DRIVER"), FontSize - 4, Label, TEXT("HeaderText")));

		UHorizontalBox* SpeedRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SpeedRow"));
		Column->AddChildToVerticalBox(SpeedRow);
		SpeedText = MakeText(TEXT("0"), FontSize * 2, Value, TEXT("SpeedText"));
		SpeedRow->AddChildToHorizontalBox(SpeedText);
		SpeedUnitText = MakeText(TEXT("km/h"), FontSize - 2, Label, TEXT("SpeedUnitText"));
		if (UHorizontalBoxSlot* BoxSlot = SpeedRow->AddChildToHorizontalBox(SpeedUnitText))
		{
			BoxSlot->SetVerticalAlignment(VAlign_Bottom);
			BoxSlot->SetPadding(FMargin(6.f, 0.f, 0.f, 6.f));
		}

		PositionXText = MakeText(TEXT("X  --"), FontSize, Value, TEXT("PositionXText"));
		Column->AddChildToVerticalBox(PositionXText);
		PositionYText = MakeText(TEXT("Y  --"), FontSize, Value, TEXT("PositionYText"));
		Column->AddChildToVerticalBox(PositionYText);

		VisionSelector = WidgetTree->ConstructWidget<UTSVisionModeSelectorWidget>(UTSVisionModeSelectorWidget::StaticClass(), TEXT("VisionSelector"));
		if (UVerticalBoxSlot* SelectorSlot = Column->AddChildToVerticalBox(VisionSelector))
		{
			SelectorSlot->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
		}
		// Labels only matter for the tree the selector builds itself, so set them before it builds.
		VisionSelector->HeaderLabel = NSLOCTEXT("TankSim", "DriverVisionHeader", "VISION");
		VisionSelector->DayLabel = NSLOCTEXT("TankSim", "DriverVisionNormal", "NORMAL");
		VisionSelector->NightVisionLabel = NSLOCTEXT("TankSim", "DriverVisionNight", "NIGHT VISION");
		VisionSelector->HeatmapLabel = NSLOCTEXT("TankSim", "DriverVisionHeat", "HEAT VISION");
		VisionSelector->FontSize = FontSize - 4;
	}

	ConfigureVisionSelector();
	return Super::RebuildWidget();
}

void UTSDriverPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	ConfigureVisionSelector();
	RefreshReadouts();
}

void UTSDriverPanelWidget::ConfigureVisionSelector()
{
	// Outside the build branch so a WBP-authored selector is pointed at the Driver's view too - a
	// selector left on its Commander default would filter the wrong station.
	if (VisionSelector)
	{
		VisionSelector->ViewTarget = ETSVisionViewTarget::Driver;
		VisionSelector->SetTargetTank(Tank.Get());
	}
}

void UTSDriverPanelWidget::ResolveMountingTank()
{
	if (Tank.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ATSTankControllerBase> It(World); It; ++It)
	{
		TArray<UWidgetComponent*> Panels;
		It->GetComponents<UWidgetComponent>(Panels);
		for (const UWidgetComponent* Panel : Panels)
		{
			if (Panel && Panel->GetUserWidgetObject() == this)
			{
				SetTank(*It);
				return;
			}
		}
	}
}

void UTSDriverPanelWidget::SetTank(ATSTankControllerBase* InTank)
{
	Tank = InTank;
	ConfigureVisionSelector();
	RefreshReadouts();
}

ATSTankControllerBase* UTSDriverPanelWidget::GetTank() const
{
	if (Tank.IsValid())
	{
		return Tank.Get();
	}

	const APlayerController* PC = GetOwningPlayer();
	const ATSTankPlayerState* PS = PC ? PC->GetPlayerState<ATSTankPlayerState>() : nullptr;
	return PS ? Cast<ATSTankControllerBase>(PS->GetAssignedTank()) : nullptr;
}

float UTSDriverPanelWidget::GetForwardSpeedKmh() const
{
	const ATSTankControllerBase* T = GetTank();
	if (!T)
	{
		return 0.f;
	}
	return FVector::DotProduct(T->GetVelocity(), T->GetActorForwardVector()) * CmPerSecondToKmh;
}

FVector2D UTSDriverPanelWidget::GetPositionMetres() const
{
	const ATSTankControllerBase* T = GetTank();
	if (!T)
	{
		return FVector2D::ZeroVector;
	}
	const FVector Location = T->GetActorLocation();
	return FVector2D(Location.X, Location.Y) / 100.f;
}

void UTSDriverPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	TimeSinceReadout += InDeltaTime;
	if (TimeSinceReadout >= 1.f / FMath::Max(ReadoutRefreshHz, 1.f))
	{
		TimeSinceReadout = 0.f;
		RefreshReadouts();
	}
}

void UTSDriverPanelWidget::RefreshReadouts()
{
	ResolveMountingTank();

	const bool bHasTank = GetTank() != nullptr;
	const float SpeedKmh = GetForwardSpeedKmh();
	const bool bReversing = SpeedKmh < -ReverseThresholdKmh;

	if (SpeedText)
	{
		SpeedText->SetText(bHasTank
			? FText::AsNumber(FMath::RoundToInt(FMath::Abs(SpeedKmh)))
			: FText::FromString(TEXT("--")));
	}
	if (SpeedUnitText)
	{
		SpeedUnitText->SetText(FText::FromString(bReversing ? TEXT("km/h  REV") : TEXT("km/h")));
	}

	const FVector2D Position = GetPositionMetres();
	auto FormatAxis = [bHasTank](const TCHAR* Axis, double Metres)
	{
		return bHasTank
			? FText::FromString(FString::Printf(TEXT("%s  %.1f m"), Axis, Metres))
			: FText::FromString(FString::Printf(TEXT("%s  --"), Axis));
	};
	if (PositionXText)
	{
		PositionXText->SetText(FormatAxis(TEXT("X"), Position.X));
	}
	if (PositionYText)
	{
		PositionYText->SetText(FormatAxis(TEXT("Y"), Position.Y));
	}
}
