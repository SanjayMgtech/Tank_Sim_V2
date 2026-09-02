# Tank Simulation — Implementation Walkthrough

A step-by-step, do-this-then-this guide to turn the compiled C++ framework into a playable 3-crew
VR tank match in the Editor. Everything here is Editor/Blueprint work — none of it can be done as a
text file, which is why it wasn't done automatically.

Read this alongside [Tank_Simulation_Setup_Guide.md](Tank_Simulation_Setup_Guide.md) (the reference —
what each class does, the RPC table, the permission matrix) and
[Tank_Simulation_Framework_Documentation.md](Tank_Simulation_Framework_Documentation.md) (the original
design doc). This document is the *order* to do things in; that one is *what things are*.

**Before you start:** build the C++ once (`Engine\Build\BatchFiles\Build.bat Tank_Sim_V2Editor Win64
Development -project="<path>\Tank_Sim_V2.uproject"`) so every class below shows up in the Editor's
class pickers. It already builds clean as of this writing.

---

## Step 1 — Wire the existing tank Blueprint

Your tank Blueprint almost certainly derives from a Chaos vehicle pawn already (the project has
`ChaosVehiclesPlugin` enabled and a `YI_TankCollection` asset pack), which rules out deriving it from
`ATSTank` — a Blueprint can only have one native parent. Use **Path B**:

1. Open the tank Blueprint (Content Browser → find it under `YI_TankCollection` or wherever your tank
   pawn lives).
2. **Class Settings** (toolbar) → **Interfaces** → **Add** → `TSTankInterface`.
3. In the **Components** panel, click **Add** and add one each of:
   - `TSTankCrewComponent`
   - `TSTankControlComponent`
   - `TSTankWeaponComponent`
   - `TSTankCommanderComponent`

   (They're C++ classes flagged `BlueprintSpawnableComponent`, so they show up in the normal Add
   Component search — type "TSTank" to find all four quickly.)
4. Open the **My Blueprint** panel → **Interfaces** section (or **Functions**, depending on engine
   version) → you'll see the 5 new overridable events. Implement each one using logic that already
   exists in this Blueprint:
   - `BP_SetDriveInput(Throttle, Steering)` → feed your Chaos vehicle movement component's
     throttle/steering input.
   - `BP_AimTurret(AimDirection)` → rotate the turret/barrel toward `AimDirection`.
   - `BP_FireMainCannon()` → call whatever node currently fires the main cannon.
   - `BP_FireMachineGun()` → call whatever node currently fires the machine gun.
   - `BP_UpdateCommanderIntel(Intel)` → optional for now; wire it up once you build the Commander HUD
     in Step 7. `Intel` is an `FTSCommanderIntel` struct (enemy positions, tank placements, a summary
     string).
5. Compile and save.

If instead you're building a brand-new, simple tank with no existing Blueprint constraints, skip all
of the above and just create a new Blueprint deriving from `ATSTank` directly (**Path A**) — the four
components and the interface come for free, and you only need to override the 5 events.

## Step 2 — Create the GameMode Blueprint

1. Content Browser → **Add** → **Blueprint Class** → search `ATSGameMode` as the parent → name it
   `BP_TSGameMode`.
2. Open it, go to **Class Defaults**, and set **Default Tank Class** to the tank Blueprint from Step 1.
   (The picker is filtered to classes implementing `TSTankInterface`, so if your tank doesn't show up,
   double check Step 1.3-1.4 were actually saved.)
3. Leave **Max Teams** at its default (4) unless you want fewer teams available.

## Step 3 — Point the project at the new GameMode

- **Project Settings → Maps & Modes → Default GameMode** → `BP_TSGameMode`. This is the global default;
  a specific level can still override it in **World Settings → GameMode Override** if you want to keep
  your existing demo map's GameMode untouched for now and only use `BP_TSGameMode` on a test level.
- Don't change `GameDefaultMap`/`EditorStartupMap` yet if you want to keep testing on your current demo
  map — do that once you have a level with team spawn points (Step 9).

## Step 4 — Create the Enhanced Input assets

All in Content Browser → **Add** → **Input** (or right-click → **Input**):

**Input Actions** (Miscellaneous → Input Action), one each:

| Name | Value Type |
|---|---|
| `IA_Interact` | Digital (bool) |
| `IA_Grab` | Digital (bool) |
| `IA_Primary` | Digital (bool) |
| `IA_Secondary` | Digital (bool) |
| `IA_Menu` | Digital (bool) |
| `IA_Recenter` | Digital (bool) |
| `IA_Drive` | Axis2D (Vector2D) |
| `IA_AimTurret` | Axis2D (Vector2D) |
| `IA_FireMainCannon` | Digital (bool) |
| `IA_FireMachineGun` | Digital (bool) |
| `IA_ReloadWeapon` | Digital (bool) |
| `IA_RequestIntel` | Digital (bool) |

**Input Mapping Contexts** (Input → Input Mapping Context):

- `IMC_Shared` — bind `IA_Interact`, `IA_Grab`, `IA_Primary`, `IA_Secondary`, `IA_Menu`, `IA_Recenter` to
  your VR controller buttons/triggers (this is applied to every player regardless of role).
- `IMC_Driver` — bind `IA_Drive` to the appropriate stick/trackpad axis.
- `IMC_Gunner` — bind `IA_AimTurret`, `IA_FireMainCannon`, `IA_FireMachineGun`, `IA_ReloadWeapon`.
- `IMC_Commander` — bind `IA_RequestIntel` (and whatever input you want for issuing crew commands, once
  you build that into the Commander HUD/interaction in Step 7).

Exact button/trigger bindings depend on your target headset — use OpenXR's generic action paths
(trigger, grip, thumbstick, primary/secondary face buttons) so it works across headsets rather than
binding to one controller's specific hardware path.

## Step 5 — Create the VR Pawn Blueprint

1. Content Browser → **Add** → **Blueprint Class** → parent `ATSVRPawn` → name it `BP_TSVRPawn`.
2. Class Defaults, under **Tank Simulation|Input**, assign:
   - **Shared Mapping Context** → `IMC_Shared`
   - **Driver Mapping Context** → `IMC_Driver`
   - **Gunner Mapping Context** → `IMC_Gunner`
   - **Commander Mapping Context** → `IMC_Commander`
   - Each `IA_*` slot → its matching Input Action from Step 4.
3. (Optional) Override `OnInteractPressed` / `OnGrabPressed` / `OnPrimaryPressed` / `OnSecondaryPressed`
   / `OnMenuPressed` (Blueprint events) if you want hand-interaction with cockpit levers/switches — the
   doc doesn't prescribe what these do, they're extension points.
4. Set `BP_TSGameMode`'s **Default Pawn Class** to `BP_TSVRPawn` (Class Defaults on the GameMode
   Blueprint from Step 2) — this is what the engine spawns for a newly-joined player before they've
   picked a team/role.

## Step 6 — (Optional) Data Assets

Only needed if you want designer-tunable values outside C++ defaults:

- Content Browser → **Add** → **Miscellaneous** → **Data Asset** → pick `TSRoleDefinition` → create
  three instances (`DA_Role_Driver`, `DA_Role_Gunner`, `DA_Role_Commander`) and fill in display
  name/color/icon per role.
- Same for `TSTankDefinition` if you want per-tank-type ammo capacities defined as data — note this
  isn't read by any C++ yet (see the setup guide's Known Limitations); it's scaffolding for when you
  wire it up.

## Step 7 — Widget Blueprints

Create each as **Add → User Interface → Widget Blueint**, but pick the matching C++ **Parent class** in
the creation dialog (search for the class name — they won't show up under the default "User Widget"
parent unless you change it there first).

| Widget Blueprint | Parent class | What to add in the designer |
|---|---|---|
| `WBP_Login` | `TSLoginWidget` | A Login button and a Continue button; wire their **OnClicked** to call `Notify Login` / `Notify Continue`. |
| `WBP_SessionBrowser` | `TSSessionBrowserWidget` | Create/Refresh buttons calling `Create Session` / `Refresh Sessions`; a list (ScrollBox or ListView) populated in the **On Session List Updated** event (override it — it hands you the `TArray<FTSSessionSearchResult>`); each row's Join button calls `Join Session` with that row's index. |
| `WBP_TeamSelection` | `TSTeamSelectionWidget` | 4 buttons (Team A–D), each calling `Notify Team Selected` with the matching `ETSTeamId`. |
| `WBP_RoleSelection` | `TSRoleSelectionWidget` | 3 buttons (Driver/Gunner/Commander), each calling `Notify Role Selected` with the matching `ETSCrewRole`. |
| `WBP_CrewHUD` | `TSCrewHUDWidget` | Text blocks bound to `Get Team Id` and `Get Occupant Name` (call with Driver/Gunner/Commander) for a crew roster; override **On Command Received** to flash a "Commander says: X" banner. |
| `WBP_DriverHUD` | `TSDriverHUDWidget` | A speedometer; override **On Speed Updated** to set its value from the `SpeedCentimetersPerSecond` parameter (divide by ~44.7 for a rough mph display, or ~36 for km/h). |
| `WBP_GunnerHUD` | `TSGunnerHUDWidget` | Ammo counters bound to `Get Main Cannon Ammo` / `Get Machine Gun Ammo`, a reload indicator bound to `Is Reloading`; override **On Weapon State Changed** to refresh them each tick. |
| `WBP_CommanderHUD` | `TSCommanderHUDWidget` | A map/list rendering `Get Intel`'s `KnownEnemyPositions`; a Refresh button calling `Request Intel Refresh`; command buttons (Regroup/Hold/Advance/Retreat) calling `Issue Command` with the matching `ETSCrewCommand`; override **On Intel Updated** to refresh the display. |

Add these widgets to the viewport from wherever makes sense in your flow (e.g. `WBP_Login` on
`BeginPlay` of the level Blueprint or a startup GameInstance event; `WBP_CrewHUD`/role-specific HUD once
`ATSTankPlayerState::OnAssignmentChanged` fires with a non-`None` role — bind to that delegate from
whichever Blueprint owns your HUD-swap logic).

## Step 8 — (Optional) GameInstance Blueprint

Content Browser → **Add → Blueprint Class** → parent `UTSGameInstance` → name it `BP_TSGameInstance` →
**Project Settings → Maps & Modes → Game Instance Class** → set it. This only matters if you want the
`Get Session Subsystem` convenience node in Blueprint graphs; without it, use
`Get Game Instance → Get Subsystem (class: TSSessionSubsystem)` instead, which works identically.

## Step 9 — Level setup

In whatever level you'll test on, place empty Actors (or any actor) at each team's intended spawn point
and tag them (**Details → Actor → Tags**, or via a Tag component) with:

- `TSTeamSpawn_TeamA`
- `TSTeamSpawn_TeamB`
- `TSTeamSpawn_TeamC`
- `TSTeamSpawn_TeamD`

Without these, `ATSGameMode::GetSpawnTransformForTeam` falls back to a fixed offset from the world
origin and logs a warning — fine for a first smoke test, not for a real level layout.

## Step 10 — Test it

Use Unreal's multiplayer PIE: **Play** dropdown (next to the Play button) → **Number of Players: 3** →
**Net Mode: Play as Listen Server** → Play. This gives you 3 local PIE windows you can drive
independently to exercise the whole crew.

Walk through the [Definition of Done checklist](Tank_Simulation_Setup_Guide.md#10-definition-of-done-section-16--verification-checklist)
in the setup guide in order:

1. Client 1 creates a session (`WBP_SessionBrowser`); Clients 2 and 3 find and join it.
2. All 3 pick the same team, then Driver/Gunner/Commander respectively.
3. Driver input moves the tank; Gunner input aims/fires; Commander's Refresh Intel populates their HUD.
4. Try a Driver calling fire (they can't — the button shouldn't even be on their HUD, but if you're
   testing via console/Blueprint call directly, confirm `TryFireMainCannon` returns `false`).
5. Disconnect one client, confirm their seat frees up (`WBP_RoleSelection` should show it open again for
   a new joiner).

## Step 11 — (Optional) Voice

To get actual crew voice audio rather than the current no-op: **Edit → Plugins** → enable
**EOSVoiceChat** → restart the Editor → follow Epic's EOS setup (product/sandbox/deployment IDs from
the Epic Developer Portal) → configure them in **Project Settings → Online Subsystem EOS**. No code
changes are needed on the framework side — `UTSVoiceSubsystem` starts working the moment a real
`IVoiceChat` backend registers itself.

## Step 12 — Session backend for real matchmaking (optional)

The framework defaults to `OnlineSubsystemNull` (LAN-only — fine for the same-machine PIE testing in
Step 10, and for a local network). For internet matchmaking, enable **OnlineSubsystemSteam** or
**OnlineSubsystemEOS**, then in `Config/DefaultEngine.ini` change:

```ini
[OnlineSubsystem]
DefaultPlatformService=Steam
```

(or `EOS`). `UTSSessionSubsystem` needs no code changes — it already talks to whichever
`IOnlineSubsystem` is configured.

---

## Quick reference: what's C++ (done) vs. what's yours (Steps above)

| | Done in C++ | Yours to build |
|---|---|---|
| Server authority, validation, replication | ✅ Everything | — |
| Session create/find/join/destroy logic | ✅ | Backend config only (Step 12) |
| Team/role assignment, permission matrix | ✅ | — |
| Tank drive/aim/fire contract + components | ✅ | The 5 `BP_*` bodies (Step 1) |
| VR input routing | ✅ | The actual Input Action assets + bindings (Step 4-5) |
| UMG delegate/event contracts | ✅ | The actual widget layouts (Step 7) |
| Voice abstraction | ✅ | A real backend plugin (Step 11) |
