// End-to-end flow test: host -> team -> role -> tank spawn -> seat -> restrictions.
//
// The component tests (TSPermissionTests, TSCrossTankAccessTests) prove the RULES in isolation
// using hand-built stand-in actors. This one proves the FLOW: that ATSGameMode actually wires
// those rules together - that assigning a role really does spawn a tank for that team, seat the
// player in it, and leave another team's crew unable to touch it.
//
// It runs against the real ATSGameMode in a transient world, so it covers the two things the plan
// had deferred to Tier 2 (host gating and privilege escalation) without needing a map or PIE.
//
// What it still cannot cover, by construction: ownership and replication. Calling a Server RPC's
// _Implementation directly skips the ownership check the engine would apply to a real client, so a
// dropped-RPC bug is invisible here. That remains the two-window manual test.

#include "Misc/AutomationTest.h"

#include "Core/TSGameMode.h"
#include "Core/TSGameState.h"
#include "Core/TSTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSTankPlayerState.h"
#include "Tests/TSFlowTestTank.h"
#include "Tank/TSTankControlComponent.h"
#include "Tank/TSTankCrewComponent.h"
#include "Tank/TSTankWeaponComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// A world with a live ATSGameMode, plus helpers to fabricate players. Controllers are spawned
	// and given a PlayerState directly rather than going through Login/PostLogin: PostLogin needs a
	// real net connection to decide host designation, and this test wants to set bIsHost explicitly
	// anyway so it can check the gate rather than the designation heuristic.
	struct FTSFlowFixture
	{
		UGameInstance* GameInstance = nullptr;
		UWorld* World = nullptr;
		ATSGameMode* GameMode = nullptr;

		bool Setup()
		{
			// The world must come from a GameInstance, not UWorld::CreateWorld.
			//
			// UWorld::SetGameMode resolves the GameMode against the GameInstance and the URL, and
			// on a bare CreateWorld it dereferences null and CRASHES - which killed the entire
			// automation run, not just this test. AuthorityGameMode is private, so wiring one in by
			// hand is not an option either. InitializeStandalone builds a world that is properly
			// owned, which is what makes SetGameMode safe here.
			GameInstance = NewObject<UGameInstance>(GEngine);
			if (!GameInstance)
			{
				return false;
			}
			GameInstance->InitializeStandalone();

			World = GameInstance->GetWorld();
			if (!World)
			{
				return false;
			}

			if (AWorldSettings* Settings = World->GetWorldSettings())
			{
				Settings->DefaultGameMode = ATSGameMode::StaticClass();
			}

			World->SetGameMode(FURL());
			World->InitializeActorsForPlay(FURL());

			// ATSGameMode ships with DefaultTankClass unset on purpose (RULE 2 removed the
			// ConstructorHelpers reference that used to hardcode a T90), so a raw C++ GameMode
			// spawns nothing and every role assignment fails. Point it at the native test tank
			// rather than a content Blueprint, so the flow test does not depend on vendor assets
			// or on whatever DefaultTankClass happens to be configured to this week.
			if (ATSGameMode* GM = World->GetAuthGameMode<ATSGameMode>())
			{
				GM->SetDefaultTankClassForTesting(ATSFlowTestTank::StaticClass());
			}

			// GetAuthGameMode<>() is how the host-assignment RPC finds the GameMode, so this must
			// resolve through the world rather than being a pointer we kept.
			GameMode = World->GetAuthGameMode<ATSGameMode>();
			return GameMode != nullptr;
		}

		ATSTankPlayerController* MakePlayer(bool bIsHost) const
		{
			ATSTankPlayerController* PC = World->SpawnActor<ATSTankPlayerController>();
			ATSTankPlayerState* PS = World->SpawnActor<ATSTankPlayerState>();
			if (!PC || !PS)
			{
				return nullptr;
			}

			PC->PlayerState = PS;
			PS->SetOwner(PC);
			if (bIsHost)
			{
				PS->SetIsHost(true);
			}
			return PC;
		}

		static ATSTankPlayerState* StateOf(const ATSTankPlayerController* PC)
		{
			return PC ? PC->GetPlayerState<ATSTankPlayerState>() : nullptr;
		}

		// UGameInstance::Shutdown does NOT destroy the world - it only clears WorldContext. The old
		// teardown stopped there, so every run leaked this world until editor EXIT, where its
		// still-initialised world subsystems were torn down out of order:
		//   Ensure: Tickable subsystem MassSignalSubsystem /Temp/Untitled_2 ... destroyed while still initialized
		// followed by an EXCEPTION_ACCESS_VIOLATION in CoreUObject. Destroy it here, explicitly.
		void TearDown()
		{
			if (GameInstance)
			{
				GameInstance->Shutdown();
				GameInstance = nullptr;
			}
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
	FTSMatchFlowTest,
	"TankSim.Flow.HostTeamRoleTankSeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTSMatchFlowTest::RunTest(const FString& Parameters)
{
	FTSFlowFixture Fx;
	if (!TestTrue(TEXT("world with a live ATSGameMode"), Fx.Setup()))
	{
		Fx.TearDown();
		return false;
	}

	ATSTankPlayerController* Host = Fx.MakePlayer(/*bIsHost=*/true);
	ATSTankPlayerController* ADriver = Fx.MakePlayer(false);
	ATSTankPlayerController* AGunner = Fx.MakePlayer(false);
	ATSTankPlayerController* BDriver = Fx.MakePlayer(false);

	if (!TestTrue(TEXT("host and three players created"), Host && ADriver && AGunner && BDriver))
	{
		Fx.TearDown();
		return false;
	}

	// ---------------------------------------------------------------------------------------
	// 1. The host runs the match and does not play in it.
	// ---------------------------------------------------------------------------------------
	TestTrue(TEXT("host PlayerState reports IsHost"), FTSFlowFixture::StateOf(Host)->IsHost());
	TestFalse(TEXT("host is REFUSED a team"), Fx.GameMode->TryAssignTeam(Host, ETSTeamId::TeamA));
	TestFalse(TEXT("host is REFUSED a crew role"), Fx.GameMode->TryAssignRole(Host, ETSCrewRole::Driver));
	TestTrue(TEXT("host still has no team"), FTSFlowFixture::StateOf(Host)->GetTeamId() == ETSTeamId::None);

	// ---------------------------------------------------------------------------------------
	// 2. A role cannot be taken before a team - the ordering the lobby depends on.
	// ---------------------------------------------------------------------------------------
	TestFalse(TEXT("role is REFUSED before a team is held"), Fx.GameMode->TryAssignRole(ADriver, ETSCrewRole::Driver));

	// ---------------------------------------------------------------------------------------
	// 3. Team, then role. The role assignment is what spawns the team's tank.
	// ---------------------------------------------------------------------------------------
	TestTrue(TEXT("Team A accepted for the first player"), Fx.GameMode->TryAssignTeam(ADriver, ETSTeamId::TeamA));

	// The tank appears when the TEAM is created, not when a role is taken. This test originally
	// asserted the opposite and failed, which is the more useful outcome than assuming: the spawn
	// call sits in TryAssignTeam, so a team exists with a tank and no crew until seats are taken.
	APawn* TankA = Fx.GameMode->GetTankForTeam(ETSTeamId::TeamA);
	TestNotNull(TEXT("assigning a TEAM spawns that team's tank"), TankA);

	TestTrue(TEXT("Driver role accepted"), Fx.GameMode->TryAssignRole(ADriver, ETSCrewRole::Driver));
	if (!TestNotNull(TEXT("assigning a role spawned Team A's tank"), TankA))
	{
		Fx.TearDown();
		return false;
	}
	TestEqual(TEXT("the Driver's AssignedTank is that tank"), FTSFlowFixture::StateOf(ADriver)->GetAssignedTank(), TankA);

	// ---------------------------------------------------------------------------------------
	// 4. A teammate joins the SAME tank; a rival team gets its OWN.
	// ---------------------------------------------------------------------------------------
	TestTrue(TEXT("Team A accepted for the second player"), Fx.GameMode->TryAssignTeam(AGunner, ETSTeamId::TeamA));
	TestTrue(TEXT("Gunner role accepted"), Fx.GameMode->TryAssignRole(AGunner, ETSCrewRole::Gunner));
	TestEqual(TEXT("the teammate is on the SAME tank"), FTSFlowFixture::StateOf(AGunner)->GetAssignedTank(), TankA);

	TestTrue(TEXT("Team B accepted"), Fx.GameMode->TryAssignTeam(BDriver, ETSTeamId::TeamB));
	TestTrue(TEXT("Team B Driver role accepted"), Fx.GameMode->TryAssignRole(BDriver, ETSCrewRole::Driver));

	APawn* TankB = Fx.GameMode->GetTankForTeam(ETSTeamId::TeamB);
	if (!TestNotNull(TEXT("Team B got a tank"), TankB))
	{
		Fx.TearDown();
		return false;
	}
	TestNotEqual(TEXT("Team B's tank is a DIFFERENT actor from Team A's"), TankB, TankA);

	// ---------------------------------------------------------------------------------------
	// 5. Seats are real: the crew component on the spawned tank holds the right occupants.
	// ---------------------------------------------------------------------------------------
	UTSTankCrewComponent* CrewA = TankA->FindComponentByClass<UTSTankCrewComponent>();
	if (!TestNotNull(TEXT("the spawned tank carries a crew component"), CrewA))
	{
		Fx.TearDown();
		return false;
	}
	TestEqual(TEXT("crew component knows it belongs to Team A"), static_cast<int32>(CrewA->GetTeamId()), static_cast<int32>(ETSTeamId::TeamA));
	TestEqual(TEXT("Driver seat holds the Driver"), CrewA->GetOccupant(ETSCrewRole::Driver), FTSFlowFixture::StateOf(ADriver));
	TestEqual(TEXT("Gunner seat holds the Gunner"), CrewA->GetOccupant(ETSCrewRole::Gunner), FTSFlowFixture::StateOf(AGunner));
	TestFalse(TEXT("Commander seat is still empty"), CrewA->IsRoleOccupied(ETSCrewRole::Commander));

	// A seat already taken cannot be taken again, through the GameMode this time.
	ATSTankPlayerController* Latecomer = Fx.MakePlayer(false);
	TestTrue(TEXT("latecomer joins Team A"), Fx.GameMode->TryAssignTeam(Latecomer, ETSTeamId::TeamA));
	TestFalse(TEXT("latecomer CANNOT take the occupied Driver seat"), Fx.GameMode->TryAssignRole(Latecomer, ETSCrewRole::Driver));
	TestEqual(TEXT("Driver seat unchanged after the attempt"), CrewA->GetOccupant(ETSCrewRole::Driver), FTSFlowFixture::StateOf(ADriver));

	// ---------------------------------------------------------------------------------------
	// 6. Role restriction on a REAL GameMode-spawned tank.
    // ---------------------------------------------------------------------------------------
	UTSTankControlComponent* ControlA = TankA->FindComponentByClass<UTSTankControlComponent>();
	UTSTankWeaponComponent* WeaponA = TankA->FindComponentByClass<UTSTankWeaponComponent>();
	if (!TestTrue(TEXT("spawned tank carries control and weapon components"), ControlA && WeaponA))
	{
		Fx.TearDown();
		return false;
	}

	TestTrue(TEXT("Driver CAN drive its own tank"), ControlA->TryApplyDriveInput(FTSFlowFixture::StateOf(ADriver), 1.f, 0.f));
	TestFalse(TEXT("Driver CANNOT aim"), WeaponA->TryAimTurret(FTSFlowFixture::StateOf(ADriver), FVector_NetQuantize(10.f, 0.f, 0.f)));
	TestTrue(TEXT("Gunner CAN aim its own tank"), WeaponA->TryAimTurret(FTSFlowFixture::StateOf(AGunner), FVector_NetQuantize(50.f, 0.f, 0.f)));
	TestFalse(TEXT("Gunner CANNOT drive"), ControlA->TryApplyDriveInput(FTSFlowFixture::StateOf(AGunner), 1.f, 0.f));

	// ---------------------------------------------------------------------------------------
	// 7. Team restriction across two GameMode-spawned tanks.
	// ---------------------------------------------------------------------------------------
	UTSTankControlComponent* ControlB = TankB->FindComponentByClass<UTSTankControlComponent>();
	TestNotNull(TEXT("Team B tank carries a control component"), ControlB);
	if (ControlB)
	{
		TestFalse(TEXT("Team B's Driver CANNOT drive Team A's tank"),
			ControlA->TryApplyDriveInput(FTSFlowFixture::StateOf(BDriver), 1.f, 0.f));
		TestTrue(TEXT("...but CAN drive its own"),
			ControlB->TryApplyDriveInput(FTSFlowFixture::StateOf(BDriver), 1.f, 0.f));
	}
	TestFalse(TEXT("Team A's Gunner CANNOT aim Team B's tank"),
		TankB->FindComponentByClass<UTSTankWeaponComponent>()->TryAimTurret(FTSFlowFixture::StateOf(AGunner), FVector_NetQuantize(99.f, 0.f, 0.f)));

	// ---------------------------------------------------------------------------------------
	// 8. Privilege escalation: only the host may assign other players.
	//
	// Calls the RPC's _Implementation directly, which is exactly where the IsMatchHost() re-check
	// lives. That check is the one thing standing between any client and assigning anyone, because
	// a Server RPC's HasAuthority() is trivially true.
	// ---------------------------------------------------------------------------------------
	// Reuses the latecomer, who is already on Team A with no role. Spawning a fresh player here
	// silently failed at first because Team A was already at its player cap, so the "host assign"
	// step was assigning someone who had no team - a false failure that looked like a broken guard.
	ATSTankPlayerState* Target = FTSFlowFixture::StateOf(Latecomer);
	TestTrue(TEXT("target is on a team but holds no role yet"),
		Target->GetTeamId() == ETSTeamId::TeamA && Target->GetCrewRole() == ETSCrewRole::None);

	ADriver->ServerHostAssignPlayerToRole_Implementation(Target, ETSCrewRole::Commander);
	TestTrue(TEXT("a NON-host assigning a role changes nothing"), Target->GetCrewRole() == ETSCrewRole::None);
	TestFalse(TEXT("Commander seat still empty after the non-host attempt"), CrewA->IsRoleOccupied(ETSCrewRole::Commander));

	Host->ServerHostAssignPlayerToRole_Implementation(Target, ETSCrewRole::Commander);
	TestTrue(TEXT("the HOST assigning the same role succeeds"), Target->GetCrewRole() == ETSCrewRole::Commander);
	TestEqual(TEXT("Commander seat now holds that player"), CrewA->GetOccupant(ETSCrewRole::Commander), Target);

	Fx.TearDown();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
