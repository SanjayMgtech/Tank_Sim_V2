#include "Core/TSGameMode.h"

#include "Core/TSGameState.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerStart.h"
#include "Tank_Sim_V2.h"
#include "Kismet/GameplayStatics.h"
#include "Player/TSHostCameraPawn.h"
#include "Player/TSTankPlayerController.h"
#include "Player/TSCrewPawn.h"
#include "Player/TSDesktopPawn.h"
#include "Player/TSTankPlayerState.h"
#include "Player/TSVRPawn.h"
#include "Tank/TSTankCrewComponent.h"
#include "UI/TSUISubsystem.h"

namespace
{
	const TArray<ETSTeamId> AllTeams = { ETSTeamId::TeamA, ETSTeamId::TeamB, ETSTeamId::TeamC, ETSTeamId::TeamD };

	FName SpawnTagForTeam(ETSTeamId TeamId)
	{
		switch (TeamId)
		{
		case ETSTeamId::TeamA: return FName(TEXT("TSTeamSpawn_TeamA"));
		case ETSTeamId::TeamB: return FName(TEXT("TSTeamSpawn_TeamB"));
		case ETSTeamId::TeamC: return FName(TEXT("TSTeamSpawn_TeamC"));
		case ETSTeamId::TeamD: return FName(TEXT("TSTeamSpawn_TeamD"));
		default: return NAME_None;
		}
	}

	// Optional level actor tag marking where the host's free-roam camera starts.
	const FName HostSpawnTag(TEXT("TSHostSpawn"));
}

ATSGameMode::ATSGameMode()
{
	// The flat screen is the default embodiment: a player who has never been given a play mode must
	// land there, not in a headset that merely happens to be plugged in.
	DefaultPawnClass = ATSDesktopPawn::StaticClass();
	PlayerControllerClass = ATSTankPlayerController::StaticClass();
	GameStateClass = ATSGameState::StaticClass();
	PlayerStateClass = ATSTankPlayerState::StaticClass();
	HostCameraPawnClass = ATSHostCameraPawn::StaticClass();

	// Carry PlayerControllers and PlayerStates across ServerTravel so the crews the host assigned in
	// the lobby survive the trip to the battle map. No transition map is configured, which is fine -
	// the engine spins up a blank one; set Project Settings > Maps & Modes > Transition Map if you
	// want a loading screen.
	//
	// The first hop (standalone menu -> hosted map) must NOT go through here: seamless travel ignores
	// ?listen, so the map would load standalone with no server at all. An earlier version of this
	// comment claimed that hop is "non-seamless whatever this says" - it is not; packaged builds proved
	// it. UTSSessionSubsystem::HandleCreateSessionComplete uses OpenLevel for that hop instead.
	bUseSeamlessTravel = true;

	// DefaultTankClass is deliberately left null here. It used to be filled by
	// ConstructorHelpers::FClassFinder on BP_T90_Controller_Chaos, which is the exact construct
	// CLAUDE.md RULE 2 forbids: an FClassFinder on a BLUEPRINT class in a constructor is a known boot
	// deadlock ("Compiling Blueprints" hang) and crashes packaged builds. It also hard-wired one tank
	// into C++, so a GameMode Blueprint that had never set the field looked configured when it was
	// not - and silently spawned a T90 for a match meant to use something else.
	//
	// Which tank a team drives is data. It belongs on the GameMode Blueprint's Class Defaults
	// (Default Tank Class, or Team Tank Classes for a per-team override), where a designer can change
	// it without a rebuild. GetTankClassForTeam logs an error if nothing is set.
}

void ATSGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	PendingLobbyCode = UGameplayStatics::ParseOption(Options, TEXT("LobbyCode"));
}

void ATSGameMode::InitGameState()
{
	Super::InitGameState();

	if (ATSGameState* TankGameState = GetGameState<ATSGameState>())
	{
		TankGameState->SetLobbyCode(PendingLobbyCode.IsEmpty() ? GenerateLobbyCode() : PendingLobbyCode);
	}
}

void ATSGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (bPreSpawnTeamTanks && CanSpawnTeamTanks())
	{
		SpawnTeamTanks();
	}
}

bool ATSGameMode::CanSpawnTeamTanks() const
{
	// UTSUISubsystem owns the menu-vs-gameplay map list (Config, DefaultGame.ini) and the menu-widget
	// sweep already reads it, so asking it here keeps one list rather than two that drift apart.
	const UGameInstance* GameInstance = GetGameInstance();
	const UTSUISubsystem* UI = GameInstance ? GameInstance->GetSubsystem<UTSUISubsystem>() : nullptr;
	return !UI || !UI->IsCurrentMapMenuMap();
}

int32 ATSGameMode::SpawnTeamTanks()
{
	// Deliberately no up-front DefaultTankClass check: GetTankClassForTeam is virtual, and
	// ATSTeamMatchGameMode resolves a team's class from its own TeamTankClasses map, which may be
	// populated when DefaultTankClass is not. Let each team's spawn attempt decide, then report.
	const int32 TeamCount = FMath::Clamp(NumTeamsToPreSpawn, 1, FMath::Min(MaxTeams, AllTeams.Num()));

	int32 SpawnedCount = 0;
	for (int32 Index = 0; Index < TeamCount; ++Index)
	{
		if (GetOrSpawnTankForTeam(AllTeams[Index]))
		{
			++SpawnedCount;
		}
	}

	if (SpawnedCount == 0)
	{
		const FString Message = TEXT("ATSGameMode: no team tanks could be spawned. Set GameMode > Class Defaults > Tank Simulation > Default Tank Class (or a per-team override) to a tank Blueprint that implements TSTankInterface.");
		UE_LOG(LogTankSim, Error, TEXT("%s"), *Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 15.f, FColor::Red, Message);
		}
	}
	else
	{
		UE_LOG(LogTankSim, Log, TEXT("ATSGameMode::SpawnTeamTanks - %d/%d team tanks present on map '%s'."),
			SpawnedCount, TeamCount, *GetWorld()->GetMapName());
	}

	return SpawnedCount;
}

TSubclassOf<APawn> ATSGameMode::GetTankClassForTeam(ETSTeamId TeamId) const
{
	if (const TSubclassOf<APawn>* Override = TeamTankClassOverrides.Find(TeamId))
	{
		if (*Override)
		{
			return *Override;
		}
	}
	return DefaultTankClass;
}

void ATSGameMode::HandleSeamlessTravelPlayer(AController*& C)
{
	Super::HandleSeamlessTravelPlayer(C);

	APlayerController* PC = Cast<APlayerController>(C);
	ATSTankPlayerState* PS = PC ? PC->GetPlayerState<ATSTankPlayerState>() : nullptr;
	if (!PS)
	{
		return;
	}

	if (ATSGameState* GS = GetGameState<ATSGameState>())
	{
		if (GS->GetMatchState() == ETSMatchState::WaitingForPlayers)
		{
			GS->SetMatchState(ETSMatchState::TeamAndRoleSelection);
		}
	}

	// Named SeatRole, not Role: AActor still declares a (deprecated) member called Role, and C4458
	// correctly flags shadowing it - the same reason TryAssignRole takes a RequestedRole.
	const ETSTeamId Team = PS->GetTeamId();
	const ETSCrewRole SeatRole = PS->GetCrewRole();
	if (Team == ETSTeamId::None || SeatRole == ETSCrewRole::None)
	{
		// Unassigned before travel, so nothing to restore - the host seats them on this map.
		return;
	}

	// CopyProperties deliberately left AssignedTank null; TryAssignRole spawns this team's tank on
	// the new map (or finds the one an earlier crewmate's arrival already spawned) and re-seats them.
	PS->SetAssignedTank(nullptr);

	if (TryAssignRole(PC, SeatRole))
	{
		UE_LOG(LogTankSim, Log, TEXT("Seamless travel: restored '%s' as %s on %s."),
			*PS->GetPlayerName(), *UTSTypeUtils::CrewRoleToString(SeatRole), *UTSTypeUtils::TeamIdToString(Team));
	}
	else
	{
		// Leave the team intact so the host can re-seat them from the console.
		PS->SetCrewRole(ETSCrewRole::None);
		UE_LOG(LogTankSim, Warning, TEXT("Seamless travel: could not restore '%s' as %s on %s - seat cleared."),
			*PS->GetPlayerName(), *UTSTypeUtils::CrewRoleToString(SeatRole), *UTSTypeUtils::TeamIdToString(Team));
	}
}

void ATSGameMode::PostLogin(APlayerController* NewPlayer)
{
	// Must happen before Super: AGameModeBase::PostLogin restarts the player, and
	// GetDefaultPawnClassForController reads bIsHost to decide between the free-roam camera and the
	// VR crew pawn. Designating the host afterwards would spawn it into the wrong pawn.
	if (ATSTankPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<ATSTankPlayerState>() : nullptr)
	{
		if (ShouldDesignateAsHost(NewPlayer))
		{
			PS->SetIsHost(true);
		}

		// One line per arrival: whether this world is actually a server, and who became host. A host
		// landing in a STANDALONE world (netmode 0) is the signature of the seamless-?listen bug.
		UE_LOG(LogTankSim, Log, TEXT("ATSGameMode::PostLogin '%s' netmode=%d local=%s -> host=%s"),
			*PS->GetPlayerName(), static_cast<int32>(GetNetMode()),
			NewPlayer->IsLocalController() ? TEXT("yes") : TEXT("no"), PS->IsHost() ? TEXT("YES") : TEXT("no"));
	}

	Super::PostLogin(NewPlayer);

	if (MaxLobbyPlayers > 0 && GameState && GameState->PlayerArray.Num() > MaxLobbyPlayers)
	{
		UE_LOG(LogTankSim, Warning, TEXT("ATSGameMode: lobby is full (%d/%d) - kicking '%s'."),
			GameState->PlayerArray.Num(), MaxLobbyPlayers, *GetNameSafe(NewPlayer));
		if (GameSession)
		{
			GameSession->KickPlayer(NewPlayer, FText::FromString(TEXT("Tank crew lobby is full.")));
		}
		return;
	}

	if (ATSGameState* GS = GetGameState<ATSGameState>())
	{
		if (GS->GetMatchState() == ETSMatchState::WaitingForPlayers)
		{
			GS->SetMatchState(ETSMatchState::TeamAndRoleSelection);
		}
	}
}

void ATSGameMode::Logout(AController* Exiting)
{
	if (ATSGameState* GS = GetGameState<ATSGameState>())
	{
		GS->ClearPlayerRole(Exiting ? Exiting->PlayerState : nullptr);
	}

	if (ATSTankPlayerState* PS = Exiting ? Exiting->GetPlayerState<ATSTankPlayerState>() : nullptr)
	{
		if (APawn* Tank = PS->GetAssignedTank())
		{
			if (UTSTankCrewComponent* Crew = Tank->FindComponentByClass<UTSTankCrewComponent>())
			{
				Crew->ReleaseRole(PS);
			}
		}
	}

	// The engine tears down the pawn a leaving controller POSSESSES; the parked one it has never
	// heard of, and it would sit hidden in the level for the rest of the match.
	if (ATSTankPlayerController* PC = Cast<ATSTankPlayerController>(Exiting))
	{
		PC->DestroyCrewPawns();
	}

	Super::Logout(Exiting);
}

bool ATSGameMode::HasDesignatedHost() const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const ATSTankPlayerState* PS = It->Get() ? It->Get()->GetPlayerState<ATSTankPlayerState>() : nullptr;
		if (PS && PS->IsHost())
		{
			return true;
		}
	}
	return false;
}

bool ATSGameMode::ShouldDesignateAsHost(const APlayerController* NewPlayer) const
{
	if (!NewPlayer || HasDesignatedHost())
	{
		return false;
	}

	switch (GetNetMode())
	{
	case NM_ListenServer:
		// The host is the machine that created the session, i.e. the one player controller local to
		// the authority.
		return NewPlayer->IsLocalController();

	case NM_DedicatedServer:
		// Nobody is local, so fall back to whoever connected first (opt-out via
		// bFirstPlayerHostsOnDedicatedServer for deployments that want a hostless server). Note this
		// also promotes a late joiner if the current host disconnects.
		return bFirstPlayerHostsOnDedicatedServer;

	default:
		// Standalone: there is no session and therefore no host. Designating the lone local player as
		// one would spawn them into the free camera with no way to play - exactly the wrong thing on
		// a plain single-player Play-In-Editor run.
		return false;
	}
}

bool ATSGameMode::IsHostController(const APlayerController* Player) const
{
	const ATSTankPlayerState* PS = Player ? Player->GetPlayerState<ATSTankPlayerState>() : nullptr;
	return PS && PS->IsHost();
}

UClass* ATSGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	const ATSTankPlayerState* PS = InController ? InController->GetPlayerState<ATSTankPlayerState>() : nullptr;
	if (PS && PS->IsHost() && HostCameraPawnClass)
	{
		return HostCameraPawnClass;
	}

	// Restart the player straight into the pawn their assigned mode calls for, so a player who chose
	// VR in the lobby never spends a frame in the desktop pawn. EnsureCrewPawnsFor then spawns the
	// other one behind them.
	if (PS)
	{
		if (const TSubclassOf<APawn> CrewPawnClass = GetCrewPawnClassForMode(PS->GetPlayMode()))
		{
			return CrewPawnClass;
		}
	}

	return Super::GetDefaultPawnClassForController_Implementation(InController);
}

TSubclassOf<APawn> ATSGameMode::GetCrewPawnClassForMode(ETSPlayMode Mode) const
{
	const TSubclassOf<APawn> Configured = (Mode == ETSPlayMode::VR) ? VRCrewPawnClass : DesktopCrewPawnClass;
	if (Configured)
	{
		return Configured;
	}

	// Unset. DefaultPawnClass, not the native crew pawn: the Enhanced Input assets are Blueprint
	// data, so a native pawn here would spawn a crew member who cannot press anything. One Blueprint
	// then serves both modes, and the pawn switches stereo from the assigned mode rather than from
	// which class it happens to be.
	return DefaultPawnClass;
}

void ATSGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);

	// After Super, not before: it is what restarts the player into their first pawn, and this adopts
	// that pawn rather than spawning a third alongside it.
	EnsureCrewPawnsFor(NewPlayer);
}

void ATSGameMode::EnsureCrewPawnsFor(APlayerController* Player)
{
	ATSTankPlayerController* PC = Cast<ATSTankPlayerController>(Player);
	ATSTankPlayerState* PS = PC ? PC->GetPlayerState<ATSTankPlayerState>() : nullptr;
	UWorld* World = GetWorld();
	if (!PC || !PS || !World)
	{
		return;
	}

	// The host is a match admin, not a participant: no team, no seat, no crew pawn of either kind.
	if (PS->IsHost())
	{
		return;
	}

	// Same rule the tank spawns follow: nobody is crewing anything on the main menu, and a second
	// pawn parked behind the menu would ride along through the travel to the gameplay map.
	if (!CanSpawnTeamTanks())
	{
		return;
	}

	const TSubclassOf<APawn> DesktopClass = GetCrewPawnClassForMode(ETSPlayMode::Desktop);
	const TSubclassOf<APawn> VRClass = GetCrewPawnClassForMode(ETSPlayMode::VR);
	const bool bSharedClass = (DesktopClass == VRClass);

	// Adopt the pawn RestartPlayer already gave us instead of spawning another one beside it. The
	// pawn answers which mode it serves, so a Blueprint of either class lands in the right slot.
	if (ATSCrewPawn* Current = Cast<ATSCrewPawn>(PC->GetPawn()))
	{
		PC->SetCrewPawnForMode(Current->GetSupportedPlayMode(), Current);

		if (bSharedClass)
		{
			// One Blueprint configured for both modes. Registering it in both slots is what keeps
			// this from spawning a redundant twin: there is one pawn, and switching mode re-applies
			// the display mode on it rather than swapping possession.
			PC->SetCrewPawnForMode(ETSPlayMode::Desktop, Current);
			PC->SetCrewPawnForMode(ETSPlayMode::VR, Current);
		}
	}

	// Spawn on top of the active pawn so a mid-match switch does not teleport the player. The parked
	// pawn is re-seated by its own possession anyway, but between spawn and first use it should not
	// be standing somewhere else in the level.
	const FTransform SpawnTransform = PC->GetPawn()
		? PC->GetPawn()->GetActorTransform()
		: FTransform(PC->GetControlRotation(), PC->GetSpawnLocation());

	for (const ETSPlayMode Mode : { ETSPlayMode::Desktop, ETSPlayMode::VR })
	{
		if (PC->GetCrewPawnForMode(Mode))
		{
			continue;
		}

		const TSubclassOf<APawn> PawnClass = GetCrewPawnClassForMode(Mode);
		if (!PawnClass)
		{
			UE_LOG(LogTankSim, Warning,
				TEXT("ATSGameMode: no crew pawn class for %s - '%s' cannot use that mode. Set the GameMode's ")
				TEXT("Desktop/VR Crew Pawn Class (or Default Pawn Class) to a crew pawn Blueprint."),
				*UTSTypeUtils::PlayModeToString(Mode), *PS->GetPlayerName());
			continue;
		}

		// A class that is not a crew pawn cannot be seated, aimed or driven, so refuse it here rather
		// than possess something that will silently do nothing.
		if (!PawnClass->IsChildOf(ATSCrewPawn::StaticClass()))
		{
			UE_LOG(LogTankSim, Warning,
				TEXT("ATSGameMode: crew pawn class '%s' for %s does not derive from ATSCrewPawn - ignored."),
				*PawnClass->GetName(), *UTSTypeUtils::PlayModeToString(Mode));
			continue;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = PC;
		// The seat this pawn will be attached to may already be occupied by its twin, and a player
		// start can be crowded at match start; neither is a reason to refuse to spawn.
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ATSCrewPawn* Spawned = World->SpawnActor<ATSCrewPawn>(PawnClass, SpawnTransform, SpawnParams);
		if (!Spawned)
		{
			continue;
		}

		// Parked until the player actually switches into it.
		Spawned->SetCrewPawnActive(false);
		PC->SetCrewPawnForMode(Mode, Spawned);

		UE_LOG(LogTankSim, Log, TEXT("ATSGameMode: spawned %s crew pawn '%s' for '%s'."),
			*UTSTypeUtils::PlayModeToString(Mode), *Spawned->GetName(), *PS->GetPlayerName());
	}

	ATSCrewPawn* Desired = PC->GetCrewPawnForMode(PS->GetPlayMode());
	if (!Desired)
	{
		return;
	}

	if (PC->GetPawn() == Desired)
	{
		// Already in the right pawn. Still re-apply the display mode: with one Blueprint serving both
		// modes this is the ONLY thing a mode change does, and without it "Play in VR" would record
		// the choice and change nothing on screen.
		Desired->SetCrewPawnActive(true);
		Desired->ApplyDisplayMode();
		return;
	}

	if (APawn* Previous = PC->GetPawn())
	{
		PC->UnPossess();

		// UnPossess fires NotifyControllerChanged on the old pawn, which detaches it from the crew
		// seat by itself - parking it here only hides it and stops it ticking.
		if (ATSCrewPawn* PreviousCrew = Cast<ATSCrewPawn>(Previous))
		{
			PreviousCrew->SetCrewPawnActive(false);

			// UnPossess clears the pawn's Owner, and an unowned hidden actor drops out of relevancy
			// for this connection - which would leave the client's own reference to its parked body
			// resolving to null. Re-owning it costs nothing and keeps both references honest.
			PreviousCrew->SetOwner(PC);
		}
	}

	// Take the outgoing pawn's transform so the switch happens where the player was standing, not
	// where this pawn was parked several minutes ago.
	Desired->SetActorTransform(SpawnTransform);
	Desired->SetCrewPawnActive(true);
	PC->Possess(Desired);

	UE_LOG(LogTankSim, Log, TEXT("ATSGameMode: '%s' is now in %s (pawn '%s')."),
		*PS->GetPlayerName(), *UTSTypeUtils::PlayModeToString(PS->GetPlayMode()), *Desired->GetName());
}

bool ATSGameMode::TrySetDriveControlMode(APlayerController* Player, ETSDriveControlMode NewMode)
{
	ATSTankPlayerController* PC = Cast<ATSTankPlayerController>(Player);
	ATSTankPlayerState* PS = PC ? PC->GetPlayerState<ATSTankPlayerState>() : nullptr;
	if (!PC || !PS)
	{
		return false;
	}

	// The host holds no crew pawn, so it has nothing to drive with either way.
	if (PS->IsHost())
	{
		UE_LOG(LogTankSim, Warning,
			TEXT("TrySetDriveControlMode: refused - '%s' is the match host and crews no tank."),
			*PS->GetPlayerName());
		return false;
	}

	// Manual means the levers ARE the input. Without VR hands there is nothing to work them with, and
	// switching a desktop player to Manual would silently take their stick away and give them
	// nothing - a dead control scheme that looks like broken input.
	if (NewMode == ETSDriveControlMode::Manual && PS->GetPlayMode() != ETSPlayMode::VR)
	{
		UE_LOG(LogTankSim, Warning,
			TEXT("TrySetDriveControlMode: refused Manual for '%s' - manual controls need VR hands, and ")
			TEXT("this player is in Desktop mode. Put them in VR first."),
			*PS->GetPlayerName());
		return false;
	}

	PS->SetDriveControlMode(NewMode);
	UE_LOG(LogTankSim, Log, TEXT("Drive control mode: '%s' -> %s"),
		*PS->GetPlayerName(), *UTSTypeUtils::DriveControlModeToString(NewMode));
	return true;
}

ETSPlayModeDenial ATSGameMode::GetPlayModeDenialReason(const APlayerController* Player, ETSPlayMode Mode) const
{
	const ATSTankPlayerState* PS = Player ? Player->GetPlayerState<ATSTankPlayerState>() : nullptr;
	if (!PS)
	{
		return ETSPlayModeDenial::HostCannotPlay;
	}

	// The host runs the match on a flat screen and possesses the free-roam camera, so a play mode
	// would have nothing to act on.
	if (PS->IsHost())
	{
		return ETSPlayModeDenial::HostCannotPlay;
	}

	// THE GUARD THIS WHOLE SECTION EXISTS FOR.
	//
	// A misclick on "Play in VR" with no headset attached used to be accepted: the server swapped the
	// player into ATSVRPawn, and the client then tried to bring stereo up against an XR runtime that
	// was initialised but headless. That took the GPU with it -
	// "GPU crash detected: Device 0 Removed: DXGI_ERROR_DEVICE_HUNG".
	//
	// So the request is refused HERE, before a pawn is spawned, possessed or even recorded. Nothing
	// about the player's state changes and the misclick costs them nothing.
	if (Mode == ETSPlayMode::VR && !PS->HasHeadsetConnected())
	{
		return ETSPlayModeDenial::NoHeadset;
	}

	const TSubclassOf<APawn> PawnClass = GetCrewPawnClassForMode(Mode);
	if (!PawnClass || !PawnClass->IsChildOf(ATSCrewPawn::StaticClass()))
	{
		return ETSPlayModeDenial::NoPawnClass;
	}

	return ETSPlayModeDenial::None;
}

bool ATSGameMode::TrySetPlayMode(APlayerController* Player, ETSPlayMode NewMode)
{
	ATSTankPlayerController* PC = Cast<ATSTankPlayerController>(Player);
	ATSTankPlayerState* PS = PC ? PC->GetPlayerState<ATSTankPlayerState>() : nullptr;
	if (!PC || !PS)
	{
		return false;
	}

	const ETSPlayModeDenial Denial = GetPlayModeDenialReason(PC, NewMode);
	if (Denial != ETSPlayModeDenial::None)
	{
		UE_LOG(LogTankSim, Warning, TEXT("ATSGameMode: refused %s for '%s' - %s."),
			*UTSTypeUtils::PlayModeToString(NewMode), *PS->GetPlayerName(),
			*UTSTypeUtils::PlayModeDenialToString(Denial));

		// Tell the asking client, so the UI can say why instead of appearing to ignore the click.
		PC->ClientPlayModeRequestResult(NewMode, false, Denial);
		return false;
	}

	PS->SetPlayMode(NewMode);

	// Manual controls ARE the VR hands (see TrySetDriveControlMode, which refuses Manual for a
	// desktop player). Leaving VR while set to Manual would strand the player with levers they can no
	// longer reach and no stick either, so the drive scheme comes back with them.
	if (NewMode != ETSPlayMode::VR && PS->GetDriveControlMode() == ETSDriveControlMode::Manual)
	{
		UE_LOG(LogTankSim, Log,
			TEXT("ATSGameMode: '%s' left VR while on manual controls - returning them to the analog stick."),
			*PS->GetPlayerName());
		PS->SetDriveControlMode(ETSDriveControlMode::Analog);
	}

	EnsureCrewPawnsFor(PC);
	PC->ClientPlayModeRequestResult(NewMode, true, ETSPlayModeDenial::None);
	return true;
}

AActor* ATSGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// Same convention as the per-team tank spawns: tag an actor in the level to place the host's
	// camera deliberately (e.g. overlooking the battlefield). Falls back to a normal PlayerStart.
	const ATSTankPlayerState* PS = Player ? Player->GetPlayerState<ATSTankPlayerState>() : nullptr;
	if (PS && PS->IsHost())
	{
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(HostSpawnTag))
			{
				return *It;
			}
		}
	}

	return Super::ChoosePlayerStart_Implementation(Player);
}

void ATSGameMode::StartTankMatch()
{
	// Off by default. Requiring every seat means a solo or two-player session can NEVER leave the
	// assignment phase - which is exactly the state that used to strand the host in cursor mode with
	// no way out. Turn it on for a production lobby that really does need full crews.
	if (bRequireFullCrewsToStart && !AreAllRolesFilled())
	{
		UE_LOG(LogTankSim, Warning, TEXT("StartTankMatch refused: bRequireFullCrewsToStart is set and not every active team is fully crewed."));
		return;
	}

	// On a menu map there is nothing to start yet - travel to the gameplay map, where this runs again.
	if (!CanSpawnTeamTanks())
	{
		if (GameplayMapName != NAME_None)
		{
			GetWorld()->ServerTravel(GameplayMapName.ToString(), true);
		}
		return;
	}

	if (ATSGameState* GS = GetGameState<ATSGameState>())
	{
		if (GS->GetMatchState() != ETSMatchState::InProgress)
		{
			GS->SetMatchState(ETSMatchState::InProgress);
			UE_LOG(LogTankSim, Log, TEXT("StartTankMatch: match state -> InProgress."));
		}
	}
}

void ATSGameMode::HandlePlayerReadyToSpawn(ATSTankPlayerController* PlayerController)
{
	if (!PlayerController || PlayerController->GetPawn())
	{
		return;
	}

	const ATSTankPlayerState* TankPS = PlayerController->GetPlayerState<ATSTankPlayerState>();
	if (!TankPS)
	{
		return;
	}

	const ETSTeamId TeamId = TankPS->GetTeamId();
	if (TeamId == ETSTeamId::None)
	{
		return;
	}

	APawn* Tank = GetOrSpawnTankForTeam(TeamId);
	if (Tank)
	{
		PlayerController->Possess(Tank);
	}
}

bool ATSGameMode::AreAllRolesFilled() const
{
	return AreAllActiveTeamsFullyCrewed();
}

FString ATSGameMode::GenerateLobbyCode() const
{
	FString Code;
	const TCHAR Alphabet[] = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Code.AppendChar(Alphabet[FMath::RandRange(0, UE_ARRAY_COUNT(Alphabet) - 2)]);
	}
	return Code;
}

int32 ATSGameMode::CountPlayersOnTeam(ETSTeamId TeamId) const
{
	int32 Count = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const ATSTankPlayerState* PS = It->Get() ? It->Get()->GetPlayerState<ATSTankPlayerState>() : nullptr)
		{
			// The host occupies no seat, so it must never count against a team's 3-player capacity.
			if (!PS->IsHost() && PS->GetTeamId() == TeamId)
			{
				++Count;
			}
		}
	}
	return Count;
}

bool ATSGameMode::IsTeamFull(ETSTeamId TeamId) const
{
	// Three crew seats per team (Driver/Gunner/Commander) - Section 1.
	return CountPlayersOnTeam(TeamId) >= 3;
}

bool ATSGameMode::TryAssignTeam(APlayerController* Player, ETSTeamId Team)
{
	ATSTankPlayerState* PS = Player ? Player->GetPlayerState<ATSTankPlayerState>() : nullptr;
	if (!PS || Team == ETSTeamId::None)
	{
		return false;
	}

	// The host runs the match, it does not play in it - it can never hold a team.
	if (PS->IsHost())
	{
		return false;
	}

	const int32 RequestedTeamIndex = AllTeams.IndexOfByKey(Team);
	if (RequestedTeamIndex == INDEX_NONE || RequestedTeamIndex >= MaxTeams)
	{
		return false;
	}

	if (IsTeamFull(Team))
	{
		return false;
	}

	// Leaving a previous team means leaving its crew seat too.
	if (APawn* PreviousTank = PS->GetAssignedTank())
	{
		if (UTSTankCrewComponent* Crew = PreviousTank->FindComponentByClass<UTSTankCrewComponent>())
		{
			Crew->ReleaseRole(PS);
		}
	}

	PS->SetTeamId(Team);
	PS->SetCrewRole(ETSCrewRole::None);
	PS->SetAssignedTank(nullptr);

	// Immediately spawn/assign this team's tank (GetTankClassForTeam picks the class)
	GetOrSpawnTankForTeam(Team);

	return true;
}

bool ATSGameMode::TryAssignRole(APlayerController* Player, ETSCrewRole RequestedRole)
{
	ATSTankPlayerState* PS = Player ? Player->GetPlayerState<ATSTankPlayerState>() : nullptr;
	if (!PS || PS->GetTeamId() == ETSTeamId::None || RequestedRole == ETSCrewRole::None)
	{
		return false;
	}

	// Belt and braces: a host has no team, so it cannot reach here - but never let it take a seat.
	if (PS->IsHost())
	{
		return false;
	}

	APawn* Tank = GetOrSpawnTankForTeam(PS->GetTeamId());
	UTSTankCrewComponent* Crew = Tank ? Tank->FindComponentByClass<UTSTankCrewComponent>() : nullptr;
	if (!Crew)
	{
		return false;
	}

	if (!Crew->TryOccupyRole(PS, RequestedRole))
	{
		return false;
	}

	PS->SetCrewRole(RequestedRole);
	PS->SetAssignedTank(Tank);

	if (ATSGameState* GS = GetGameState<ATSGameState>())
	{
		if (GS->GetMatchState() != ETSMatchState::InProgress && AreAllActiveTeamsFullyCrewed())
		{
			GS->SetMatchState(ETSMatchState::InProgress);
		}
	}

	return true;
}

void ATSGameMode::ClearAssignment(APlayerController* Player)
{
	ATSTankPlayerState* PS = Player ? Player->GetPlayerState<ATSTankPlayerState>() : nullptr;
	if (!PS)
	{
		return;
	}

	if (APawn* Tank = PS->GetAssignedTank())
	{
		if (UTSTankCrewComponent* Crew = Tank->FindComponentByClass<UTSTankCrewComponent>())
		{
			Crew->ReleaseRole(PS);
		}
	}

	PS->SetAssignedTank(nullptr);
	PS->SetCrewRole(ETSCrewRole::None);
	PS->SetTeamId(ETSTeamId::None);
}

APawn* ATSGameMode::GetTankForTeam(ETSTeamId Team) const
{
	const ATSGameState* GS = GetGameState<ATSGameState>();
	return GS ? GS->FindTankForTeam(Team) : nullptr;
}

bool ATSGameMode::AreAllActiveTeamsFullyCrewed() const
{
	const ATSGameState* GS = GetGameState<ATSGameState>();
	if (!GS)
	{
		return false;
	}

	TSet<ETSTeamId> ActiveTeams;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const ATSTankPlayerState* PS = It->Get() ? It->Get()->GetPlayerState<ATSTankPlayerState>() : nullptr)
		{
			if (!PS->IsHost() && PS->GetTeamId() != ETSTeamId::None)
			{
				ActiveTeams.Add(PS->GetTeamId());
			}
		}
	}

	if (ActiveTeams.Num() == 0)
	{
		return false;
	}

	for (ETSTeamId TeamId : ActiveTeams)
	{
		const APawn* Tank = GS->FindTankForTeam(TeamId);
		const UTSTankCrewComponent* Crew = Tank ? Tank->FindComponentByClass<UTSTankCrewComponent>() : nullptr;
		if (!Crew
			|| !Crew->IsRoleOccupied(ETSCrewRole::Driver)
			|| !Crew->IsRoleOccupied(ETSCrewRole::Gunner)
			|| !Crew->IsRoleOccupied(ETSCrewRole::Commander))
		{
			return false;
		}
	}

	return true;
}

FTransform ATSGameMode::GetSpawnTransformForTeam(ETSTeamId TeamId) const
{
	const FName Tag = SpawnTagForTeam(TeamId);
	if (Tag != NAME_None)
	{
		// Any actor carrying the tag wins - a PlayerStart, a TargetPoint or an empty Actor all work.
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(Tag))
			{
				return It->GetActorTransform();
			}
		}

		// A PlayerStart whose Player Start Tag matches is the same idea via the PlayerStart-specific
		// field, which is what people usually reach for first in the Details panel.
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			if (It->PlayerStartTag == Tag)
			{
				return It->GetActorTransform();
			}
		}
	}

	// Level had no spawn actor. Before dropping the tank at the world origin, use the transform
	// held as GameMode data - see FallbackTeamSpawnTransforms for why that is not just belt and
	// braces: the 167MB WarZone map cannot be committed, so its spawn actors do not reach a clone.
	if (const FTransform* Configured = FallbackTeamSpawnTransforms.Find(TeamId))
	{
		UE_LOG(LogTankSim, Log,
			TEXT("ATSGameMode: no actor tagged '%s' in the level - using the configured fallback transform for %s."),
			*Tag.ToString(), *UTSTypeUtils::TeamIdToString(TeamId));
		return *Configured;
	}

	const int32 TeamIndex = AllTeams.IndexOfByKey(TeamId);
	const float Offset = TeamIndex >= 0 ? static_cast<float>(TeamIndex) : 0.f;
	UE_LOG(LogTankSim, Warning, TEXT("ATSGameMode: no actor tagged '%s' in the level AND no FallbackTeamSpawnTransforms entry - dropping at a world-origin offset. Expect a bad landing on sloped ground."), *Tag.ToString());
	return FTransform(FVector(Offset * FallbackTeamSpawnSpacing, 0.f, 200.f));
}

APawn* ATSGameMode::GetOrSpawnTankForTeam(ETSTeamId TeamId)
{
	ATSGameState* GS = GetGameState<ATSGameState>();
	if (!GS)
	{
		return nullptr;
	}

	if (APawn* Existing = GS->FindTankForTeam(TeamId))
	{
		return Existing;
	}

	if (!CanSpawnTeamTanks())
	{
		UE_LOG(LogTankSim, Verbose, TEXT("ATSGameMode: refusing to spawn a tank for %s on menu map '%s'."),
			*UTSTypeUtils::TeamIdToString(TeamId), *GetWorld()->GetMapName());
		return nullptr;
	}

	const TSubclassOf<APawn> TankClass = GetTankClassForTeam(TeamId);
	if (!TankClass)
	{
		UE_LOG(LogTankSim, Error, TEXT("ATSGameMode: no tank class for %s - set Default Tank Class (or a Team Tank Class Overrides entry) in the GameMode defaults."),
			*UTSTypeUtils::TeamIdToString(TeamId));
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	const FTransform SpawnTransform = GetSpawnTransformForTeam(TeamId);
	APawn* NewTank = GetWorld()->SpawnActor<APawn>(TankClass, SpawnTransform, SpawnParams);
	if (!NewTank)
	{
		UE_LOG(LogTankSim, Error, TEXT("ATSGameMode: SpawnActor failed for %s using class '%s'."),
			*UTSTypeUtils::TeamIdToString(TeamId), *TankClass->GetName());
		return nullptr;
	}

	if (!NewTank->GetIsReplicated())
	{
		// Without this the tank exists on the server only and clients see an empty battlefield.
		UE_LOG(LogTankSim, Warning, TEXT("ATSGameMode: '%s' does not replicate - tick Replicates in the tank Blueprint's Class Defaults or clients will not see it."),
			*TankClass->GetName());
	}

	if (UTSTankCrewComponent* Crew = NewTank->FindComponentByClass<UTSTankCrewComponent>())
	{
		Crew->SetTeamId(TeamId);
	}
	else
	{
		UE_LOG(LogTankSim, Error, TEXT("ATSGameMode: spawned tank '%s' has no UTSTankCrewComponent - add one in the Blueprint's Components panel, or nobody can take a seat in it."),
			*NewTank->GetName());
	}

	GS->RegisterTeamTank(TeamId, NewTank);

	UE_LOG(LogTankSim, Log, TEXT("ATSGameMode: spawned '%s' for %s at %s."),
		*NewTank->GetName(), *UTSTypeUtils::TeamIdToString(TeamId), *SpawnTransform.GetLocation().ToCompactString());

	return NewTank;
}
