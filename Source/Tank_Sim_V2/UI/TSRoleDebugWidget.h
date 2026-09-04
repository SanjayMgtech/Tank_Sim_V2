// The lobby console: net/match state, a row per connected player, and each team's tank with its crew.
//
// On the host every row carries buttons to put that player on a team and into a seat, which is the
// assignment flow - the host builds the crews, rather than every player racing to self-select. On a
// client the same rows render read-only, so everyone can see the crew list forming.
//
// The whole widget tree is built in C++ (RebuildWidget populates the WidgetTree), so there is no WBP
// asset to create, reparent or keep in sync - ATSTankPlayerController instantiates it from
// StaticClass() directly. Derive a Blueprint from it only if you want to restyle it.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TSRoleDebugWidget.generated.h"

class UBorder;
class UTextBlock;
class UTSRoleDebugRowWidget;
class UVerticalBox;

UCLASS()
class UTSRoleDebugWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UTSRoleDebugWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

	// Rebuilds the panel immediately instead of waiting for the next refresh interval.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Debug")
	void RefreshNow();

	// The status text the panel shows (without the player rows) - handy for logging.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Debug")
	FString BuildDebugString() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Debug", meta = (ClampMin = "0.0"))
	float RefreshInterval = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Debug")
	bool bShowAllPlayers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Debug")
	bool bShowTeamTanks = true;

	// While the host is assigning crews, they need a cursor to click the buttons with. The panel takes
	// one for the host only, and only until the match reaches InProgress - it hands input back the
	// moment the match starts, so it never eats gameplay input.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Debug")
	bool bTakeMouseCursorForAssignment = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Debug", meta = (ClampMin = "6", ClampMax = "48"))
	int32 FontSize = 13;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Debug")
	TObjectPtr<UBorder> RootBorder;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Debug")
	TObjectPtr<UTextBlock> HeaderText;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Debug")
	TObjectPtr<UTextBlock> BodyText;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Debug")
	TObjectPtr<UTextBlock> PlayersHeaderText;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Debug")
	TObjectPtr<UVerticalBox> PlayerRowsBox;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Debug")
	TObjectPtr<UTextBlock> TankText;

private:
	FString DescribeNetContext() const;
	FString DescribeLocalPlayer() const;
	FString DescribeTeamTanks() const;

	// Rows are pooled and re-pointed rather than rebuilt, so buttons keep their identity and do not
	// flicker out from under the cursor on every refresh.
	void RefreshPlayerRows();
	void UpdateInputModeForAssignment();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTSRoleDebugRowWidget>> PlayerRows;

	float TimeSinceRefresh = 0.f;
	bool bCursorTakenForAssignment = false;
};
