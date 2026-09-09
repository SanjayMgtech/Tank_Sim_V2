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

// What a crew station's periscope is rendering through. Purely a viewing filter: it changes the
// post processing on that station's SceneCaptureComponent2D and nothing about the world, so it is a
// LOCAL, per-viewer setting and is deliberately not replicated.
UENUM(BlueprintType)
enum class ETSVisionMode : uint8
{
	// The capture's authored settings, whatever the tank Blueprint ships.
	Normal			UMETA(DisplayName = "Day / Normal"),

	// Light-amplified: monochrome green, heavily over-exposed, bloomed and vignetted.
	NightVision		UMETA(DisplayName = "Night Vision"),

	// White-hot thermal: luminance only, crushed to high contrast.
	Thermal			UMETA(DisplayName = "Thermal")
};

// One blip on the Commander's radar. Server-built (see UTSTankCommanderComponent), so a client
// cannot manufacture contacts it was not told about.
USTRUCT(BlueprintType)
struct FTSRadarContact
{
	GENERATED_BODY()

	// Stable for the life of the contact's tank actor, which is what lets the radar widget
	// interpolate a blip between two intel updates instead of teleporting it. Array position is NOT
	// stable across refreshes, so it cannot be used for this.
	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Commander")
	int32 ContactId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Commander")
	FVector_NetQuantize Location = FVector::ZeroVector;

	// World yaw in degrees - which way the contact's hull is facing, so a blip can be drawn as a
	// pointed icon rather than a dot.
	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Commander")
	float Heading = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Commander")
	ETSTeamId TeamId = ETSTeamId::None;

	// True when the contact belongs to a team other than the viewer's.
	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Commander")
	bool bHostile = false;

	// True for the viewer's own tank, which the radar draws at the centre rather than as a contact.
	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Commander")
	bool bIsSelf = false;
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

	// The radar's actual data source. A superset of the two position arrays above, which are kept
	// because existing Blueprint graphs read them; new work should use this.
	UPROPERTY(BlueprintReadOnly, Category = "Tank Simulation|Commander")
	TArray<FTSRadarContact> Contacts;
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
