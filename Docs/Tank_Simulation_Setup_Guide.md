# Tank Simulation Framework — Setup Guide

This document covers the C++ framework implemented under `Source/Tank_Sim_V2/` from
[Tank_Simulation_Framework_Documentation.md](Tank_Simulation_Framework_Documentation.md). It explains
what was built, what still needs doing in the Editor (Blueprints, input assets, data assets — things
that cannot be authored as text files), and how to verify each piece.

Engine: **UE 5.7**. Module: **Tank_Sim_V2** (the project's existing primary game module — the
framework was added to it directly rather than as a separate `TankSimulation` module).

## 1. What was implemented

| Doc class | File | Notes |
|---|---|---|
| Shared enums/structs/permissions | [TSTypes.h](../Source/Tank_Sim_V2/Core/TSTypes.h) | `ETSCrewRole`, `ETSTeamId`, `ETSMatchState`, `ETSCapability`, `ETSAccessLevel`, `ETSCrewCommand`, `FTSTeamTankEntry`, `FTSCommanderIntel`, `FTSPermissions` |
| Tank Blueprint contract | [TSTankInterface.h](../Source/Tank_Sim_V2/Tank/TSTankInterface.h) | `ITSTankInterface` — the 5 `BP_*` hooks from Section 9 |
| `ATSGameMode` | [TSGameMode.h](../Source/Tank_Sim_V2/Core/TSGameMode.h) | team/role validation, tank spawn/assign |
| `ATSGameState` | [TSGameState.h](../Source/Tank_Sim_V2/Core/TSGameState.h) | match state, public team→tank map |
| `UTSGameInstance` | [TSGameInstance.h](../Source/Tank_Sim_V2/Core/TSGameInstance.h) | convenience accessor to the session subsystem |
| `ATSTankPlayerState` | [TSTankPlayerState.h](../Source/Tank_Sim_V2/Player/TSTankPlayerState.h) | TeamId / CrewRole / AssignedTank |
| `ATSTankPlayerController` | [TSTankPlayerController.h](../Source/Tank_Sim_V2/Player/TSTankPlayerController.h) | every Server RPC (table below) |
| `ATSVRPawn` | [TSVRPawn.h](../Source/Tank_Sim_V2/Player/TSVRPawn.h) | HMD/motion controllers, Enhanced Input routing |
| `ATSTank` | [TSTank.h](../Source/Tank_Sim_V2/Tank/TSTank.h) | Path A convenience base (see §4) |
| `UTSTankCrewComponent` | [TSTankCrewComponent.h](../Source/Tank_Sim_V2/Tank/TSTankCrewComponent.h) | TeamId + seat occupancy/access checks |
| `UTSTankControlComponent` | [TSTankControlComponent.h](../Source/Tank_Sim_V2/Tank/TSTankControlComponent.h) | drive request validation |
| `UTSTankWeaponComponent` | [TSTankWeaponComponent.h](../Source/Tank_Sim_V2/Tank/TSTankWeaponComponent.h) | aim/fire/reload + ammo state |
| `UTSTankCommanderComponent` | [TSTankCommanderComponent.h](../Source/Tank_Sim_V2/Tank/TSTankCommanderComponent.h) | intel + crew commands |
| `UTSSessionSubsystem` | [TSSessionSubsystem.h](../Source/Tank_Sim_V2/Networking/TSSessionSubsystem.h) | create/find/join/destroy session |
| `UTSVoiceSubsystem` | [TSVoiceSubsystem.h](../Source/Tank_Sim_V2/Voice/TSVoiceSubsystem.h) | `IVoiceChat` wrapper |
| `UTSRoleDefinition` / `UTSTankDefinition` | [TSRoleDefinition.h](../Source/Tank_Sim_V2/Data/TSRoleDefinition.h) / [TSTankDefinition.h](../Source/Tank_Sim_V2/Data/TSTankDefinition.h) | Data Asset classes |
| 8 UMG widget base classes | `Source/Tank_Sim_V2/UI/` | see §5 |

Plus a small internal helper not in the doc's class table: `UTSHUDWidgetBase`
([TSHUDWidgetBase.h](../Source/Tank_Sim_V2/UI/TSHUDWidgetBase.h)), which the four HUD widgets share to
avoid duplicating "find my tank, rebind on reassignment" boilerplate.

Module/config changes:

- [Tank_Sim_V2.Build.cs](../Source/Tank_Sim_V2/Tank_Sim_V2.Build.cs) — added `Slate`, `SlateCore`,
  `UMG`, `OnlineSubsystem`, `OnlineSubsystemUtils`, `HeadMountedDisplay` as link dependencies, and
  `VoiceChat` as an include-path-only dependency (it's a header-only interface module — see §8).
- [Tank_Sim_V2.uproject](../Tank_Sim_V2.uproject) — enabled the `EnhancedInput`, `OpenXR`, and
  `OnlineSubsystemNull` plugins.
- [DefaultEngine.ini](../Config/DefaultEngine.ini) — added `[OnlineSubsystem] DefaultPlatformService=Null`
  and `[OnlineSubsystemNull] bEnabled=True`, additively (the existing Steam net driver config was left
  untouched).

## 2. Build status

Confirmed compiling and linking clean (`Result: Succeeded`) against the installed UE 5.7 toolchain via:

```
Engine\Build\BatchFiles\Build.bat Tank_Sim_V2Editor Win64 Development -project="<path to>\Tank_Sim_V2.uproject"
```

Two things fixed along the way that are worth knowing about if you add more files under `Source/Tank_Sim_V2/<Subfolder>/`:

- **Self-includes needed an explicit include path.** `#include "Core/TSTypes.h"` from a file already
  inside `Core/` failed to resolve until `Tank_Sim_V2.Build.cs` added
  `PublicIncludePaths.Add(ModuleDirectory);` explicitly — this project's default module setup didn't
  auto-add its own root the way the doc's Section 14 folder layout assumes.
- **`VoiceChat` is a header-only, `ClientOnly` plugin module** (zero `.cpp` files) — referencing it via
  `PrivateDependencyModuleNames` makes UBT try to build it as a real linkable binary and fail
  (`'VoiceChat' is not a C++ module`), and enabling it as a `.uproject` plugin fails the same way for
  the same reason. It's referenced via `PrivateIncludePathModuleNames` instead (matching Epic's own
  `EOSVoiceChat.Build.cs`), and is *not* listed in the `.uproject` Plugins array — nothing needs to
  "enable" a header-only module.

## 3. Adversarial review pass

After the initial implementation compiled clean, it went through a 4-lens adversarial review
(security/authority, replication correctness, Unreal object-lifetime, and fidelity to the design doc),
with every finding independently re-checked by up to 3 skeptic passes against the actual source before
being trusted. Six were confirmed and fixed; two were investigated and refuted (kept as-is, noted
below). The build was re-verified clean after every fix.

**Fixed:**

- **Commander intel was leaking to Driver/Gunner.** `UTSTankCommanderComponent::OnRep_Intel` pushed the
  raw, unfiltered `FTSCommanderIntel` to every crew member's Blueprint hook — `GetIntelFor`'s Section 8
  filtering was only ever applied to the Commander HUD widget's own display, never to what actually
  replicated out. Fixed: `OnRep_Intel` now resolves the local viewer's real crew role and pushes
  `GetIntelFor(LocalRole)` instead of the raw struct. The underlying property still replicates to all
  crew (see the Known Limitations note below for why), but no legitimate Blueprint UI can render
  Commander-only data to the wrong role anymore.
- **Fire/aim/drive cosmetic effects never reached remote clients.** `BP_FireMainCannon`,
  `BP_FireMachineGun`, `BP_AimTurret`, and `BP_SetDriveInput` were invoked via plain function dispatch
  from inside server-only RPC code — with no multicast and no `OnRep`, only the server/host process
  ever ran them. Fixed: fire events now go through `UFUNCTION(NetMulticast, Unreliable)` (a one-shot
  cosmetic event, matching the doc's own Section 10 line "use multicast sparingly for transient
  effects"); turret aim and drive input now replicate (`OnRep_AimDirection` / `OnRep_DriveInput`) since
  they're continuous state, not discrete events — the idiomatically correct choice for something that
  changes at VR-input rate, throttled by `ATSTank`'s existing `NetUpdateFrequency`.
- **Repeated identical crew commands were silently dropped.** `LastIssuedCommand` used default
  change-detection, so a Commander re-issuing "Regroup" twice in a row never fired `OnRep` on remote
  clients the second time. Fixed: `DOREPLIFETIME_CONDITION_NOTIFY(..., REPNOTIFY_Always)`.
- **Match state flipped to `InProgress` after the very first seat filled anywhere**, not once every
  active team was actually crewed. Fixed: added `ATSGameMode::AreAllActiveTeamsFullyCrewed()`, which
  the state transition now waits on.
- **`MaxTeams` was a dead config knob** — declared, never read. Fixed: `RequestTeamAssignment` now
  rejects a requested team whose index is `>= MaxTeams`.
- **Redundant `OnAssignmentChanged`/`OnCrewChanged` broadcasts** when a setter was called with a value
  equal to the current one (e.g. `SetCrewRole(None)` right after spawn). Fixed with early-return guards
  in `ATSTankPlayerState`'s setters and `UTSTankCrewComponent::TryOccupyRole`.

**Investigated and refuted** (verifiers actively tried to reproduce these and could not, given the
actual call chains in this codebase — left as-is):

- A hypothesized double-`AddDynamic` binding on `ATSVRPawn::PossessedBy` if a pawn were ever possessed
  twice. Nothing in this framework re-possesses a pawn — team/role changes are pure `PlayerState`
  mutations, never `Possess()`/`UnPossess()` calls.
- A hypothesized voice-channel authorization gap (`JoinCrewChannel` takes a free-form channel name with
  no server-side team check). Currently dead code: with no `IVoiceChat` backend enabled,
  `IsVoiceChatAvailable()` is permanently false and the function is an unconditional no-op.

**Noted but not changed:** two lower-severity HUD-widget delegate-cleanup gaps (missing
`NativeDestruct`/`RemoveDynamic` pairing on `UTSHUDWidgetBase` and a stale cross-team `OnCrewCommandIssued`
binding in `UTSCrewHUDWidget` when a player switches teams mid-match) were confirmed real but are
UI-lifecycle hygiene issues, not security or correctness bugs — worth fixing when you build out the
actual WBP widget-swap flow, since the right fix depends on how you construct/destroy these widgets in
Blueprint.

## 4. Wiring the existing tank Blueprint (Section 9)

`DefaultGame.ini` points `GlobalDefaultGameMode` at `/Game/YI_TankCollection/Blueprint/CustomController/Tank_GameMode`,
and the project has `ChaosVehiclesPlugin`/`ChaosModularVehicleExamples` enabled — the existing tank
Blueprint almost certainly derives from a Chaos vehicle pawn class already. That rules out **Path A**
for it (a Blueprint can only have one native parent), which is exactly why the doc offers **Path B**.

**Path A — new/simple tanks.** Create a Blueprint deriving from `ATSTank`. You get the four
components and `ITSTankInterface` for free; just override the 5 `BP_*` events (Class Defaults →
Functions) with real animation/weapon/movement calls.

**Path B — the existing YI_TankCollection tank (recommended here).**
1. Open the tank Blueprint. Class Settings → Interfaces → add `TSTankInterface`.
2. In the Components panel, add one each of `TSTankCrewComponent`, `TSTankControlComponent`,
   `TSTankWeaponComponent`, `TSTankCommanderComponent` (all four are `BlueprintSpawnableComponent`,
   so they appear in the Add Component search).
3. Implement the 5 interface events (`BP_SetDriveInput`, `BP_AimTurret`, `BP_FireMainCannon`,
   `BP_FireMachineGun`, `BP_UpdateCommanderIntel`) using the Blueprint's existing movement/turret/
   firing logic.
4. Set `ATSGameMode::DefaultTankClass` (GameMode Blueprint defaults) to this tank Blueprint. The
   property is filtered to classes implementing `TSTankInterface`.

Either path, once `DefaultTankClass` is set, `ATSGameMode::GetOrSpawnTankForTeam` spawns and assigns
it automatically the first time a player picks a role on that team.

**Spawn points.** Tag level actors `TSTeamSpawn_TeamA` / `TeamB` / `TeamC` / `TeamD` for deterministic
placement. Without a tag, `ATSGameMode::GetSpawnTransformForTeam` falls back to a fixed offset from the
origin and logs a warning — fine for smoke testing, not for a real level.

## 5. Content still to author in the Editor

None of these can be produced as text files — they're binary editor assets. Nothing in the framework
is blocked on them existing (every C++ property that references one is `EditDefaultsOnly`/optional and
degrades safely to "no input bound" / "no visuals" if left unset), but the framework does nothing
visible until they do.

1. **GameMode Blueprint** — derive from `ATSGameMode`, set `DefaultTankClass`. Point
   `GlobalDefaultGameMode` (`DefaultGame.ini`) at it once ready — not changed automatically, to avoid
   overwriting your current demo-map default.
2. **Enhanced Input assets** (`ATSVRPawn`'s `EditDefaultsOnly` slots, Section 12):
   - Input Actions: `IA_Interact`, `IA_Grab`, `IA_Menu`, `IA_Recenter` (shared);
     `IA_Drive` (Axis2D), `IA_AimTurret` (Axis2D), `IA_FireMainCannon`, `IA_FireMachineGun`,
     `IA_ReloadWeapon`, `IA_RequestIntel` (role-specific).
   - Input Mapping Contexts: `IMC_Shared` (binds the 4 shared actions to VR controller inputs),
     `IMC_Driver` / `IMC_Gunner` / `IMC_Commander` (bind the role-specific actions). The pawn
     swaps these in via `ApplyRoleMappingContext` whenever `CrewRole` changes.
   - Assign all of the above on a Blueprint subclass of `ATSVRPawn` (or the C++ class defaults).
3. **Data Assets** — instances of `UTSRoleDefinition` (×3, one per role) and `UTSTankDefinition` if you
   want designer-tunable ammo/display data outside C++ defaults.
4. **Widget Blueprints**, each deriving from its matching C++ base:

   | WBP | C++ base | Minimum to wire up |
   |---|---|---|
   | `WBP_Login` | `UTSLoginWidget` | buttons calling `NotifyLogin` / `NotifyContinue` |
   | `WBP_SessionBrowser` | `UTSSessionBrowserWidget` | buttons calling `CreateSession` / `RefreshSessions` / `JoinSession`; implement `OnSessionListUpdated` to populate a list |
   | `WBP_TeamSelection` | `UTSTeamSelectionWidget` | 4 buttons calling `NotifyTeamSelected` |
   | `WBP_RoleSelection` | `UTSRoleSelectionWidget` | 3 buttons calling `NotifyRoleSelected` |
   | `WBP_CrewHUD` | `UTSCrewHUDWidget` | implement `OnAssignmentRefreshed` / `OnCommandReceived` |
   | `WBP_DriverHUD` | `UTSDriverHUDWidget` | implement `OnSpeedUpdated` |
   | `WBP_GunnerHUD` | `UTSGunnerHUDWidget` | implement `OnWeaponStateChanged`, read the `Get*Ammo`/`GetAimDirection`/`IsReloading` getters |
   | `WBP_CommanderHUD` | `UTSCommanderHUDWidget` | implement `OnIntelUpdated`; buttons calling `RequestIntelRefresh` / `IssueCommand` |

5. **GameInstance Blueprint** (optional) — derive from `UTSGameInstance` and set it as the project's
   Game Instance Class if you want the `GetSessionSubsystem` convenience node; the subsystem works
   without this too (`GetGameInstance()->GetSubsystem<UTSSessionSubsystem>()`).

## 6. Server RPC reference (Section 10/15)

All RPCs live on `ATSTankPlayerController`, not on the Tank or its components: a shared Tank actor has
no single owning `NetConnection` (three different players act on it), so the RPC boundary has to be
each player's own controller, which always has one.

| RPC | Reliability | Validates | Result |
|---|---|---|---|
| `ServerRequestTeam` | Reliable | Team is a real team, not already full | `ATSGameMode::RequestTeamAssignment` |
| `ServerRequestRole` | Reliable | Player has a team; seat free | `ATSGameMode::RequestRoleAssignment`, spawns the team's tank on first request |
| `ServerSetDriveInput` | Unreliable | Finite floats in range; sender is the tank's Driver | `UTSTankControlComponent::TryApplyDriveInput` |
| `ServerAimTurret` | Unreliable | Non-NaN vector; sender is the Gunner | `UTSTankWeaponComponent::TryAimTurret` |
| `ServerFireMainCannon` | Reliable | Sender is the Gunner; ammo > 0; fire-rate cooldown | `UTSTankWeaponComponent::TryFireMainCannon` |
| `ServerFireMachineGun` | Unreliable | Same as above | `UTSTankWeaponComponent::TryFireMachineGun` |
| `ServerRequestReload` | Reliable | Sender is the Gunner; not already reloading | `UTSTankWeaponComponent::TryReload` |
| `ServerRequestCommanderIntelRefresh` | Reliable | Sender is the Commander | `UTSTankCommanderComponent::TryRefreshIntel` |
| `ServerIssueCrewCommand` | Reliable | Sender is the Commander; command != None | `UTSTankCommanderComponent::TryIssueCommand` |

Every `Try*` function on the components re-checks seat occupancy via `UTSTankCrewComponent::HasAccess`
independently of the PlayerController's own check — defense in depth per the doc's "never trust
client-supplied ... " rule, and it keeps the components safe to call from anywhere in the future, not
just from the controller.

## 7. Permission matrix (Section 8)

Implemented as a single function, `FTSPermissions::GetAccessLevel` in
[TSTypes.cpp](../Source/Tank_Sim_V2/Core/TSTypes.cpp) — every validation path in the framework routes
through it, so the matrix only needs updating in one place:

| Capability | Driver | Gunner | Commander |
|---|---|---|---|
| Drive | Full | Denied | Denied |
| Main cannon / Machine gun / Turret aim | Denied | Full | Denied |
| Radar/intel | Denied | Limited | Full |
| Crew commands | Denied | Denied | Full |
| Voice / Tank status | Full | Full | Full |

Gunner's "Limited" radar/intel access is implemented in `UTSTankCommanderComponent::GetIntelFor`: it
returns known enemy positions but not the placement list or summary text that Commander gets.

## 8. Voice (Section 13)

`UTSVoiceSubsystem` wraps `IVoiceChat`/`IVoiceChatUser` (Engine's `VoiceChat` module — header-only,
resolved through `IModularFeatures` at runtime). With no voice backend plugin enabled,
`IVoiceChat::Get()` returns null and every subsystem call is a safe no-op (logged once at startup).

To get real audio: enable a backend that registers the `IVoiceChat` modular feature — Epic ships
**EOSVoiceChat** (`Engine/Plugins/Online/VoiceChat/EOSVoiceChat`) for this. Enable it in
Edit → Plugins, configure EOS credentials, and `JoinCrewChannel`/`SetMuted` start doing real work with
no code changes on the framework side.

## 9. Known limitations / next steps

- **Commander intel still replicates in full to all crew at the wire level.**
  `UTSTankCommanderComponent::Intel` replicates to all clients (COND_None); the §3 review fix makes
  `OnRep_Intel` push only the role-filtered copy to Blueprint, so no legitimate UI shows the wrong data
  — but a determined client could still read the raw replicated struct directly (e.g. via memory
  inspection). Closing that fully needs per-connection replication filtering (e.g. a custom
  `IsNetRelevantFor`/subobject-channel scheme keyed off crew role), which is a meaningfully bigger
  change than this pass's scope given the shared-actor-with-no-single-owner architecture described in
  §6.
- **Two UI-widget delegate-cleanup gaps found in review (§3) were left as-is** pending the actual WBP
  widget lifecycle being built out: `UTSHUDWidgetBase` has no `NativeDestruct`/`RemoveDynamic` pairing
  for its `PS->OnAssignmentChanged` binding, and `UTSCrewHUDWidget` doesn't unbind from a previous
  tank's `OnCrewCommandIssued` when a player switches teams mid-match. Neither crashes (Unreal's
  dynamic multicast delegates skip destroyed targets), but both can leave stale subscriptions.
- **`ETSCrewCommand` is a starter set** (`None`/`Regroup`/`HoldPosition`/`Advance`/`Retreat`) — the doc
  says Commander "issues supported commands" without enumerating them. Extend the enum as gameplay
  needs grow; `TryIssueCommand`/`ServerIssueCrewCommand` need no changes.
- **No health/damage model.** The doc's architecture rules mention the server owning "damage" as a
  category, but no concrete fields are specified anywhere in the doc. Not implemented — add it to
  `ATSTank`/`UTSTankCrewComponent` when the design is decided rather than guessing at one now.
- **VoiceChat module dependency.** It's included via `PrivateIncludePathModuleNames` rather than
  `PrivateDependencyModuleNames` because it ships with zero `.cpp` files (pure interface headers) and
  is `ClientOnly` — UBT rejects it as a normal link dependency. If you add a dedicated Server target
  later, this needs no change (there's nothing being linked either way).
- **`UTSSessionSubsystem::FindSessions`'s `bIsPresence` parameter is currently unused.** The expected
  `SEARCH_PRESENCE` query-settings key used to filter presence-only sessions doesn't exist under that
  name in UE 5.7 (searched the installed engine source and couldn't confirm its replacement), so the
  filter was dropped rather than guessed at. LAN vs. online is still controlled by `bIsLanQuery`. If you
  need presence filtering, check `FOnlineSessionSearch`'s query-settings keys for your enabled OSS.
- **Session default is `OnlineSubsystemNull`** (LAN-only, no real matchmaking/NAT traversal). Swap in
  Steam/EOS by enabling the relevant plugin and changing `DefaultPlatformService` in
  `DefaultEngine.ini` — `UTSSessionSubsystem` needs no changes, it talks to whatever `IOnlineSubsystem`
  is configured.
- **`CreateSession` does not travel to a map.** It only creates the session; call
  `GetWorld()->ServerTravel("/Game/YourMap?listen")` yourself afterward (from wherever your map path is
  known) once `OnCreateSessionComplete` fires successfully.

## 10. Definition of Done (Section 16) — verification checklist

| # | Item | How to verify |
|---|---|---|
| 1 | Host creates a session, clients join | `WBP_SessionBrowser` → Create, then Join from a second client/PIE instance |
| 2 | Teams/roles are server-authoritative | Try `ServerRequestRole` from a client with no team assigned — `ATSGameMode::RequestRoleAssignment` returns false |
| 3 | Each team has one tank | `ATSGameState::GetTeamTankEntries` has ≤1 entry per `ETSTeamId` |
| 4 | 3 clients occupy Driver/Gunner/Commander on the same tank | 3 PIE clients pick the same team, different roles; `UTSTankCrewComponent`'s 3 seats fill |
| 5 | Each role can only do its allowed actions | Have a Driver call `ServerFireMainCannon` — `TryFireMainCannon` returns false (not the Gunner) |
| 6 | Blueprint tank executes authorized commands | Drive/fire inputs from the Driver/Gunner reach the tank's `BP_*` overrides |
| 7 | Crew voice works | `IsVoiceChatAvailable()` true after enabling a voice backend (§8); 3 crew hear each other |
| 8 | VR input reaches the correct command path | VR controller trigger → `ATSVRPawn::Input_FireMainCannon` → `ServerFireMainCannon` |
| 9 | C++ widgets expose bindable events without owning gameplay authority | Every widget's server-affecting action goes through `ATSTankPlayerController`, never mutates state directly |
