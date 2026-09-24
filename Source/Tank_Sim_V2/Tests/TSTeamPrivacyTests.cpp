// Automation test for team privacy: which players' PlayerStates a connection is sent.
//
//     editor_query run_automation_tests  {"prefix": "TankSim.Net.TeamPrivacy"}
//
// Proves the rule (ATSTankPlayerState::CanPlayerSeePlayer). It does NOT prove the NetDriver honours
// it - that needs a listen server and a client on different teams, checking that the client's
// GameState->PlayerArray holds only itself, its teammates and the host.

#include "Misc/AutomationTest.h"

#include "Core/TSTypes.h"
#include "Player/TSTankPlayerState.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTSTeamPrivacyTest, "TankSim.Net.TeamPrivacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTSTeamPrivacyTest::RunTest(const FString& Parameters)
{
	using T = ETSTeamId;
	auto See = [](bool bSame, bool bViewerHost, T ViewerTeam, bool bTargetHost, T TargetTeam)
	{
		return ATSTankPlayerState::CanPlayerSeePlayer(bSame, bViewerHost, ViewerTeam, bTargetHost, TargetTeam);
	};

	TestTrue(TEXT("a teammate is visible"), See(false, false, T::TeamA, false, T::TeamA));
	TestFalse(TEXT("another team is hidden"), See(false, false, T::TeamA, false, T::TeamB));
	TestFalse(TEXT("an unassigned player is hidden from a team member"), See(false, false, T::TeamA, false, T::None));
	TestFalse(TEXT("a team member is hidden from an unassigned player"), See(false, false, T::None, false, T::TeamA));
	TestFalse(TEXT("two unassigned players are not teammates"), See(false, false, T::None, false, T::None));
	TestTrue(TEXT("an unassigned player still sees themselves"), See(true, false, T::None, false, T::None));
	TestTrue(TEXT("the host sees every team"), See(false, true, T::None, false, T::TeamC));
	TestTrue(TEXT("the host sees unassigned players"), See(false, true, T::None, false, T::None));
	TestTrue(TEXT("everyone sees the host"), See(false, false, T::TeamD, true, T::None));
	TestTrue(TEXT("an unassigned player sees the host"), See(false, false, T::None, true, T::None));
	return true;
}

#endif
