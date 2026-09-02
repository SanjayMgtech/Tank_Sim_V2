// Validated control requests forwarded to the tank Blueprint (Section 4/9). No client-callable RPCs
// live here: ATSTankPlayerController is the only actor with a real per-client NetConnection, so it
// owns the Server RPC boundary and calls straight into TryApplyDriveInput once already server-side.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/TSTypes.h"
#include "TSTankControlComponent.generated.h"

class ATSTankPlayerState;

UCLASS(ClassGroup = (TankSimulation), meta = (BlueprintSpawnableComponent))
class UTSTankControlComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTSTankControlComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Server only. Re-validates Driver occupancy via the sibling crew component, then forwards to
	// the Blueprint via ITSTankInterface::BP_SetDriveInput. Returns false if denied.
	bool TryApplyDriveInput(ATSTankPlayerState* Requester, float Throttle, float Steering);

protected:
	// X = Throttle, Y = Steering. Replicated (rather than pushed via a per-call multicast) since this
	// changes at VR-input rate; ATSTank's NetUpdateFrequency already throttles how often it actually
	// goes out, matching the doc's Section 10 guidance to reserve multicast for one-shot transients.
	UPROPERTY(ReplicatedUsing = OnRep_DriveInput, BlueprintReadOnly, Category = "Tank Simulation|Control")
	FVector2D CurrentDriveInput = FVector2D::ZeroVector;

	UFUNCTION()
	void OnRep_DriveInput();

	class UTSTankCrewComponent* GetCrewComponent() const;
};
