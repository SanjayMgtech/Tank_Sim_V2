// Cross-tank access denial - gap 1 in Docs/Testing_And_Role_Access_Plan.md, and the highest-risk
// untested path in the role layer.
//
// Every permission check in this project is TWO factors:
//   1. the requester's ROLE grants the capability (FTSPermissions, the Section 8 matrix), and
//   2. the requester occupies that seat ON THIS TANK (UTSTankCrewComponent::HasAccess).
//
// Factor 1 has good coverage. Factor 2 had NONE: every test so far used a single tank, where a
// legitimate Gunner is a legitimate Gunner. Factor 2 exists solely to stop Team B's Gunner - who
// genuinely holds the Gunner capability - operating Team A's tank, and nothing had ever put that
// question to the code.
//
// The plan originally scheduled this as a Tier 2 functional map test on the assumption it needed
// two teams and a GameMode. It does not: the rule lives in the components, so two tank stand-ins
// with their own crew components reproduce it exactly, in a transient world, with no map, no
// GameMode and no PIE.

#include "Misc/AutomationTest.h"

#include "Core/TSTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankControlComponent.h"
#include "Tank/TSTankCrewComponent.h"
#include "Tank/TSTankWeaponComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// One tank's worth of framework components on a bare actor. Deliberately not the tank
	// Blueprint: the access rule lives entirely in these components, so testing against a plain
	// AActor keeps the test independent of Chaos vehicle setup and of the Blueprint itself.
	struct FTSTankStandIn
	{
		AActor* Actor = nullptr;
		UTSTankCrewComponent* Crew = nullptr;
		UTSTankWeaponComponent* Weapon = nullptr;
		UTSTankControlComponent* Control = nullptr;

		void Build(UWorld* World)
		{
			Actor = World->SpawnActor<AActor>();
			Crew = NewObject<UTSTankCrewComponent>(Actor);
			Crew->RegisterComponent();
			Weapon = NewObject<UTSTankWeaponComponent>(Actor);
			Weapon->RegisterComponent();
			Control = NewObject<UTSTankControlComponent>(Actor);
			Control->RegisterComponent();
		}

		bool IsValid() const { return Actor && Crew && Weapon && Control; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTSCrossTankAccessTest,
	"TankSim.Permissions.CrossTankDenial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTSCrossTankAccessTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	if (!TestNotNull(TEXT("transient world created"), World))
	{
		return false;
	}

	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	FTSTankStandIn TankA;
	FTSTankStandIn TankB;
	TankA.Build(World);
	TankB.Build(World);

	ATSTankPlayerState* GunnerA = World->SpawnActor<ATSTankPlayerState>();
	ATSTankPlayerState* DriverA = World->SpawnActor<ATSTankPlayerState>();
	ATSTankPlayerState* GunnerB = World->SpawnActor<ATSTankPlayerState>();

	if (!TestTrue(TEXT("two tanks and three crew created"),
		TankA.IsValid() && TankB.IsValid() && GunnerA && DriverA && GunnerB))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// Crew the two tanks. Note both Gunners are *legitimately* Gunners - this test is not about
	// someone holding the wrong role, it is about the right role on the wrong tank.
	TestTrue(TEXT("GunnerA takes the Gunner seat on tank A"), TankA.Crew->TryOccupyRole(GunnerA, ETSCrewRole::Gunner));
	TestTrue(TEXT("DriverA takes the Driver seat on tank A"), TankA.Crew->TryOccupyRole(DriverA, ETSCrewRole::Driver));
	TestTrue(TEXT("GunnerB takes the Gunner seat on tank B"), TankB.Crew->TryOccupyRole(GunnerB, ETSCrewRole::Gunner));

	GunnerA->SetCrewRole(ETSCrewRole::Gunner);
	DriverA->SetCrewRole(ETSCrewRole::Driver);
	GunnerB->SetCrewRole(ETSCrewRole::Gunner);

	// --- sanity: each crew CAN act on its own tank, or the denials below prove nothing ---
	TestTrue(TEXT("GunnerA can aim tank A"), TankA.Weapon->TryAimTurret(GunnerA, FVector_NetQuantize(100.f, 0.f, 0.f)));
	TestTrue(TEXT("GunnerB can aim tank B"), TankB.Weapon->TryAimTurret(GunnerB, FVector_NetQuantize(200.f, 0.f, 0.f)));
	TestTrue(TEXT("DriverA can drive tank A"), TankA.Control->TryApplyDriveInput(DriverA, 1.f, 0.f));

	// =========================================================================================
	// THE POINT OF THIS TEST: a valid Gunner on the WRONG tank.
	//
	// GunnerB holds ETSCrewRole::Gunner, so FTSPermissions grants every gunner capability. Only
	// factor 2 - not occupying a seat on tank A - can stop this.
	// =========================================================================================
	TestFalse(TEXT("GunnerB CANNOT aim tank A"),
		TankA.Weapon->TryAimTurret(GunnerB, FVector_NetQuantize(999.f, 0.f, 0.f)));
	TestFalse(TEXT("GunnerB CANNOT fire tank A's main cannon"),
		TankA.Weapon->TryFireMainCannon(GunnerB));
	TestFalse(TEXT("GunnerB CANNOT fire tank A's machine gun"),
		TankA.Weapon->TryFireMachineGun(GunnerB));
	TestFalse(TEXT("GunnerB CANNOT reload tank A"),
		TankA.Weapon->TryReload(GunnerB));

	// And the same in the other direction, so this is not an artefact of which tank was built first.
	TestFalse(TEXT("GunnerA CANNOT aim tank B"),
		TankB.Weapon->TryAimTurret(GunnerA, FVector_NetQuantize(888.f, 0.f, 0.f)));

	// Driving is the same rule through a different component.
	TestFalse(TEXT("DriverA CANNOT drive tank B"),
		TankB.Control->TryApplyDriveInput(DriverA, 1.f, 0.f));

	// --- the denial must not have corrupted the legitimate state ---
	// If a rejected request still wrote through before returning false, the aim point would carry
	// the intruder's 999. It must still hold GunnerA's 100.
	TestEqual(TEXT("tank A's aim point is unchanged by the rejected cross-tank requests"),
		static_cast<float>(TankA.Weapon->GetCurrentAimPoint().X), 100.f);
	TestEqual(TEXT("tank B's aim point is likewise untouched"),
		static_cast<float>(TankB.Weapon->GetCurrentAimPoint().X), 200.f);

	// --- an unseated player with a role name is still denied ---
	// Covers the window between a role being set on the PlayerState and a seat actually being
	// taken, and the state after a seat is released.
	ATSTankPlayerState* Freeloader = World->SpawnActor<ATSTankPlayerState>();
	Freeloader->SetCrewRole(ETSCrewRole::Gunner);
	TestFalse(TEXT("a player claiming Gunner but holding NO seat is denied"),
		TankA.Weapon->TryAimTurret(Freeloader, FVector_NetQuantize(777.f, 0.f, 0.f)));

	// --- releasing a seat revokes access immediately ---
	TankA.Crew->ReleaseRole(GunnerA);
	TestFalse(TEXT("GunnerA loses tank A access the moment the seat is released"),
		TankA.Weapon->TryAimTurret(GunnerA, FVector_NetQuantize(555.f, 0.f, 0.f)));
	TestEqual(TEXT("tank A's aim point still unchanged after the revoked request"),
		static_cast<float>(TankA.Weapon->GetCurrentAimPoint().X), 100.f);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
