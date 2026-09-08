// A world-space UI panel for VR: the physical surface a headset player actually reads and points at.
//
// WHY THIS EXISTS
// A screen-space widget (AddToViewport) draws onto the flat 2D viewport, and in stereo there is no
// flat viewport - the eyes get render targets. So every existing widget in this project is simply
// invisible to a VR player. World-space UI is not a nicer option, it is the only one that renders.
//
// WHERE IT LIVES, AND WHY THAT IS THE PAWN
// On the crew PAWN, not on the tank. The pawn is already attached to its crew seat by
// UTSCrewStationComponent / ATSVRPawn, so a panel parented to the pawn rides the seat for free -
// and rides the turret too, for the Gunner and Commander whose seats are socketed to it. Putting it
// on the tank would mean duplicating the seat lookup and would leave a player with no panel until
// they were seated, which breaks the menu case entirely.
//
// It is deliberately NOT head-locked. A panel welded to the HMD is the classic way to make people
// sick; this behaves like an instrument panel fixed to the vehicle, which is what a tank crew
// station actually is.
#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "TSVRUIPanelComponent.generated.h"

class UTSUISubsystem;

UCLASS(ClassGroup = (TankSimulation), meta = (BlueprintSpawnableComponent))
class TANK_SIM_V2_API UTSVRUIPanelComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:
	UTSVRUIPanelComponent();

	virtual void BeginPlay() override;

	// Swaps the panel's content. Pass nullptr to clear it.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|VR UI")
	void SetPanelWidgetClass(TSubclassOf<UUserWidget> NewWidgetClass);

	// Re-asks the router whether this panel should be showing. Cheap; call it whenever the
	// presentation mode might have changed (entering VR, changing seat, opening a menu).
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|VR UI")
	void RefreshPanelVisibility();

	// True when the router says world-space AND a widget class is set.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|VR UI")
	bool ShouldPanelBeVisible() const;

protected:
	// Content for this panel. Blueprint data: which panel shows what is a design decision, and the
	// same component serves the lobby console, the role HUDs and the menu.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR UI")
	TSubclassOf<UUserWidget> PanelWidgetClass;

	// Hide the panel on a flat screen. Normally true: the flat player already gets the same content
	// as a screen-space widget, and showing both would double it up. Turn it off to review panel
	// placement on a desktop.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR UI")
	bool bHideWhenPresentationIsFlat = true;

private:
	UTSUISubsystem* GetUISubsystem() const;
};
