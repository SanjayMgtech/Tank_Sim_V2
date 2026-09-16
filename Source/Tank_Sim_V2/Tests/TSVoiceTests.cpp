// Automation tests for the voice channel model.
//
// Run everything with one call in the running editor (no PIE, no headset, no microphone):
//     editor_query run_automation_tests  {"prefix": "TankSim.Voice"}
//
// WHAT THESE PROVE
//   The audibility matrix and the seat policy, exhaustively. Both are pure static functions of
//   plain data - which is precisely why FTSVoiceParticipant exists as a flattened snapshot rather
//   than the rules walking live actors. A rule change can therefore be proven wrong in
//   milliseconds, before anybody plugs in a headset.
//
// WHAT THEY DO NOT PROVE, stated plainly so nobody reads a green run as "voice works":
//   * that any audio is carried. That needs the engine voice interface, a microphone, and two
//     machines. These tests never touch UNetConnection, the gameplay mute list, or an audio device.
//   * that ATSTankPlayerController reports transmit state correctly over a real connection. The
//     dead-man timeout and the Unreliable refresh only mean anything with a NetDriver and loss.
//   * that the widgets draw anything. They assert on the model the widgets read.
//
// The manual two-window listen-server test in CLAUDE.md is what covers the rest.

#include "Misc/AutomationTest.h"

#include "Core/TSTypes.h"
#include "Voice/TSVoiceRouterSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Distinct ids, because CanHear refuses a participant hearing themselves by identity.
	int32 NextParticipantId = 1;

	FTSVoiceParticipant MakeCrew(ETSTeamId Team, ETSCrewRole CrewRole, ETSVoiceChannel Channel)
	{
		FTSVoiceParticipant Participant;
		Participant.bIsHost = false;
		Participant.TeamId = Team;
		Participant.CrewRole = CrewRole;
		Participant.Channel = Channel;
		Participant.ParticipantId = NextParticipantId++;
		return Participant;
	}

	FTSVoiceParticipant MakeHost(const TArray<ETSTeamId>& Targets)
	{
		FTSVoiceParticipant Participant;
		Participant.bIsHost = true;
		Participant.Channel = ETSVoiceChannel::Command;
		Participant.CommandTargets = Targets;
		Participant.ParticipantId = NextParticipantId++;
		return Participant;
	}
}

// =============================================================================================
// 1. The audibility matrix.
//
// Written out as explicit expectations rather than derived from the implementation. A test that
// computes its expectation from the code under test proves only that the code equals itself.
// =============================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTSVoiceRoutingTest,
	"TankSim.Voice.Routing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTSVoiceRoutingTest::RunTest(const FString& Parameters)
{
	using ERouter = UTSVoiceRouterSubsystem;

	// --- Team A, commander on the intercom -----------------------------------------------------
	{
		const FTSVoiceParticipant Driver = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Driver, ETSVoiceChannel::Crew);
		const FTSVoiceParticipant Gunner = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Gunner, ETSVoiceChannel::Crew);
		const FTSVoiceParticipant Commander = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Commander, ETSVoiceChannel::Crew);

		TestTrue(TEXT("Driver hears Gunner on the intercom"), ERouter::CanHear(Driver, Gunner));
		TestTrue(TEXT("Gunner hears Driver on the intercom"), ERouter::CanHear(Gunner, Driver));
		TestTrue(TEXT("Driver hears a Commander who is on the intercom"), ERouter::CanHear(Driver, Commander));
		TestTrue(TEXT("Commander on the intercom hears the Driver"), ERouter::CanHear(Commander, Driver));
		TestTrue(TEXT("Commander on the intercom hears the Gunner"), ERouter::CanHear(Commander, Gunner));

		TestFalse(TEXT("Nobody hears their own microphone"), ERouter::CanHear(Driver, Driver));
	}

	// --- THE HEADLINE CASE: the Commander leaves the intercom for the host's net -----------------
	// "If the commander is talking to the host then the gunner and driver will be talking to each
	// other." This is the requirement that motivated the whole channel model, so it is asserted in
	// both directions and for both remaining crew members.
	{
		const FTSVoiceParticipant Driver = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Driver, ETSVoiceChannel::Crew);
		const FTSVoiceParticipant Gunner = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Gunner, ETSVoiceChannel::Crew);
		const FTSVoiceParticipant Commander = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Commander, ETSVoiceChannel::Command);

		TestTrue(TEXT("Driver and Gunner still hear each other while the Commander is on the host net"),
			ERouter::CanHear(Driver, Gunner) && ERouter::CanHear(Gunner, Driver));

		TestFalse(TEXT("Driver does not hear a Commander who is on the host net"), ERouter::CanHear(Driver, Commander));
		TestFalse(TEXT("Gunner does not hear a Commander who is on the host net"), ERouter::CanHear(Gunner, Commander));
		TestFalse(TEXT("Commander on the host net does not hear the Driver"), ERouter::CanHear(Commander, Driver));
		TestFalse(TEXT("Commander on the host net does not hear the Gunner"), ERouter::CanHear(Commander, Gunner));
	}

	// --- Teams are sealed off from one another --------------------------------------------------
	{
		const FTSVoiceParticipant DriverA = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Driver, ETSVoiceChannel::Crew);
		const FTSVoiceParticipant DriverB = MakeCrew(ETSTeamId::TeamB, ETSCrewRole::Driver, ETSVoiceChannel::Crew);
		const FTSVoiceParticipant CommanderA = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Commander, ETSVoiceChannel::Crew);
		const FTSVoiceParticipant CommanderB = MakeCrew(ETSTeamId::TeamB, ETSCrewRole::Commander, ETSVoiceChannel::Crew);

		TestFalse(TEXT("Crews on different teams never hear each other"), ERouter::CanHear(DriverA, DriverB));
		TestFalse(TEXT("Commanders on different intercoms never hear each other"), ERouter::CanHear(CommanderA, CommanderB));
	}

	// --- Commander -> host ----------------------------------------------------------------------
	{
		const FTSVoiceParticipant HostNoTargets = MakeHost({});
		const FTSVoiceParticipant CommanderOnCommand = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Commander, ETSVoiceChannel::Command);
		const FTSVoiceParticipant CommanderOnCrew = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Commander, ETSVoiceChannel::Crew);

		// Deliberately with an EMPTY selection: the host always hears a commander calling, because a
		// call the host silently is not receiving is indistinguishable from a broken microphone.
		TestTrue(TEXT("Host hears a Commander on the command net even with nobody selected"),
			ERouter::CanHear(HostNoTargets, CommanderOnCommand));

		TestFalse(TEXT("Host does not hear a Commander who is on their own intercom"),
			ERouter::CanHear(HostNoTargets, CommanderOnCrew));

		const FTSVoiceParticipant Driver = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Driver, ETSVoiceChannel::Crew);
		TestFalse(TEXT("Host never hears a Driver"), ERouter::CanHear(HostNoTargets, Driver));
	}

	// --- Host -> commander, gated on the selection ----------------------------------------------
	{
		const FTSVoiceParticipant Host = MakeHost({ ETSTeamId::TeamA, ETSTeamId::TeamC });

		const FTSVoiceParticipant CommanderA = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Commander, ETSVoiceChannel::Command);
		const FTSVoiceParticipant CommanderB = MakeCrew(ETSTeamId::TeamB, ETSCrewRole::Commander, ETSVoiceChannel::Command);
		const FTSVoiceParticipant CommanderC = MakeCrew(ETSTeamId::TeamC, ETSCrewRole::Commander, ETSVoiceChannel::Command);

		// Multi-select: several commanders at once, and only those.
		TestTrue(TEXT("Selected Commander A hears the host"), ERouter::CanHear(CommanderA, Host));
		TestTrue(TEXT("Selected Commander C hears the host"), ERouter::CanHear(CommanderC, Host));
		TestFalse(TEXT("Unselected Commander B does not hear the host"), ERouter::CanHear(CommanderB, Host));

		// The one that is easy to get wrong: a selected Commander sitting on the CREW channel still
		// hears the host. The channel selector governs where a Commander transmits and whether they
		// hear the intercom - it is not a way to tune the host out.
		const FTSVoiceParticipant CommanderAOnCrew = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Commander, ETSVoiceChannel::Crew);
		TestTrue(TEXT("A selected Commander hears the host even while sitting on the intercom"),
			ERouter::CanHear(CommanderAOnCrew, Host));

		// The host addresses commanders, never the rest of a crew.
		const FTSVoiceParticipant DriverA = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Driver, ETSVoiceChannel::Crew);
		const FTSVoiceParticipant GunnerA = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Gunner, ETSVoiceChannel::Crew);
		TestFalse(TEXT("A Driver on a selected team does not hear the host"), ERouter::CanHear(DriverA, Host));
		TestFalse(TEXT("A Gunner on a selected team does not hear the host"), ERouter::CanHear(GunnerA, Host));
	}

	// --- No commander-to-commander leak ---------------------------------------------------------
	// Two Commanders on the host's net are on OPPOSING teams. Letting them hear each other would be
	// a cross-team intel leak wearing a conference call as a disguise.
	{
		const FTSVoiceParticipant CommanderA = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Commander, ETSVoiceChannel::Command);
		const FTSVoiceParticipant CommanderB = MakeCrew(ETSTeamId::TeamB, ETSCrewRole::Commander, ETSVoiceChannel::Command);

		TestFalse(TEXT("Commanders on the command net do not hear each other (A to B)"), ERouter::CanHear(CommanderA, CommanderB));
		TestFalse(TEXT("Commanders on the command net do not hear each other (B to A)"), ERouter::CanHear(CommanderB, CommanderA));
	}

	// --- Unassigned players are inert -----------------------------------------------------------
	{
		const FTSVoiceParticipant Unassigned = MakeCrew(ETSTeamId::None, ETSCrewRole::None, ETSVoiceChannel::None);
		const FTSVoiceParticipant Driver = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Driver, ETSVoiceChannel::Crew);
		const FTSVoiceParticipant Host = MakeHost({ ETSTeamId::TeamA, ETSTeamId::TeamB, ETSTeamId::TeamC, ETSTeamId::TeamD });

		TestFalse(TEXT("An unassigned player hears nobody"), ERouter::CanHear(Unassigned, Driver));
		TestFalse(TEXT("Nobody hears an unassigned player"), ERouter::CanHear(Driver, Unassigned));
		TestFalse(TEXT("An unassigned player is not reached by a host addressing every team"),
			ERouter::CanHear(Unassigned, Host));
		TestFalse(TEXT("The host does not hear an unassigned player"), ERouter::CanHear(Host, Unassigned));
	}

	// --- A crew member with a team but no seat --------------------------------------------------
	// Reachable for the moment between TryAssignTeam and TryAssignRole. They must be silent, not
	// silently patched into the intercom of the team they are about to join.
	{
		const FTSVoiceParticipant Seatless = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::None, ETSVoiceChannel::Crew);
		const FTSVoiceParticipant Driver = MakeCrew(ETSTeamId::TeamA, ETSCrewRole::Driver, ETSVoiceChannel::Crew);

		TestFalse(TEXT("A player on a team but with no seat is not on its intercom (outbound)"), ERouter::CanHear(Driver, Seatless));
		TestFalse(TEXT("A player on a team but with no seat is not on its intercom (inbound)"), ERouter::CanHear(Seatless, Driver));
	}

	return true;
}

// =============================================================================================
// 2. Seat policy: which nets each seat may occupy.
//
// This is the check the server runs before honouring a channel request, so it is the thing
// standing between a hand-crafted RPC and a Driver on the command net.
// =============================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTSVoiceChannelPolicyTest,
	"TankSim.Voice.ChannelPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTSVoiceChannelPolicyTest::RunTest(const FString& Parameters)
{
	using ERouter = UTSVoiceRouterSubsystem;

	// --- Only the Commander chooses -------------------------------------------------------------
	TestTrue(TEXT("A Commander may choose their channel"), ERouter::CanChooseChannel(false, ETSCrewRole::Commander));
	TestFalse(TEXT("A Driver may not choose a channel"), ERouter::CanChooseChannel(false, ETSCrewRole::Driver));
	TestFalse(TEXT("A Gunner may not choose a channel"), ERouter::CanChooseChannel(false, ETSCrewRole::Gunner));
	TestFalse(TEXT("The host may not choose a channel - it only ever has one"), ERouter::CanChooseChannel(true, ETSCrewRole::None));

	// --- Resting channels -----------------------------------------------------------------------
	TestEqual(TEXT("A Driver rests on the intercom"),
		ERouter::GetDefaultChannelFor(false, ETSCrewRole::Driver), ETSVoiceChannel::Crew);
	TestEqual(TEXT("A Gunner rests on the intercom"),
		ERouter::GetDefaultChannelFor(false, ETSCrewRole::Gunner), ETSVoiceChannel::Crew);

	// A Commander joins their own crew first and chooses to leave it. Arriving already on the host's
	// net would mean their tank could not reach them from the moment they sat down.
	TestEqual(TEXT("A Commander rests on the intercom, not the command net"),
		ERouter::GetDefaultChannelFor(false, ETSCrewRole::Commander), ETSVoiceChannel::Crew);

	TestEqual(TEXT("The host rests on the command net"),
		ERouter::GetDefaultChannelFor(true, ETSCrewRole::None), ETSVoiceChannel::Command);
	TestEqual(TEXT("An unassigned player rests on no net"),
		ERouter::GetDefaultChannelFor(false, ETSCrewRole::None), ETSVoiceChannel::None);

	// --- What each seat is ALLOWED, which is what the server enforces ---------------------------
	TestTrue(TEXT("Commander may sit on the intercom"), ERouter::IsChannelAllowedFor(false, ETSCrewRole::Commander, ETSVoiceChannel::Crew));
	TestTrue(TEXT("Commander may sit on the command net"), ERouter::IsChannelAllowedFor(false, ETSCrewRole::Commander, ETSVoiceChannel::Command));

	// The security-relevant rows: a client asking for these must be refused, not merely discouraged
	// by a greyed-out button.
	TestFalse(TEXT("A Driver is refused the command net"), ERouter::IsChannelAllowedFor(false, ETSCrewRole::Driver, ETSVoiceChannel::Command));
	TestFalse(TEXT("A Gunner is refused the command net"), ERouter::IsChannelAllowedFor(false, ETSCrewRole::Gunner, ETSVoiceChannel::Command));
	TestFalse(TEXT("An unassigned player is refused the command net"), ERouter::IsChannelAllowedFor(false, ETSCrewRole::None, ETSVoiceChannel::Command));
	TestFalse(TEXT("An unassigned player is refused the intercom"), ERouter::IsChannelAllowedFor(false, ETSCrewRole::None, ETSVoiceChannel::Crew));

	// The host holds no seat, so there is no intercom for it to join - not even its own team's,
	// because it does not have one.
	TestFalse(TEXT("The host is refused an intercom"), ERouter::IsChannelAllowedFor(true, ETSCrewRole::None, ETSVoiceChannel::Crew));
	TestTrue(TEXT("The host is allowed the command net"), ERouter::IsChannelAllowedFor(true, ETSCrewRole::None, ETSVoiceChannel::Command));

	// Every seat's resting channel must be one that seat is allowed to hold, or EnforceChannelPolicy
	// would fight itself: it snaps a disallowed channel to the default, and would then find the
	// default disallowed and snap it again, every maintenance tick.
	const TArray<ETSCrewRole> AllRoles = { ETSCrewRole::None, ETSCrewRole::Driver, ETSCrewRole::Gunner, ETSCrewRole::Commander };
	for (const ETSCrewRole CrewRole : AllRoles)
	{
		for (const bool bIsHost : { false, true })
		{
			const ETSVoiceChannel Default = ERouter::GetDefaultChannelFor(bIsHost, CrewRole);
			TestTrue(FString::Printf(TEXT("The resting channel is always an allowed one (host=%d seat=%s)"),
				bIsHost ? 1 : 0, *UTSTypeUtils::CrewRoleToString(CrewRole)),
				ERouter::IsChannelAllowedFor(bIsHost, CrewRole, Default));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
