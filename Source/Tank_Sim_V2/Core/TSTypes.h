// Tank Simulation Framework - shared enums, structs and permission contracts.
#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TSTypes.generated.h"

class APawn;

UENUM(BlueprintType)
enum class ETSCrewRole : uint8
{
	None		UMETA(DisplayName = "None"),
	Driver		UMETA(DisplayName = "Driver"),
	Gunner		UMETA(DisplayName = "Gunner"),
	Commander	UMETA(DisplayName = "Commander")
};

UENUM(BlueprintType)
enum class ETSTeamId : uint8
{
	None	UMETA(DisplayName = "None"),
	TeamA	UMETA(DisplayName = "Team A"),
	TeamB	UMETA(DisplayName = "Team B"),
	TeamC	UMETA(DisplayName = "Team C"),
	TeamD	UMETA(DisplayName = "Team D")
};

// How a player is embodied in the match. Assigned by the host alongside team and crew role, and
// changeable mid-match by the player themselves - it decides WHICH crew pawn the PlayerController
// possesses (ATSDesktopPawn or ATSVRPawn), not merely whether stereo rendering is on.
//
// This is a deliberate assignment rather than pure headset auto-detection: a player with a headset
// plugged in may still want to sit at the keyboard, and a lobby needs to show who is where.
UENUM(BlueprintType)
enum class ETSPlayMode : uint8
{
	Desktop	UMETA(DisplayName = "Play in Desktop"),
	VR		UMETA(DisplayName = "Play in VR")
};

// How the Driver's input reaches the tank.
//
// These are mutually exclusive on purpose: the interior lever bones can be driven BY the input
// (Analog - the stick moves the tank and the levers follow) or they can BE the input (Manual - a VR
// hand pulls a lever and that produces the drive command). Both cannot own the pose at once, so this
// is a switch rather than a layer.
UENUM(BlueprintType)
enum class ETSDriveControlMode : uint8
{
	// Thumbstick or WSAD. The interior controls animate to follow it.
	Analog	UMETA(DisplayName = "Analog stick / keyboard"),

	// The VR driver physically works the levers and pedals; the controls produce the input.
	Manual	UMETA(DisplayName = "Manual controls (VR hands)")
};

// Why a play-mode request was refused. Returned by ATSGameMode::GetPlayModeDenialReason and sent
// back to the asking client, because "the VR button did nothing" is otherwise indistinguishable
// from a bug - and the honest answer is usually "you have no headset plugged in".
UENUM(BlueprintType)
enum class ETSPlayModeDenial : uint8
{
	None			UMETA(DisplayName = "Granted"),

	// The requesting client reported no connected HMD. Enabling stereo without one is what took the
	// GPU down (DXGI_ERROR_DEVICE_HUNG), so this is refused on the SERVER before any pawn is swapped.
	NoHeadset		UMETA(DisplayName = "No headset connected"),

	// The session host is a match admin on a flat screen and holds no crew pawn of either kind.
	HostCannotPlay	UMETA(DisplayName = "Host does not play"),

	// The GameMode has no crew pawn class configured for that mode.
	NoPawnClass		UMETA(DisplayName = "No crew pawn class configured")
};

UENUM(BlueprintType)
enum class ETSMatchState : uint8
{
	WaitingForPlayers		UMETA(DisplayName = "Waiting For Players"),
	TeamAndRoleSelection	UMETA(DisplayName = "Team And Role Selection"),
	InProgress				UMETA(DisplayName = "In Progress"),
	Ended					UMETA(DisplayName = "Ended")
};

UENUM(BlueprintType)
enum class ETSSessionStatus : uint8
{
	Idle		UMETA(DisplayName = "Idle"),
	Creating	UMETA(DisplayName = "Creating"),
	InLobby		UMETA(DisplayName = "In Lobby"),
	Searching	UMETA(DisplayName = "Searching"),
	Joining		UMETA(DisplayName = "Joining"),
	Failed		UMETA(DisplayName = "Failed")
};

// Capabilities gated by the Section 8 permission matrix.
UENUM(BlueprintType)
enum class ETSCapability : uint8
{
	Drive,
	MainCannon,
	MachineGun,
	TurretAim,
	RadarIntel,
	CrewCommands,
	Voice,
	TankStatus
};

// A capability can be fully denied, fully granted, or (Gunner/RadarIntel only) partially granted.
UENUM(BlueprintType)
enum class ETSAccessLevel : uint8
{
	Denied,
	Limited,
	Full
};

// Starter set of Commander crew commands. Extend as gameplay needs grow.
UENUM(BlueprintType)
enum class ETSCrewCommand : uint8
{
	None,
	Regroup,
	HoldPosition,
	Advance,
	Retreat
};

// Section 5 "Tank: replicated public tank state" - one entry per team, mirrored on GameState.
USTRUCT(BlueprintType)
struct FTSTeamTankEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation")
	ETSTeamId TeamId = ETSTeamId::None;

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation")
	TObjectPtr<APawn> AssignedTank = nullptr;
};

// Commander intel payload, replicated from UTSTankCommanderComponent and pushed to Blueprint via
// ITSTankInterface::BP_UpdateCommanderIntel.
USTRUCT(BlueprintType)
struct FTSCommanderIntel
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Commander")
	TArray<FVector_NetQuantize> KnownEnemyPositions;

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Commander")
	TArray<FVector_NetQuantize> TankPlacements;

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Commander")
	FString IntelSummary;
};

// Single source of truth for the Section 8 permission matrix. Every server-side validation path
// (Server RPC _Validate implementations and component-level re-checks) must route through this.
struct FTSPermissions
{
	static ETSAccessLevel GetAccessLevel(ETSCrewRole Role, ETSCapability Capability);
	static bool HasFullAccess(ETSCrewRole Role, ETSCapability Capability);
};

// Display-name helpers for the framework enums. Used by the role debug HUD (UTSRoleDebugWidget) and
// exposed to Blueprint so WBP HUDs can label a role/team without a hand-maintained Select node.
UCLASS()
class UTSTypeUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Utils")
	static FString CrewRoleToString(ETSCrewRole Role);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Utils")
	static FString TeamIdToString(ETSTeamId TeamId);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Utils")
	static FString MatchStateToString(ETSMatchState State);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Utils")
	static FString PlayModeToString(ETSPlayMode PlayMode);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Types")
	static FString DriveControlModeToString(ETSDriveControlMode Mode);

	UFUNCTION(BlueprintPure, Category = "Tank Simulation|Utils")
	static FString PlayModeDenialToString(ETSPlayModeDenial Denial);
};
