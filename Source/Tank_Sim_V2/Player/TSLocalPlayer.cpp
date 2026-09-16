#include "Player/TSLocalPlayer.h"

FString UTSLocalPlayer::GetNickname() const
{
	return PlayerDisplayName.IsEmpty() ? Super::GetNickname() : PlayerDisplayName;
}

void UTSLocalPlayer::SetPlayerDisplayName(const FString& NewName)
{
	PlayerDisplayName = SanitizePlayerName(NewName);
}

FString UTSLocalPlayer::SanitizePlayerName(const FString& InName)
{
	FString Result;
	Result.Reserve(InName.Len());
	for (TCHAR Char : InName)
	{
		if (Char == TEXT('?') || Char == TEXT('#') || Char == TEXT('=') || FChar::IsControl(Char))
		{
			continue;
		}
		Result.AppendChar(Char);
	}

	Result.TrimStartAndEndInline();
	Result.LeftInline(MaxPlayerNameLength);
	// Clamping can leave a trailing space behind ("Name       X" -> "Name       ").
	Result.TrimEndInline();
	return Result;
}
