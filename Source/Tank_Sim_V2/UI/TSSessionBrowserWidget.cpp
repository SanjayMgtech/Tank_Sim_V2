#include "UI/TSSessionBrowserWidget.h"

UTSSessionSubsystem* UTSSessionBrowserWidget::GetSessionSubsystem() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UTSSessionSubsystem>() : nullptr;
}

void UTSSessionBrowserWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UTSSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->OnFindSessionsComplete.AddDynamic(this, &UTSSessionBrowserWidget::HandleFindSessionsComplete);
	}
}

void UTSSessionBrowserWidget::NativeDestruct()
{
	if (UTSSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->OnFindSessionsComplete.RemoveDynamic(this, &UTSSessionBrowserWidget::HandleFindSessionsComplete);
	}

	Super::NativeDestruct();
}

void UTSSessionBrowserWidget::CreateSession(int32 MaxPlayers, bool bIsLAN)
{
	OnCreateSession.Broadcast();
	if (UTSSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->CreateSession(MaxPlayers, bIsLAN);
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

void UTSSessionBrowserWidget::HandleFindSessionsComplete(bool bWasSuccessful, const TArray<FTSSessionSearchResult>& Results)
{
	OnSessionListUpdated(bWasSuccessful ? Results : TArray<FTSSessionSearchResult>());
}
