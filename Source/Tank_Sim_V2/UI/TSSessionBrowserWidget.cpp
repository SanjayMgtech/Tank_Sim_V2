#include "UI/TSSessionBrowserWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/TSGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "Tank_Sim_V2.h"
#include "UI/TSRoleDebugRowWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FTSMapOption MakeMapOption(const TCHAR* DisplayName, const TCHAR* MapName)
	{
		// Soft paths only - nothing is loaded here (RULE 2). The thumbnails load when the picker is
		// built, on the menu.
		FTSMapOption Option;
		Option.DisplayName = FText::FromString(DisplayName);
		Option.Map = TSoftObjectPtr<UWorld>(FSoftObjectPath(FString::Printf(TEXT("/Game/TankSimulation/Maps/%s.%s"), MapName, MapName)));
		Option.Thumbnail = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(FString::Printf(
			TEXT("/Game/TankSimulation/UI/MapThumbnails/T_MapThumb_%s.T_MapThumb_%s"), MapName, MapName)));
		return Option;
	}
}

void UTSMapTileClickProxy::HandleClicked()
{
	if (UTSSessionBrowserWidget* Browser = Owner.Get())
	{
		Browser->SelectMap(MapIndex);
	}
}

UTSSessionBrowserWidget::UTSSessionBrowserWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AvailableMaps.Add(MakeMapOption(TEXT("War Zone"), TEXT("WarZone")));
	AvailableMaps.Add(MakeMapOption(TEXT("Tundra"), TEXT("Tundra")));
	AvailableMaps.Add(MakeMapOption(TEXT("Desert"), TEXT("Desert")));
}

void UTSSessionBrowserWidget::BuildMapPicker()
{
	if (!bBuildMapPicker || AvailableMaps.Num() == 0 || !WidgetTree || MapTileFrames.Num() > 0)
	{
		return;
	}

	// The lobby console's look (its style defaults), so the picker matches WBP_SessionBrowser's panel.
	const FTSLobbyConsoleStyle Style;

	UVerticalBox* PickerRoot = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MapPicker"));

	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MapPicker_Title"));
	Title->SetText(FText::FromString(TEXT("SELECT MAP")));
	Title->SetColorAndOpacity(FSlateColor(Style.AccentColor));
	TSLobbyConsoleUI::SetFont(Title, Style.FontSize - 1, true, 150);
	if (UVerticalBoxSlot* TitleSlot = PickerRoot->AddChildToVerticalBox(Title))
	{
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}

	UHorizontalBox* Tiles = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("MapPicker_Tiles"));
	PickerRoot->AddChildToVerticalBox(Tiles);

	for (int32 Index = 0; Index < AvailableMaps.Num(); ++Index)
	{
		const FTSMapOption& Option = AvailableMaps[Index];

		UButton* TileButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		TileButton->SetBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.f));

		// The frame is what changes colour on selection: a white rounded brush, tinted by BrushColor.
		UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Frame->SetBrush(TSLobbyConsoleUI::MakeRoundedBrush(FLinearColor::White, Style.CornerRadius));
		Frame->SetPadding(FMargin(4.f));
		Frame->SetBrushColor(UnselectedTileColor);
		TileButton->AddChild(Frame);

		UVerticalBox* TileContent = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Frame->SetContent(TileContent);

		USizeBox* ThumbBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		ThumbBox->SetWidthOverride(MapThumbnailSize.X);
		ThumbBox->SetHeightOverride(MapThumbnailSize.Y);
		TileContent->AddChildToVerticalBox(ThumbBox);

		UImage* Thumb = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		if (!Option.Thumbnail.IsNull())
		{
			Thumb->SetBrushFromSoftTexture(Option.Thumbnail, false);
		}
		else
		{
			Thumb->SetColorAndOpacity(FLinearColor(0.2f, 0.2f, 0.2f, 1.f));
		}
		ThumbBox->AddChild(Thumb);

		UTextBlock* MapLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		MapLabel->SetText(Option.DisplayName.IsEmpty()
			? FText::FromString(FPackageName::GetShortName(Option.GetMapPath()))
			: Option.DisplayName);
		MapLabel->SetColorAndOpacity(FSlateColor(Style.TextColor));
		TSLobbyConsoleUI::SetFont(MapLabel, Style.FontSize, true);
		MapLabel->SetJustification(ETextJustify::Center);
		if (UVerticalBoxSlot* LabelSlot = TileContent->AddChildToVerticalBox(MapLabel))
		{
			LabelSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
			LabelSlot->SetHorizontalAlignment(HAlign_Center);
		}

		UTSMapTileClickProxy* Proxy = NewObject<UTSMapTileClickProxy>(this);
		Proxy->MapIndex = Index;
		Proxy->Owner = this;
		TileButton->OnClicked.AddDynamic(Proxy, &UTSMapTileClickProxy::HandleClicked);

		if (UHorizontalBoxSlot* TileSlot = Tiles->AddChildToHorizontalBox(TileButton))
		{
			TileSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		}
		MapTileFrames.Add(Frame);
		MapTileProxies.Add(Proxy);
	}

	SelectedMapLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MapPicker_Selected"));
	SelectedMapLabel->SetColorAndOpacity(FSlateColor(Style.MutedTextColor));
	TSLobbyConsoleUI::SetFont(SelectedMapLabel, Style.FontSize - 1, false);
	if (UVerticalBoxSlot* SelectedSlot = PickerRoot->AddChildToVerticalBox(SelectedMapLabel))
	{
		SelectedSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 8.f));
	}

	// Where it goes: a designer-provided MapPickerContainer, else directly above Create Session, else the
	// top of the root panel.
	bool bPlaced = false;
	if (UPanelWidget* Container = Cast<UPanelWidget>(GetWidgetFromName(TEXT("MapPickerContainer"))))
	{
		Container->AddChild(PickerRoot);
		bPlaced = true;
	}
	else if (UWidget* CreateButton = GetWidgetFromName(TEXT("Btn_CreateSession")))
	{
		if (UPanelWidget* ParentPanel = CreateButton->GetParent())
		{
			ParentPanel->InsertChildAt(ParentPanel->GetChildIndex(CreateButton), PickerRoot);
			bPlaced = true;
		}
	}
	if (!bPlaced)
	{
		if (UPanelWidget* RootPanel = Cast<UPanelWidget>(GetRootWidget()))
		{
			RootPanel->InsertChildAt(0, PickerRoot);
			bPlaced = true;
		}
	}
	if (!bPlaced)
	{
		UE_LOG(LogTankSim, Warning, TEXT("UTSSessionBrowserWidget: nowhere to put the map picker - add a panel named MapPickerContainer to %s."),
			*GetClass()->GetName());
	}

	// Start on the map HostMapPath already names (a WBP may have set it), else the first one.
	const int32 Initial = AvailableMaps.IndexOfByPredicate([this](const FTSMapOption& Option)
	{
		return Option.GetMapPath().Equals(HostMapPath, ESearchCase::IgnoreCase);
	});
	SelectMap(Initial != INDEX_NONE ? Initial : 0);
}

void UTSSessionBrowserWidget::SelectMap(int32 MapIndex)
{
	if (!AvailableMaps.IsValidIndex(MapIndex))
	{
		return;
	}

	SelectedMapIndex = MapIndex;
	const FTSMapOption& Option = AvailableMaps[MapIndex];
	HostMapPath = Option.GetMapPath();
	RefreshMapTiles();

	UE_LOG(LogTankSim, Log, TEXT("[Session] Host map selected: %s (%s)"), *Option.DisplayName.ToString(), *HostMapPath);
	OnSelectedMapChanged(MapIndex, Option);
}

void UTSSessionBrowserWidget::RefreshMapTiles()
{
	for (int32 Index = 0; Index < MapTileFrames.Num(); ++Index)
	{
		if (UBorder* Frame = MapTileFrames[Index])
		{
			Frame->SetBrushColor(Index == SelectedMapIndex ? SelectedTileColor : UnselectedTileColor);
		}
	}

	if (SelectedMapLabel && AvailableMaps.IsValidIndex(SelectedMapIndex))
	{
		SelectedMapLabel->SetText(FText::Format(FText::FromString(TEXT("Map: {0}")), AvailableMaps[SelectedMapIndex].DisplayName));
	}
}

void UTSSessionBrowserWidget::ShowLoadingText(const FString& Message)
{
	if (!LoadingOverlay.IsValid())
	{
		UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
		if (!Viewport)
		{
			return;
		}

		// A lobby-console status badge: rounded panel, amber accent bar, letter-spaced bold text. SBorder
		// keeps a raw pointer to its brush, so the brushes are statics rather than locals.
		const FTSLobbyConsoleStyle Style;
		static const FSlateBrush PanelBrush = TSLobbyConsoleUI::MakeRoundedBrush(Style.PanelColor, Style.CornerRadius + 4.f);
		static const FSlateBrush AccentBrush = TSLobbyConsoleUI::MakeRoundedBrush(Style.AccentColor, 2.f);

		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", Style.FontSize + 2);
		Font.LetterSpacing = 80;

		LoadingOverlay = SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Bottom)
			.Padding(FMargin(0.f, 0.f, 0.f, 48.f))
			.Visibility(EVisibility::HitTestInvisible)
			[
				SNew(SBorder)
				.BorderImage(&PanelBrush)
				.Padding(FMargin(16.f, 10.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, 10.f, 0.f))
					[
						SNew(SBox)
						.WidthOverride(4.f)
						.HeightOverride(Style.FontSize + 8.f)
						[
							SNew(SBorder).BorderImage(&AccentBrush)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SAssignNew(LoadingTextBlock, STextBlock)
						.Font(Font)
						.ColorAndOpacity(Style.TextColor)
					]
				]
			];

		// High Z-order so it sits above the session browser itself.
		Viewport->AddViewportWidgetContent(LoadingOverlay.ToSharedRef(), 1000);
	}

	LoadingTextBlock->SetText(FText::FromString(Message));
}

void UTSSessionBrowserWidget::HideLoadingText()
{
	if (LoadingOverlay.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
		{
			Viewport->RemoveViewportWidgetContent(LoadingOverlay.ToSharedRef());
		}
	}
	LoadingOverlay.Reset();
	LoadingTextBlock.Reset();
}

UTSSessionSubsystem* UTSSessionBrowserWidget::GetSessionSubsystem() const
{
	if (UTSGameInstance* TSGI = Cast<UTSGameInstance>(GetGameInstance()))
	{
		return TSGI->GetSessionSubsystem();
	}
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UTSSessionSubsystem>() : nullptr;
}

void UTSSessionBrowserWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UTSSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->OnCreateSessionComplete.AddDynamic(this, &UTSSessionBrowserWidget::HandleCreateSessionComplete);
		Sessions->OnFindSessionsComplete.AddDynamic(this, &UTSSessionBrowserWidget::HandleFindSessionsComplete);
		Sessions->OnJoinSessionComplete.AddDynamic(this, &UTSSessionBrowserWidget::HandleJoinSessionComplete);
		Sessions->OnDestroySessionComplete.AddDynamic(this, &UTSSessionBrowserWidget::HandleDestroySessionComplete);
	}

	OnJoin.AddDynamic(this, &UTSSessionBrowserWidget::HandleJoinClicked);

	BuildMapPicker();
}

void UTSSessionBrowserWidget::NativeDestruct()
{
	HideLoadingText();
	OnJoin.RemoveDynamic(this, &UTSSessionBrowserWidget::HandleJoinClicked);

	if (UTSSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->OnCreateSessionComplete.RemoveDynamic(this, &UTSSessionBrowserWidget::HandleCreateSessionComplete);
		Sessions->OnFindSessionsComplete.RemoveDynamic(this, &UTSSessionBrowserWidget::HandleFindSessionsComplete);
		Sessions->OnJoinSessionComplete.RemoveDynamic(this, &UTSSessionBrowserWidget::HandleJoinSessionComplete);
		Sessions->OnDestroySessionComplete.RemoveDynamic(this, &UTSSessionBrowserWidget::HandleDestroySessionComplete);
	}

	Super::NativeDestruct();
}

void UTSSessionBrowserWidget::CreateSession(int32 MaxPlayers, bool bIsLAN)
{
	OnCreateSession.Broadcast();
	if (UTSSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->CreateSession(MaxPlayers, bIsLAN, false, HostMapPath);
	}
}

void UTSSessionBrowserWidget::RefreshSessions(bool bIsLAN)
{
	OnRefresh.Broadcast();
	if (UTSSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		// Shown before the call: every failure path in FindSessions broadcasts completion synchronously,
		// which hides it again straight away.
		ShowLoadingText(TEXT("Searching for sessions..."));
		Sessions->FindSessions(bIsLAN);
	}
}

void UTSSessionBrowserWidget::JoinSession(int32 SessionIndex)
{
	OnJoin.Broadcast(SessionIndex);
	if (UTSSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		ShowLoadingText(TEXT("Joining session..."));
		Sessions->JoinSession(SessionIndex);
	}
}

void UTSSessionBrowserWidget::HandleCreateSessionComplete(bool bWasSuccessful)
{
	OnCreateSessionFinished(bWasSuccessful);
}

void UTSSessionBrowserWidget::HandleFindSessionsComplete(bool bWasSuccessful, const TArray<FTSSessionSearchResult>& Results)
{
	HideLoadingText();
	OnSessionListUpdated(bWasSuccessful ? Results : TArray<FTSSessionSearchResult>());
}

void UTSSessionBrowserWidget::HandleJoinSessionComplete(bool bWasSuccessful)
{
	// On success the client is now traveling, so keep "Joining session..." up until the map change
	// tears this widget down (NativeDestruct hides it). On failure there is nothing left to wait for.
	if (!bWasSuccessful)
	{
		HideLoadingText();
	}
	OnJoinSessionFinished(bWasSuccessful);
}

void UTSSessionBrowserWidget::HandleDestroySessionComplete(bool bWasSuccessful)
{
	OnDestroySessionFinished(bWasSuccessful);
}

void UTSSessionBrowserWidget::HandleJoinClicked(int32 SessionIndex)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.0f, FColor::Cyan,
			FString::Printf(TEXT("[Session] Join clicked for result index %d"), SessionIndex));
	}
}
