// Free-roam camera possessed by the session host instead of ATSVRPawn. The host is a match admin,
// not a participant: it never occupies a crew seat, so it has no tank to sit in and no role-specific
// Input Mapping Context. Derived from ASpectatorPawn purely for the free-flight plumbing it already
// brings - USpectatorPawnMovement plus ADefaultPawn's built-in WASD/mouse axis bindings, which work
// with no extra input assets to author. Assign HostMappingContext in a Blueprint subclass to layer
// project/VR-specific Enhanced Input on top.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpectatorPawn.h"
#include "TSHostCameraPawn.generated.h"

class UCameraComponent;
class UInputMappingContext;

UCLASS()
class ATSHostCameraPawn : public ASpectatorPawn
{
	GENERATED_BODY()

public:
	ATSHostCameraPawn(const FObjectInitializer& ObjectInitializer);

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// The host is a match admin on a FLAT SCREEN, never a VR participant, even when a headset is
	// plugged into the same machine. Possession therefore switches stereo off rather than
	// leaving whatever the previous pawn set - a listen-server host that had been a crew member
	// before, or a host who simply has a headset connected, would otherwise be dropped into a
	// stereo free-cam it cannot fly.
	virtual void NotifyControllerChanged() override;

protected:
	// Explicit camera (rather than relying on APawn::CalcCamera's eye-height fallback) so designers
	// have a component to tune post-process/FOV on. bLockToHmd is cleared on possession: head
	// tracking can still be live while stereo is off, and a free-cam that swings around with a
	// headset sitting on the desk is unusable.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tank Simulation|Host")
	TObjectPtr<UCameraComponent> Camera;

	// Optional. Added at priority 0 on possession for hosts who want the project's own Enhanced Input
	// bindings; the inherited ADefaultPawn axis bindings work without it.
	UPROPERTY(EditDefaultsOnly, Category = "Tank Simulation|Input")
	TObjectPtr<UInputMappingContext> HostMappingContext;
};
