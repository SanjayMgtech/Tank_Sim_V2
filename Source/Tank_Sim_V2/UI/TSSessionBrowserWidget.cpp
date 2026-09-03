#include "UI/TSSessionBrowserWidget.h"

#include "Core/TSGameInstance.h"
#include "Engine/Engine.h"

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
		Sessions->FindSessions(bIsLAN);
	}
}

void UTSSessionBrowserWidget::JoinSession(int32 SessionIndex)
{
	OnJoin.Broadcast(SessionIndex);
	if (UTSSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->JoinSession(SessionIndex);
	}
}

void UTSSessionBrowserWidget::HandleCreateSessionComplete(bool bWasSuccessful)
{
	OnCreateSessionFinished(bWasSuccessful);
}

void UTSSessionBrowserWidget::HandleFindSessionsComplete(bool bWasSuccessful, const TArray<FTSSessionSearchResult>& Results)
{
	OnSessionListUpdated(bWasSuccessful ? Results : TArray<FTSSessionSearchResult>());
}

void UTSSessionBrowserWidget::HandleJoinSessionComplete(bool bWasSuccessful)
{
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
