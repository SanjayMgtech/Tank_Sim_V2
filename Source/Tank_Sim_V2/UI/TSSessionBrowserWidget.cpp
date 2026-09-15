#include "UI/TSSessionBrowserWidget.h"

#include "Core/TSGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

void UTSSessionBrowserWidget::ShowLoadingText(const FString& Message)
{
	if (!LoadingOverlay.IsValid())
	{
		UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
		if (!Viewport)
		{
			return;
		}

		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 24);
		LoadingOverlay = SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Bottom)
			.Padding(FMargin(0.f, 0.f, 0.f, 48.f))
			.Visibility(EVisibility::HitTestInvisible)
			[
				SAssignNew(LoadingTextBlock, STextBlock)
				.Font(Font)
				.ColorAndOpacity(FLinearColor::White)
				.ShadowOffset(FVector2D(1.f, 1.f))
				.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f))
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
