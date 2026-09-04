// Tank Simulation Framework - shared enums, structs and permission contracts.
#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
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
