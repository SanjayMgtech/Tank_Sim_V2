// Radar/intel/command contract (Section 4). Full access is Commander-only; Gunner gets LIMITED
// access per the Section 8 permission matrix (see UTSTankCommanderComponent::GetIntelFor).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/TSTypes.h"
#include "TSTankCommanderComponent.generated.h"

class ATSTankPlayerState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTSOnCrewCommandIssued, ETSCrewCommand, Command);

UCLASS(ClassGroup = (TankSimulation), meta = (BlueprintSpawnableComponent))
class UTSTankCommanderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTSTankCommanderComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Server only. Commander-only. Recomputes Intel from GameState's team/tank roster; each client's
	// own OnRep_Intel then pushes a role-filtered copy (see GetIntelFor) to the Blueprint via
	// ITSTankInterface::BP_UpdateCommanderIntel.
	bool TryRefreshIntel(ATSTankPlayerState* Requester);

	// Server only. Commander-only. Broadcasts Command to every crew member's HUD via OnCrewCommandIssued.
	bool TryIssueCommand(ATSTankPlayerState* Requester, ETSCrewCommand Command);

	// Returns Intel for Full access, a reduced copy (positions only, no summary) for Limited access,
	// and an empty struct for Denied - mirrors FTSPermissions::GetAccessLevel(RadarIntel).
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Commander")
	FTSCommanderIntel GetIntelFor(ETSCrewRole RequestingRole) const;

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|Commander")
	FTSOnCrewCommandIssued OnCrewCommandIssued;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_Intel, BlueprintReadOnly, Category = "Tank Simulation|Commander")
	FTSCommanderIntel Intel;

	UPROPERTY(ReplicatedUsing = OnRep_LastCommand, BlueprintReadOnly, Category = "Tank Simulation|Commander")
	ETSCrewCommand LastIssuedCommand = ETSCrewCommand::None;

	UFUNCTION()
	void OnRep_Intel();

	UFUNCTION()
	void OnRep_LastCommand();

	class UTSTankCrewComponent* GetCrewComponent() const;
};
