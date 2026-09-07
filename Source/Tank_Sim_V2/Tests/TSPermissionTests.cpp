// Tier 1 automation tests for the role/permission layer.
// See Docs/Testing_And_Role_Access_Plan.md for why these exist and what they deliberately do NOT
// cover: ownership and replication are invisible to any single-process test and need the manual
// two-window session.
//
// Run headless:
//   UnrealEditor-Cmd.exe <project> -ExecCmds="Automation RunTests TankSim.Permissions" -unattended -nop4 -testexit="Automation Test Queue Empty"
// or through the editor's Session Frontend / the run_automation_tests action, filter "TankSim".

#include "Misc/AutomationTest.h"

#include "Core/TSTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Player/TSTankPlayerState.h"
#include "Tank/TSTankCrewComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

// =============================================================================================
// 1. The Section 8 permission matrix.
//
// FTSPermissions is a pure static function, so this needs no world, no actors and no engine
// state - which is exactly why it is the cheapest possible protection for the most
// security-relevant table in the project.
//
// The table below is written out in full rather than generated from the implementation. A test
// that derives its expectations from the code under test proves only that the code equals
// itself; this one encodes the DOCUMENTED contract, so changing the implementation without
// changing the doc fails here.
// =============================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTSPermissionMatrixTest,
	"TankSim.Permissions.Matrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTSPermissionMatrixTest::RunTest(const FString& Parameters)
{
	using ELvl = ETSAccessLevel;

	struct FCase
	{
		ETSCapability Capability;
		const TCHAR* CapabilityName;
		ELvl None;
		ELvl Driver;
		ELvl Gunner;
		ELvl Commander;
	};

	// Section 8 of the framework doc, transcribed. Voice and TankStatus are crew-wide; RadarIntel
	// is the only row with a partial grant.
	static const FCase Cases[] =
	{
		{ ETSCapability::Drive,        TEXT("Drive"),        ELvl::Denied, ELvl::Full,   ELvl::Denied,  ELvl::Denied },
		{ ETSCapability::MainCannon,   TEXT("MainCannon"),   ELvl::Denied, ELvl::Denied, ELvl::Full,    ELvl::Denied },
		{ ETSCapability::MachineGun,   TEXT("MachineGun"),   ELvl::Denied, ELvl::Denied, ELvl::Full,    ELvl::Denied },
		{ ETSCapability::TurretAim,    TEXT("TurretAim"),    ELvl::Denied, ELvl::Denied, ELvl::Full,    ELvl::Denied },
		{ ETSCapability::RadarIntel,   TEXT("RadarIntel"),   ELvl::Denied, ELvl::Denied, ELvl::Limited, ELvl::Full   },
		{ ETSCapability::CrewCommands, TEXT("CrewCommands"), ELvl::Denied, ELvl::Denied, ELvl::Denied,  ELvl::Full   },
		{ ETSCapability::Voice,        TEXT("Voice"),        ELvl::Denied, ELvl::Full,   ELvl::Full,    ELvl::Full   },
		{ ETSCapability::TankStatus,   TEXT("TankStatus"),   ELvl::Denied, ELvl::Full,   ELvl::Full,    ELvl::Full   },
	};

	auto Check = [this](const TCHAR* CapName, const TCHAR* RoleName, ETSCrewRole InRole, ETSCapability Cap, ELvl Expected)
	{
		const ELvl Actual = FTSPermissions::GetAccessLevel(InRole, Cap);
		TestEqual(FString::Printf(TEXT("%s / %s"), RoleName, CapName), static_cast<int32>(Actual), static_cast<int32>(Expected));
	};

	for (const FCase& C : Cases)
	{
		Check(C.CapabilityName, TEXT("None"),      ETSCrewRole::None,      C.Capability, C.None);
		Check(C.CapabilityName, TEXT("Driver"),    ETSCrewRole::Driver,    C.Capability, C.Driver);
		Check(C.CapabilityName, TEXT("Gunner"),    ETSCrewRole::Gunner,    C.Capability, C.Gunner);
		Check(C.CapabilityName, TEXT("Commander"), ETSCrewRole::Commander, C.Capability, C.Commander);
	}

	// HasFullAccess must be strictly Full. This is the guard for gap 3 in the plan: the single
	// Limited cell collapses to "denied" under a boolean check, so anything gating intel on
	// HasFullAccess would silently lock the Gunner out of partial radar access.
	TestFalse(TEXT("HasFullAccess is FALSE for the Limited cell (Gunner/RadarIntel)"),
		FTSPermissions::HasFullAccess(ETSCrewRole::Gunner, ETSCapability::RadarIntel));
	TestTrue(TEXT("...but GetAccessLevel still reports Limited, not Denied"),
		FTSPermissions::GetAccessLevel(ETSCrewRole::Gunner, ETSCapability::RadarIntel) == ELvl::Limited);

	// ETSCrewRole::None must be denied everything. A player is None before assignment and after a
	// seat is released, so a gap here would let an unassigned player act.
	for (const FCase& C : Cases)
	{
		TestTrue(FString::Printf(TEXT("None is denied %s"), C.CapabilityName),
			FTSPermissions::GetAccessLevel(ETSCrewRole::None, C.Capability) == ELvl::Denied);
	}

	return true;
}

// =============================================================================================
// 2. Crew seat occupancy.
//
// Needs a world: TryOccupyRole requires GetOwner()->HasAuthority(), so the component must live on
// a real actor. A transient game world with no net driver reports authority, which is what we
// want - this is the server-side rule being tested.
// =============================================================================================

namespace
{
	struct FTSCrewFixture
	{
		UWorld* World = nullptr;
		AActor* TankStandIn = nullptr;
		UTSTankCrewComponent* Crew = nullptr;

		bool Setup()
		{
			World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
			if (!World)
			{
				return false;
			}

			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());

			TankStandIn = World->SpawnActor<AActor>();
			if (!TankStandIn)
			{
				return false;
			}

			// A bare AActor standing in for the tank. The crew component does not care what it is
			// attached to - only that the owner has authority - so this keeps the test independent
			// of the tank Blueprint and of Chaos vehicle setup.
			Crew = NewObject<UTSTankCrewComponent>(TankStandIn);
			Crew->RegisterComponent();

			return Crew != nullptr;
		}

		ATSTankPlayerState* SpawnPlayerState() const
		{
			return World ? World->SpawnActor<ATSTankPlayerState>() : nullptr;
		}

		void TearDown()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				World = nullptr;
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTSCrewOccupancyTest,
	"TankSim.Permissions.CrewOccupancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTSCrewOccupancyTest::RunTest(const FString& Parameters)
{
	FTSCrewFixture Fixture;
	if (!TestTrue(TEXT("fixture world and crew component created"), Fixture.Setup()))
	{
		Fixture.TearDown();
		return false;
	}

	ATSTankPlayerState* Alice = Fixture.SpawnPlayerState();
	ATSTankPlayerState* Bob = Fixture.SpawnPlayerState();
	ATSTankPlayerState* Carol = Fixture.SpawnPlayerState();

	if (!TestTrue(TEXT("three PlayerStates spawned"), Alice && Bob && Carol))
	{
		Fixture.TearDown();
		return false;
	}

	UTSTankCrewComponent* Crew = Fixture.Crew;

	// --- empty to start ---
	TestFalse(TEXT("Driver seat starts empty"), Crew->IsRoleOccupied(ETSCrewRole::Driver));
	TestNull(TEXT("no Driver occupant initially"), Crew->GetOccupant(ETSCrewRole::Driver));

	// --- basic occupancy ---
	TestTrue(TEXT("Alice takes Driver"), Crew->TryOccupyRole(Alice, ETSCrewRole::Driver));
	TestTrue(TEXT("Driver now occupied"), Crew->IsRoleOccupied(ETSCrewRole::Driver));
	TestEqual(TEXT("Driver occupant is Alice"), Crew->GetOccupant(ETSCrewRole::Driver), Alice);

	// Taking one seat must not fill the others - the bug where a single flag stands in for three.
	TestFalse(TEXT("Gunner still free after Alice takes Driver"), Crew->IsRoleOccupied(ETSCrewRole::Gunner));
	TestFalse(TEXT("Commander still free after Alice takes Driver"), Crew->IsRoleOccupied(ETSCrewRole::Commander));

	// --- exclusivity: the headline rule ---
	TestFalse(TEXT("Bob CANNOT take the occupied Driver seat"), Crew->TryOccupyRole(Bob, ETSCrewRole::Driver));
	TestEqual(TEXT("Driver seat still held by Alice after Bob's attempt"), Crew->GetOccupant(ETSCrewRole::Driver), Alice);

	// --- three distinct crew on one tank ---
	TestTrue(TEXT("Bob takes Gunner"), Crew->TryOccupyRole(Bob, ETSCrewRole::Gunner));
	TestTrue(TEXT("Carol takes Commander"), Crew->TryOccupyRole(Carol, ETSCrewRole::Commander));
	TestEqual(TEXT("Gunner is Bob"), Crew->GetOccupant(ETSCrewRole::Gunner), Bob);
	TestEqual(TEXT("Commander is Carol"), Crew->GetOccupant(ETSCrewRole::Commander), Carol);

	// --- HasAccess is per-seat, not per-role-name ---
	TestTrue(TEXT("Alice has Driver access"), Crew->HasAccess(Alice, ETSCrewRole::Driver));
	TestFalse(TEXT("Alice does NOT have Gunner access"), Crew->HasAccess(Alice, ETSCrewRole::Gunner));
	TestFalse(TEXT("Bob does NOT have Driver access"), Crew->HasAccess(Bob, ETSCrewRole::Driver));
	TestFalse(TEXT("a null requester has no access"), Crew->HasAccess(nullptr, ETSCrewRole::Driver));

	// --- one seat per player: moving seats must vacate the old one ---
	// Alice moves Driver -> (Driver is hers; free Gunner is Bob's) so use a fresh player instead.
	Crew->ReleaseRole(Bob);
	TestFalse(TEXT("Gunner freed after Bob released"), Crew->IsRoleOccupied(ETSCrewRole::Gunner));
	TestTrue(TEXT("Alice moves from Driver to Gunner"), Crew->TryOccupyRole(Alice, ETSCrewRole::Gunner));
	TestFalse(TEXT("Alice's old Driver seat is now FREE"), Crew->IsRoleOccupied(ETSCrewRole::Driver));
	TestEqual(TEXT("Alice now holds Gunner"), Crew->GetOccupant(ETSCrewRole::Gunner), Alice);

	// --- release frees the seat for someone else (the disconnect path) ---
	Crew->ReleaseRole(Alice);
	TestFalse(TEXT("Gunner free after Alice released"), Crew->IsRoleOccupied(ETSCrewRole::Gunner));
	TestTrue(TEXT("Bob can now take the freed Gunner seat"), Crew->TryOccupyRole(Bob, ETSCrewRole::Gunner));

	// --- re-taking your own seat is a no-op success, not a failure ---
	TestTrue(TEXT("Bob re-taking his own seat succeeds"), Crew->TryOccupyRole(Bob, ETSCrewRole::Gunner));
	TestEqual(TEXT("Bob still holds Gunner"), Crew->GetOccupant(ETSCrewRole::Gunner), Bob);

	// --- rejected inputs ---
	TestFalse(TEXT("ETSCrewRole::None is not an occupiable seat"), Crew->TryOccupyRole(Carol, ETSCrewRole::None));
	TestFalse(TEXT("a null PlayerState cannot occupy a seat"), Crew->TryOccupyRole(nullptr, ETSCrewRole::Driver));

	Fixture.TearDown();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
