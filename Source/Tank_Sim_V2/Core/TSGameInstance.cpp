#include "Core/TSGameInstance.h"

#include "Networking/TSSessionSubsystem.h"

UTSSessionSubsystem* UTSGameInstance::GetSessionSubsystem() const
{
	return GetSubsystem<UTSSessionSubsystem>();
}

void UTSGameInstance::CreateSession(int32 MaxPlayers, bool bIsLAN, bool bIsPresence, FString MapPath)
{
	if (UTSSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->CreateSession(MaxPlayers, bIsLAN, bIsPresence, MapPath);
	}
}

void UTSGameInstance::FindSessions(bool bIsLAN, bool bIsPresence)
{
	if (UTSSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->FindSessions(bIsLAN, bIsPresence);
	}
}

void UTSGameInstance::JoinSession(int32 SearchResultIndex)
{
	if (UTSSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->JoinSession(SearchResultIndex);
	}
}

void UTSGameInstance::DestroySession()
{
	if (UTSSessionSubsystem* Sessions = GetSessionSubsystem())
	{
		Sessions->DestroySession();
	}
}
