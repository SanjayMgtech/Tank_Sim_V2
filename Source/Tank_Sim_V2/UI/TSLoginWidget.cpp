#include "UI/TSLoginWidget.h"

#include "Components/EditableTextBox.h"
#include "Engine/GameInstance.h"
#include "Player/TSLocalPlayer.h"

#define LOCTEXT_NAMESPACE "TSLoginWidget"

UTSLocalPlayer* UTSLoginWidget::GetTSLocalPlayer() const
{
	// A WBP created without an Owning Player has no owning local player; fall back to the primary one
	// (the same player UTSSessionSubsystem hosts/joins with).
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	if (!LocalPlayer && GetGameInstance())
	{
		LocalPlayer = GetGameInstance()->GetFirstGamePlayer();
	}
	return Cast<UTSLocalPlayer>(LocalPlayer);
}

void UTSLoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (!PlayerNameTextBox)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTSLoginWidget: no Editable Text Box named 'PlayerNameTextBox' - players will use their PC name."));
		return;
	}

	if (const UTSLocalPlayer* LocalPlayer = GetTSLocalPlayer())
	{
		// Returning to the login screen keeps the name typed last time.
		PlayerNameTextBox->SetText(FText::FromString(LocalPlayer->GetPlayerDisplayName()));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("UTSLoginWidget: local player is not a UTSLocalPlayer - check LocalPlayerClassName in DefaultEngine.ini and restart the editor."));
	}

	if (PlayerNameTextBox->GetHintText().IsEmpty())
	{
		PlayerNameTextBox->SetHintText(LOCTEXT("PlayerNameHint", "Player name (blank = PC name)"));
	}

	PlayerNameTextBox->OnTextChanged.AddDynamic(this, &UTSLoginWidget::HandlePlayerNameChanged);
}

void UTSLoginWidget::NativeDestruct()
{
	if (PlayerNameTextBox)
	{
		PlayerNameTextBox->OnTextChanged.RemoveDynamic(this, &UTSLoginWidget::HandlePlayerNameChanged);
	}

	Super::NativeDestruct();
}

void UTSLoginWidget::NotifyLogin()
{
	CommitPlayerName();
	OnLogin.Broadcast();
}

void UTSLoginWidget::NotifyContinue()
{
	CommitPlayerName();
	OnContinue.Broadcast();
}

void UTSLoginWidget::CommitPlayerName()
{
	if (!PlayerNameTextBox)
	{
		return;
	}

	if (UTSLocalPlayer* LocalPlayer = GetTSLocalPlayer())
	{
		LocalPlayer->SetPlayerDisplayName(PlayerNameTextBox->GetText().ToString());
	}
}

void UTSLoginWidget::HandlePlayerNameChanged(const FText& Text)
{
	// Hard cap while typing so the player sees exactly what will be sent (the server clamps too).
	const FString Typed = Text.ToString();
	if (Typed.Len() > UTSLocalPlayer::MaxPlayerNameLength)
	{
		PlayerNameTextBox->SetText(FText::FromString(Typed.Left(UTSLocalPlayer::MaxPlayerNameLength)));
	}

	CommitPlayerName();
}

#undef LOCTEXT_NAMESPACE
