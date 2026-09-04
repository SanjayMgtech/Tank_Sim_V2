#include "Tank/TSTankControllerBase.h"

#include "Net/UnrealNetwork.h"

ATSTankControllerBase::ATSTankControllerBase()
{
	// Phase 8: restore the Blueprint's pre-sized array defaults.
	//
	// These are the ONLY statements this constructor is allowed to grow. The
	// Blueprint shipped these arrays already sized, and the graph indexes into
	// them directly, so leaving them empty would produce
	// "Attempted to access index N from array of length 0" at runtime.
	// Lengths verified identical across all six per-tank Blueprints.
	//
	// This does not weaken RULE 1 or RULE 2: no components are created and no
	// assets are loaded here.
	AntennaCurrentSpeed.Init(FVector::ZeroVector, 30);
	TurretsRotUnstabilized.Init(FRotator::ZeroRotator, 10);
	TurretsRotPrevFrame.Init(FRotator::ZeroRotator, 10);
	GunsRotUnstabilized.Init(FRotator::ZeroRotator, 10);
	GunsRotPrevFrame.Init(FRotator::ZeroRotator, 10);

	// Phase 9: the replicated rotator arrays are pre-sized to 10 in the Blueprint
	// exactly like their non-replicated counterparts above.
	TurretsRot.Init(FRotator::ZeroRotator, 10);
	GunsRot.Init(FRotator::ZeroRotator, 10);

	// Otherwise deliberately empty.
	//
	// The Blueprint (BP_TankController_Chaos) supplies every component and every
	// default value. Recreating them here would re-base the components and silently
	// drop any property not manually copied - the failure mode that sank the first
	// port attempt (see CLAUDE.md section 4).
	//
	// Likewise, no ConstructorHelpers asset loading: FClassFinder on a Blueprint class
	// deadlocks the editor during Blueprint compilation.
}

void ATSTankControllerBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Every property carrying the Replicated specifier MUST be registered here.
	// The Blueprint replicated all three with the default condition (no RepNotify,
	// no replication condition), so plain DOREPLIFETIME reproduces that exactly.
	//
	// If a future phase moves another replicated Blueprint variable, add it here in
	// the same commit. Forgetting is silent in single-player and only breaks over
	// the network.
	DOREPLIFETIME(ATSTankControllerBase, Rep_ControlRotation);
	DOREPLIFETIME(ATSTankControllerBase, TurretsRot);
	DOREPLIFETIME(ATSTankControllerBase, GunsRot);
}
