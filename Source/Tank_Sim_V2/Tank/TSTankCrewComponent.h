// Driver/Gunner/Commander occupancy and access checks (Section 4). Also owns the tank's TeamId
// (Section 5), since this is the one component guaranteed present on every tank regardless of
// which is the one component guaranteed present on every tank.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/TSTypes.h"
#include "TSTankCrewComponent.generated.h"

class ATSTankPlayerState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTSOnCrewChanged);

UCLASS(ClassGroup = (TankSimulation), meta = (BlueprintSpawnableComponent))
class UTSTankCrewComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTSTankCrewComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Crew")
	ETSTeamId GetTeamId() const { return TeamId; }

	// Server only. Called once by ATSGameMode right after the tank is spawned/assigned to a team.
	void SetTeamId(ETSTeamId NewTeamId);

	// Server only. Returns false if the seat is already occupied by someone else.
	bool TryOccupyRole(ATSTankPlayerState* RequestingPlayerState, ETSCrewRole Role);

	// Server only. Frees whichever seat RequestingPlayerState currently holds on this tank, if any.
	void ReleaseRole(ATSTankPlayerState* RequestingPlayerState);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Crew")
	bool IsRoleOccupied(ETSCrewRole Role) const;

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Crew")
	ATSTankPlayerState* GetOccupant(ETSCrewRole Role) const;

	// True if RequestingPlayerState currently occupies exactly RequiredRole on this tank.
	bool HasAccess(const ATSTankPlayerState* RequestingPlayerState, ETSCrewRole RequiredRole) const;

	UPROPERTY(BlueprintAssignable, Category = "Tank Simulation|Crew")
	FTSOnCrewChanged OnCrewChanged;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_TeamId, BlueprintReadOnly, Category = "Tank Simulation|Crew")
	ETSTeamId TeamId = ETSTeamId::None;

	UPROPERTY(ReplicatedUsing = OnRep_Crew, BlueprintReadOnly, Category = "Tank Simulation|Crew")
	TObjectPtr<ATSTankPlayerState> DriverPlayerState;

	UPROPERTY(ReplicatedUsing = OnRep_Crew, BlueprintReadOnly, Category = "Tank Simulation|Crew")
	TObjectPtr<ATSTankPlayerState> GunnerPlayerState;

	UPROPERTY(ReplicatedUsing = OnRep_Crew, BlueprintReadOnly, Category = "Tank Simulation|Crew")
	TObjectPtr<ATSTankPlayerState> CommanderPlayerState;

	UFUNCTION()
	void OnRep_TeamId();

	UFUNCTION()
	void OnRep_Crew();

	TObjectPtr<ATSTankPlayerState>& GetSlot(ETSCrewRole Role);
};
