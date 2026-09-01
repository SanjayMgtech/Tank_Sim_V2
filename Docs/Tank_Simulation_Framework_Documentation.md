# Tank Simulation — Multiplayer VR Framework

Unreal Engine C++ architecture, Blueprint integration, networking, UI, VR and voice

> **Goal:** establish a stable framework so three developers can work in parallel. C++ owns contracts, authority, networking and permissions; the existing Blueprint tank remains responsible for tank-specific animation, weapon and movement execution.

## 1. System Scope

A multiplayer VR tank simulation where a session can contain multiple teams. Each team owns one tank. Each tank has three crew roles: Driver, Gunner and Commander. All three players operate the same tank actor through role-specific permissions.

- **Driver:** drives the tank.
- **Gunner:** aims and fires the main cannon and machine gun.
- **Commander:** accesses radar/intel/tank placements/tank information and issues supported commands.
- All three crew members can communicate by voice.
- Host creates the session; clients join; players are assigned to teams and roles.
- Existing Blueprint tank logic is reused rather than duplicated.

## 2. Architecture Rules

- **Server authoritative:** server owns team, role, tank assignment, damage, weapon authorization and important gameplay state.
- **Client responsive:** VR input and UI feel immediate, but gameplay requests are validated on the server.
- **One tank actor:** do not create separate Driver/Gunner/Commander tank classes.
- **Components/interfaces:** isolate crew, controls, weapons and commander systems.
- **Blueprint integration:** C++ decides who is allowed to act; Blueprint decides how the existing tank executes the action.
- **Data-driven:** use Data Assets for tank, role and input definitions where practical.

## 3. Runtime Architecture

```
Client
├─ PlayerController / VR Pawn
├─ UMG menus + role HUD
└─ Voice
   │ Server RPCs
   ▼
GameMode (server)
├─ Session/match flow
├─ Team assignment
├─ Role assignment
└─ Tank spawning/assignment

GameState (replicated)
├─ Match state
└─ Public team/tank state

PlayerState (replicated)
├─ TeamId
├─ CrewRole
└─ AssignedTank

Tank Actor (replicated)
├─ Crew Component
├─ Control Component
├─ Weapon Component
└─ Commander Component

Existing Tank Blueprint
├─ Movement
├─ Animations
├─ Turret/barrel
├─ Missile
├─ Main cannon
└─ Machine gun
```

## 4. C++ Class Framework

| Class | Responsibility |
|---|---|
| `ATSGameMode` | Server-only session flow, team/role validation, tank assignment/spawning. |
| `ATSGameState` | Replicated match and public team state. |
| `ATSTankPlayerState` | Replicated player TeamId, CrewRole and AssignedTank. |
| `ATSTankPlayerController` | Player requests, role input routing, UI ownership. |
| `ATSVRPawn` | HMD/controllers, local VR interaction and Enhanced Input. |
| `ATSTank` | Common tank actor contract / Blueprint parent. |
| `UTSTankCrewComponent` | Driver/Gunner/Commander occupancy and access checks. |
| `UTSTankControlComponent` | Validated control requests forwarded to tank. |
| `UTSTankWeaponComponent` | Aim/fire/reload request contract. |
| `UTSTankCommanderComponent` | Radar/intel/command contract. |
| `UTSSessionSubsystem` | Create/find/join/destroy session wrapper. |
| `UTSVoiceSubsystem` | Voice abstraction for crew/team communication. |
| `UTSRoleDefinition` / `UTSTankDefinition` | Data Assets for roles, permissions and tank configuration. |
| `UTS*Widget` classes | C++ UMG bases and BlueprintAssignable delegates. |

## 5. Core State

```cpp
enum class ETSCrewRole : uint8
{
    None, Driver, Gunner, Commander
};

enum class ETSTeamId : uint8
{
    None, TeamA, TeamB, TeamC, TeamD
};
```

**PlayerState:**
- TeamId
- CrewRole
- AssignedTank

**Tank:**
- TeamId
- CrewState
- replicated public tank state

## 6. Session Lifecycle

1. Host calls `UTSSessionSubsystem::CreateSession`.
2. Clients find and join the session.
3. Server creates/initializes PlayerState.
4. GameMode validates team selection.
5. GameMode validates role selection and locks the role slot.
6. GameMode spawns or assigns one tank to each team.
7. PlayerState replicates TeamId, CrewRole and AssignedTank.
8. PlayerController routes role-specific input to the assigned tank.
9. On disconnect, the server releases the crew role.

## 7. Team/Tank Ownership

> A tank belongs to a team, not to an individual player. Use a server-owned TeamId → Tank mapping. Every gameplay request follows: PlayerState → Team → AssignedTank → CrewRole → permission → tank component → Blueprint event.

```
Team A
└─ Tank A
   ├─ Driver
   ├─ Gunner
   └─ Commander

Team B
└─ Tank B
   ├─ Driver
   ├─ Gunner
   └─ Commander
```

## 8. Permission Matrix

| Capability | Driver | Gunner | Commander |
|---|---|---|---|
| Drive | YES | NO | NO |
| Main cannon | NO | YES | NO |
| Machine gun | NO | YES | NO |
| Turret aim | NO | YES | NO |
| Radar/intel | NO | LIMITED | YES |
| Crew commands | NO | NO | YES |
| Voice | YES | YES | YES |
| Tank status | YES | YES | YES |

## 9. Blueprint Integration

The existing tank Blueprint should derive from `ATSTank` (recommended) or implement the tank interface. C++ exposes stable `BlueprintNativeEvent` hooks; the Blueprint performs the actual tank-specific action.

```cpp
UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
void BP_SetDriveInput(float Throttle, float Steering);

UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
void BP_AimTurret(FVector_NetQuantize AimDirection);

UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
void BP_FireMainCannon();

UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
void BP_FireMachineGun();

UFUNCTION(BlueprintNativeEvent, BlueprintCallable)
void BP_UpdateCommanderIntel(const FTSCommanderIntel& Intel);
```

## 10. Networking Rules

- Client sends a role-specific Server RPC.
- Server validates PlayerState, team, role, assigned tank and capability.
- Server changes authoritative state or invokes the tank component.
- Replicate durable state; use multicast sparingly for transient effects.
- Never trust client-supplied TeamId, Role, TankId or authorization.
- Use Unreal replicated movement for tank movement where appropriate.
- Weapon firing must be authority validated and replicated.

## 11. UI Contract

| Widget | Purpose | Events |
|---|---|---|
| `UTSLoginWidget` | Login/main flow | OnLogin, OnContinue |
| `UTSSessionBrowserWidget` | Host/find/join | OnCreateSession, OnRefresh, OnJoin |
| `UTSTeamSelectionWidget` | Team selection | OnTeamSelected |
| `UTSRoleSelectionWidget` | Role selection | OnRoleSelected |
| `UTSCrewHUDWidget` | Common tank HUD | Crew/tank status |
| `UTSDriverHUDWidget` | Driver HUD | Speed/status |
| `UTSGunnerHUDWidget` | Gunner HUD | Weapon/ammo/aim |
| `UTSCommanderHUDWidget` | Commander HUD | Radar/intel/commands |

## 12. VR

Use Enhanced Input and Unreal's OpenXR-compatible VR path. `ATSVRPawn` captures local HMD/controller input. Input is converted into gameplay requests; the VR pawn must not contain authoritative role permissions.

- Shared actions: Interact, Grab, Primary, Secondary, Menu, Recenter.
- Role-specific Input Mapping Contexts can be enabled after role assignment.
- Driver/Gunner/Commander may use different VR layouts.
- Keep comfort settings and recentering local to the VR layer.

## 13. Voice

Wrap Unreal's selected online voice solution behind `UTSVoiceSubsystem` so gameplay code is not tied to one provider. First milestone: crew voice channel. Later: push-to-talk, radio channels, range voice and commander broadcast.

## 14. Repository Structure

```
Source/TankSimulation/
├─ Core/         GameInstance, GameMode, GameState, Types
├─ Networking/   SessionSubsystem
├─ Player/       PlayerController, PlayerState, VRPawn
├─ Tank/         Tank + Crew/Control/Weapon/Commander components
├─ UI/           Login/Session/Team/Role/HUD widgets
├─ Voice/        VoiceSubsystem
└─ Data/         Tank/Role/Input Data Assets
```

## 15. Team Workflow

- Developer 1 owns multiplayer/session/team/role authority.
- Developer 2 owns tank gameplay/components and Blueprint integration.
- Developer 3 owns VR/UI/voice.
- Shared enums, structs and API contracts are frozen before parallel implementation.
- Use feature branches and merge through an integration branch.
- Every new RPC documents caller, validation, reliability and result.
- Every replicated property documents why it replicates.

## 16. Framework Definition of Done

1. Host creates a session and clients join.
2. Teams and roles are server-authoritative.
3. Each team has one tank.
4. Three clients can occupy Driver/Gunner/Commander on the same tank.
5. Each role can perform only its allowed actions.
6. Existing Blueprint tank executes authorized commands.
7. Crew voice works.
8. VR input reaches the correct command path.
9. C++ widgets expose bindable events without owning gameplay authority.

## 17. Master Claude Prompt

Paste this prompt into Claude along with the Unreal project/code/Blueprint information. Ask Claude to implement incrementally rather than dumping an entire unverified project in one response.

```
You are the senior Unreal Engine multiplayer C++ architect for a VR tank simulation.

Requirements:
- Multiplayer VR.
- A session contains multiple teams.
- One tank per team.
- Three crew roles per tank: Driver, Gunner, Commander.
- Driver drives.
- Gunner aims/fires main cannon and machine gun.
- Commander accesses radar/intel/tank placements/tank information and issues commands.
- All three players operate the same replicated tank actor.
- All three crew members can communicate by voice.
- Existing Blueprint tank contains movement, animations, missile, cannon, machine gun and firing logic. Reuse it.
- C++ provides architecture, networking, permissions, role/team/session state, stable APIs and widget bases.
- Blueprint performs tank-specific execution.
- Use server-authoritative validation.
- Use Enhanced Input and OpenXR-compatible VR.

First explain the architecture. Then generate:
1. File/module tree.
2. Shared enums/structs/interfaces.
3. GameMode/GameState/PlayerState/PlayerController/VRPawn.
4. Session subsystem.
5. Team/role/tank assignment.
6. Tank crew/control/weapon/commander components.
7. Secure Server RPCs.
8. C++ UMG base widgets and delegates.
9. Voice abstraction.
10. Enhanced Input/VR routing.
11. Exact Blueprint integration steps for the existing tank.
12. Multiplayer test plan.
13. Three-developer task breakdown.

Rules:
- Do not invent Unreal APIs.
- If an API depends on engine version, say so.
- Produce compilable C++ where project-specific details are known.
- Clearly mark placeholders.
- Server decides WHO is allowed; Blueprint decides HOW the tank acts.
- Avoid monolithic classes.
- Explain ownership and replication for every RPC.
- Keep shared contracts stable for three parallel developers.
- Implement incrementally and identify exactly which files change at each step.
```
