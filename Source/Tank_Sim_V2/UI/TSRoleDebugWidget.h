// On-screen "who is who" panel for multiplayer testing (net mode, my team/role/tank, every player's
// assignment, and each team's tank + crew occupancy).
//
// The whole widget tree is built in C++ (RebuildWidget populates the WidgetTree), so there is no WBP
// asset to create, reparent or keep in sync - ATSTankPlayerController can instantiate it from
// StaticClass() directly. Derive a Blueprint from it only if you want to restyle it.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TSRoleDebugWidget.generated.h"

class UBorder;
class UTextBlock;

UCLASS()
class UTSRoleDebugWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UTSRoleDebugWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Rebuilds the panel text immediately instead of waiting for the next refresh interval.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|Debug")
	void RefreshNow();

	// The same text the panel shows - handy for printing to the log or into your own HUD.
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|Debug", meta = (ClampMin = "6", ClampMax = "48"))
	int32 FontSize = 13;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Debug")
	TObjectPtr<UBorder> RootBorder;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Debug")
	TObjectPtr<UTextBlock> HeaderText;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tank Simulation|Debug")
	TObjectPtr<UTextBlock> BodyText;

private:
	FString DescribeNetContext() const;
	FString DescribeLocalPlayer() const;
	FString DescribeAllPlayers() const;
	FString DescribeTeamTanks() const;

	float TimeSinceRefresh = 0.f;
};
