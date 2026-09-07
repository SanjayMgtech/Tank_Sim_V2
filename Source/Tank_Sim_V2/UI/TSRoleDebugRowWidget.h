// One player's row in the lobby console (UTSRoleDebugWidget): name, current team/seat, and the
// host's buttons to assign them.
//
// Built entirely in C++ like its parent, so there is no WBP to author. UButton::OnClicked carries no
// payload, so rather than one handler object per button this widget owns a fixed set of buttons and a
// single TargetPlayerState - each handler is a plain UFUNCTION that knows its own team/role.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/TSTypes.h"
#include "TSRoleDebugRowWidget.generated.h"

class APlayerState;
class UButton;
class UHorizontalBox;
class UTextBlock;

UCLASS()
class UTSRoleDebugRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UTSRoleDebugRowWidget(const FObjectInitializer& ObjectInitializer);

	// Points this row at a player. Rows are reused across refreshes so the buttons keep their
	// identity (and don't flicker out from under the cursor) while the roster is stable.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Debug")
	void SetTargetPlayerState(APlayerState* InPlayerState);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Debug")
	APlayerState* GetTargetPlayerState() const;

	// Repaints the labels and re-evaluates which buttons are usable. Cheap - safe to call every tick.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Debug")
	void RefreshRow();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Debug", meta = (ClampMin = "6", ClampMax = "48"))
	int32 FontSize = 13;

	// How many team buttons to show, TeamA first. Mirrors the GameMode's MaxTeams, which clients
	// cannot read (the GameMode only exists on the server).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Debug", meta = (ClampMin = "1", ClampMax = "4"))
	int32 NumTeamButtons = 4;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> TeamButtons;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> RoleButtons;

	UPROPERTY(Transient)
	TObjectPtr<UButton> ClearButton;

private:
	UFUNCTION() void OnTeamAClicked();
	UFUNCTION() void OnTeamBClicked();
	UFUNCTION() void OnTeamCClicked();
	UFUNCTION() void OnTeamDClicked();
	UFUNCTION() void OnDriverClicked();
	UFUNCTION() void OnGunnerClicked();
	UFUNCTION() void OnCommanderClicked();
	UFUNCTION() void OnClearClicked();

	void AssignTeam(ETSTeamId Team);
	void AssignRole(ETSCrewRole Role);

	UButton* MakeButton(const FString& Label, float MinWidth);
	class ATSTankPlayerController* GetOwningTankController() const;

	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerState> TargetPlayerState;
};
