// The VR laser pointer: turns a motion controller into a mouse for world-space UI.
//
// WHY THIS SUBCLASSES INSTEAD OF USING UWidgetInteractionComponent DIRECTLY
// The stock component's automatic hit testing calls
// GetRelatedComponentsToIgnoreInAutomaticHitTesting, which walks from
// Root->GetAttachmentRoot() and ignores every primitive underneath it. That is fine for a free
// standing character. It is fatal here: under the crew model the PAWN IS ATTACHED TO THE TANK, so
// the pawn's attachment root is the tank, and the stock trace would ignore the entire tank AND the
// pawn - including the very panel we are trying to click. The failure mode is a laser that visibly
// points at a button and does nothing.
//
// So the interaction source is Custom and this class supplies its own hit result. That is the
// supported escape hatch for exactly this situation, not a workaround.
#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetInteractionComponent.h"
#include "TSVRPointerComponent.generated.h"

class UInputAction;

UCLASS(ClassGroup = (TankSimulation), meta = (BlueprintSpawnableComponent))
class TANK_SIM_V2_API UTSVRPointerComponent : public UWidgetInteractionComponent
{
	GENERATED_BODY()

public:
	UTSVRPointerComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Click. Separate press/release rather than one "click" so a widget can see a held button -
	// sliders and drag behaviour need both edges.
	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|VR UI")
	void PressPointer();

	UFUNCTION(BlueprintCallable, Category = "Tank Simulation|VR UI")
	void ReleasePointer();

	// True when the laser is currently over a widget component. Useful for showing the beam only
	// when it would do something.
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|VR UI")
	bool IsPointingAtWidget() const;

protected:
	// Trigger action. IMC_VR_Widget maps IA_Primary to the right-hand trigger.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR UI")
	TObjectPtr<UInputAction> ClickAction;

	// Optional: name of a component on the owning pawn to attach to at BeginPlay, e.g. the motion
	// controller for this hand.
	//
	// Needed because a pointer often belongs on a NATIVE component (ATSVRPawn creates LeftHand and
	// RightHand in C++), and the Blueprint tooling cannot re-parent an SCS component onto a native
	// one - the same limitation that put the turret crew seats in AttachTurretCrewSeats. Leave it
	// None to keep whatever parent the Blueprint authored, which is what BP_XRPawn does (its motion
	// controllers are SCS, so it parents them in the editor).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR UI")
	FName AttachToComponentName;

	// Only widget components can be hit, so tank geometry never blocks the ray.
	//
	// Deliberate: the crew sit INSIDE a hull, and a panel mounted on a seat is often behind or
	// inside collision geometry from the pointer's point of view. A physically honest ray would be
	// blocked by the tank the player is sitting in and the UI would be unusable. The cost is that a
	// panel cannot be occluded by world geometry - acceptable for cockpit UI, and the reason this is
	// a flag rather than a hard rule.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tank Simulation|VR UI")
	bool bOnlyHitWidgetComponents = true;

private:
	void UpdateCustomHit();
	void BindClickAction();

	UFUNCTION()
	void HandlePawnRestarted(APawn* Pawn);

	void Input_Click_Pressed();
	void Input_Click_Released();

	bool bClickBound = false;
};
