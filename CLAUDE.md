# Tank_Sim_V2 — Blueprint → C++ Port Guide

This file is the operating manual for porting the tank controller from Blueprint to C++.
Attempt 1 failed. This documents why, and the method that replaces it.

---

## 1. The Core Principle

**C++ owns logic. Blueprint owns data.**

This is Epic's own guidance ([Balancing Blueprint and C++](https://dev.epicgames.com/documentation/en-us/unreal-engine/balancing-blueprint-and-cplusplus?application_version=4.27)).
C++ is for base classes, APIs, math, replication. Blueprint stays the home for
"data-heavy classes mixing logic and configuration" — components, asset references,
and tuning values.

**You reparent a Blueprint. You do not rewrite it.**

The Blueprint keeps existing and keeps holding its data. C++ slides in underneath it
as a new parent class. Nothing about the Blueprint's components or defaults should
need to be retyped by hand — if you are retyping property values into C++, you have
already made the mistake that killed attempt 1.

---

## 2. Hard Rules (violating these caused real, costly bugs)

### RULE 1 — Never `CreateDefaultSubobject` for a component the Blueprint already has
If the component exists in the Blueprint's Components panel (SCS), **leave it there**.
Recreating it in the C++ constructor re-bases the component, which means every single
property on it (mass, collision profile, intensity, auto-activate, ...) must be manually
copied into C++. Anything missed silently reverts to an engine default.

Attempt 1 recreated ~23 components this way. Result: a long tail of silent regressions,
each one only discoverable by diffing property-by-property against a backup.

### RULE 2 — Never load assets in the constructor
No `ConstructorHelpers::FObjectFinder`. No `ConstructorHelpers::FClassFinder`.
Epic explicitly advises against hard-referencing assets in C++ constructors.

`FClassFinder` on a **Blueprint class** is the worst case: it is a
[known boot deadlock](https://forums.unrealengine.com/t/static-constructorhelpers-fclassfinder-in-uobject-constructors-can-cause-deadlock-during-boot/263572)
and [crashes packaged builds](https://forums.unrealengine.com/t/constructorhelpers-fclassfinder-causing-project-to-crash/351974).
In attempt 1 this hung the editor at "Compiling Blueprints" on every launch, and only
triggered once Blueprints were parented to the C++ class.

Instead: declare `UPROPERTY(EditDefaultsOnly)` `TSoftObjectPtr` / `TSubclassOf` and let the
**Blueprint set the value**. Load lazily at `BeginPlay` if you must load in code.

### RULE 3 — Move one thing at a time, and test it
Never transcribe a whole graph in one pass. Attempt 1 did, and accumulated at least six
independent regressions before anyone pressed Play — each then took hours to isolate.

### RULE 4 — Match names exactly when moving variables
A C++ `UPROPERTY` with the same name and type as the Blueprint variable lets existing
Blueprint values and node connections carry over. Mismatched names silently orphan data.

### RULE 5 — Close the editor *gracefully* before rebuilding C++
Live Coding cannot safely patch constructors or class layout. Adding or removing a
`UPROPERTY` changes class layout, so **every variable-move phase needs a full rebuild.**
Live Coding caused repeated `EXCEPTION_ACCESS_VIOLATION` crashes in the destructor
during attempt 1.

Close the editor from its own File menu / window close button. Do **not**
`Stop-Process`/`taskkill` it: a force-kill makes the next launch open a blocking
auto-save recovery modal, which freezes the MCP server until a human dismisses it
(`LogMonolith: Warning: MODAL_OPEN ... MCP will be unresponsive until dismissed`).
That costs a human round-trip on every single rebuild.

### 🔁 DO THE REBUILD YOURSELF - never ask the human to close the editor
Standing instruction from the user (2026-09-07): when a rebuild is needed, close the editor and
rebuild without asking. `CloseMainWindow()` posts WM_CLOSE, which is exactly the window-close
button RULE 5 permits - it runs the editor's own shutdown path, not a kill.

```bash
powershell -NoProfile -Command "Get-Process UnrealEditor -EA SilentlyContinue | ForEach-Object { $_.CloseMainWindow() | Out-Null }"
# then poll until it is gone - do NOT escalate to Stop-Process if it lingers
powershell -NoProfile -Command "(Get-Process UnrealEditor -EA SilentlyContinue) -ne $null"
```
If the process is still alive after ~60s it is sitting on a **save-content modal**, which only a
human can dismiss (see the human-only table). Say so and wait; do not force-kill, and do not
answer the modal by guessing. **Never** save `Controller_Demo_T90.umap` (170 MB) - Don't Save.

Remember the editor is SHARED with other concurrent sessions - closing it interrupts them too.
Relaunch it as soon as the build finishes.

Full rebuild with the editor closed:
```bash
"C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" Tank_Sim_V2Editor Win64 Development -Project="C:\Projects\Tank_Sim_V2\Tank_Sim_V2.uproject" -WaitMutex -FromMsBuild
```
`Unable to build while Live Coding is active` in the output means the editor is still up - the
close did not take. UHT still runs before that error, so a clean UHT pass there proves headers
parse but proves **nothing** about the `.cpp` files.

Relaunch afterwards:
```bash
"C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" "C:\Projects\Tank_Sim_V2\Tank_Sim_V2.uproject"
```
If Live Coding was used, delete stale patches before relaunching, or the editor may hang/crash:
```bash
rm -f Binaries/Win64/UnrealEditor-Tank_Sim_V2.patch_*
```

### RULE 6 — Never leave stray `.uasset` files in `Content/`
The editor scans and compiles everything under `Content/` at startup. A stray backup
Blueprint there gets loaded and compiled. Keep backups in
`CPP_Port_WIP_DO_NOT_USE_YET/`, never in `Content/`.

### RULE 7 — When the tooling cannot do it, HAND IT OFF. Do not experiment.
The MCP surface cannot do everything the editor UI can. When a step has no action for it,
**stop and ask the human** instead of trying near-miss actions to see what happens. A wrong
guess here does not just fail — it can corrupt a Blueprint in memory and cost a recovery cycle.

This rule exists because of a real incident: with no rename-parameter action available,
`set_function_params` was tried on a live function. Its description says "**Add** input/output
parameters" and it meant it literally — it appended a duplicate of every parameter and output.
Recovery took a package reload plus a human clicking through a modal. The human then did the
rename by hand in about a minute.

**Hand-off format — give all four, briefly:**
1. **Why** — what is blocked and what it unblocks.
2. **Where** — exact asset, graph, panel.
3. **What** — the precise change, with exact names/values.
4. **How we verify** — what will be checked afterwards to prove it worked.

**Before starting any step, ask: is there an action for this?** If not, it is a hand-off.
Check the namespace action list rather than assuming an action exists because it "should".

### RULE 8 — Prefer the SIMPLEST mechanism a designer can edit
Before writing placement, tuning or layout data as C++ properties, ask whether it could just be
a **thing in the Blueprint** — a component, a curve, a data asset. If a human will ever want to
adjust it, they should be able to adjust it by dragging or typing in the editor, not by editing a
header and waiting for a rebuild.

This is the same principle as RULE 1 and the core "C++ owns logic, Blueprint owns data" split,
applied to *new* code rather than ported code. It is easy to honour the rule while porting and
then quietly break it the moment you add something new.

**Worked example — VR crew seat placement.** The first version put three `FVector` offsets on
`ATSVRPawn` as `EditDefaultsOnly` properties, plus mesh socket names, plus fallback logic to pick
between them. Two mechanisms, numbers typed into a header, and the same seats for all six tanks.

The replacement is three **scene components** on the tank Blueprint named `DriverSeat`,
`GunnerSeat`, `CommanderSeat`. C++ only looks them up by name; where they sit is Blueprint data.
That is strictly better on every axis:
- a designer drags a gizmo and sees exactly where the player's head will be
- each per-tank Blueprint overrides its own seats — which matters, the six hulls differ
- **it subsumes sockets**: parent the seat component to a mesh socket in the Components panel and
  it rides that socket, with no code change
- one mechanism instead of a socket path plus an offset fallback path

Signs you are on the wrong side of this rule: a `FVector`/`FRotator`/`FTransform` UPROPERTY that
describes *where something goes*; two code paths that exist only to choose between two ways of
authoring the same value; a "fallback default" for data that a designer was always going to set.

Keep the C++ side to a **name lookup plus a loud warning** when the Blueprint has not provided the
thing. Silence here is the RULE 1 failure mode all over again — the crew member ends up standing
at the tank's origin and it reads as "seating is broken" rather than "nobody placed the seat".

### ⚠ A parameter or local named `Role` will not compile
`AActor` declares a deprecated member `Role` (legacy `ENetRole`), and UHT builds with
`-WarningsAsErrors`, so C4458 shadowing is a hard error:
```
error C4458: declaration of 'Role' hides class member
```
This has now bitten twice — `ATSGameMode::TryAssignRole` (named `RequestedRole`) and
`ATSVRPawn::GetSeatComponentNameForRole` (named `InRole`). Use `InRole`, `CrewRole` or
`RequestedRole`, never bare `Role`, for parameters AND locals on any `AActor` subclass.

**`Mesh` is the same trap.** `AWheeledVehiclePawn` declares a member `Mesh`, so a loop variable
`for (USkeletalMeshComponent* Mesh : Meshes)` inside `ATSTankControllerBase` is also a hard error.
Name it `MeshComp`. Assume any short, obvious name (`Mesh`, `Role`, `Owner`, `Controller`) is
already taken somewhere in the `AActor` chain.

### Spawned sessions and git worktrees — Unreal is NOT reachable from a worktree
`.mcp.json` starts the Monolith proxy from `Plugins/Monolith/Binaries/monolith_proxy.exe`.
`Plugins/` is **not tracked in git**, so a worktree never contains it and the proxy reports
`CONNECTION_CLOSED` every time. A subagent spawned into a worktree therefore cannot touch the
editor at all, and will end up working in the main checkout instead — which looks like an empty
worktree and an untouched branch even when the work was done. Do not read an empty worktree as
"nothing happened"; check the main checkout too.

Fixed 2026-09-04 by making that path absolute in `.mcp.json` (the file is untracked, so this is
local-only and does not affect other clones). Also note: only ONE editor and ONE main checkout
serve all concurrent sessions, so log lines and file writes may come from another agent — always
stage commits with an explicit pathspec, never `git add -A`.

### Things ONLY the human can do (known list — extend as found)
| Task | Why the tooling cannot |
|---|---|
| Rename a Blueprint **function parameter** | No rename-parameter action; `set_function_params` is additive. Done in the function's Details panel; the editor fixes up call sites and keeps connections (verified Phase 15). |
| Dismiss editor modal dialogs | A modal blocks the game thread, so MCP is unresponsive until a human clicks. Seen with "Save Content", the reload-assets confirm, and the auto-save recovery prompt after a force-kill. |
| ~~Run PIE as Listen Server with 2 players~~ **NO LONGER HUMAN-ONLY** | `run_pie_smoke` still reaches one PIE world, but two `-game` processes give a real listen server + client that a script can drive end to end. See *Automated listen-server testing* below. A human is still needed to JUDGE SMOOTHNESS - logs cannot see jitter. |
| Drive real gameplay events | e.g. `DamageCausedUI` is only written by a macro reached through an actual damage-caused event; macros are inlined and cannot be invoked directly. |
| Anything that is a Blueprint **editor-UI** operation with no MCP action | Reordering pins, graph-level refactors. NOTE: editing a macro's internals was previously listed here and is WRONG - `add_node`/`connect_pins` work on a macro graph (proven on `UpdateDamageCausedUI`). Test before declaring something human-only. |
| Override an **inherited** component's property on a CHILD Blueprint | `set_component_property` only sees a Blueprint's own SCS ("Component not found: DriverSeat" on the child). The override lives in the Inheritable Component Handler, which neither the MCP surface nor Python can create. `SubobjectDataSubsystem` can *read* it (`k2_gather_subobject_data_for_blueprint`). Select the component in the child's Components panel and type the value; verify with `get_inherited_component_override`. |

### Open hand-offs (keep current)
- ~~Replication test~~ **DONE 2026-09-04.** A/B listen-server test showed identical behaviour  before and after the port. Phase 9 verified; the remaining turret faults are pre-existing  feature gaps, documented separately.
- **Parameter renames** are only needed if a port hits shadowing. `WheelRotationDefinition`
  turned out NOT to need one (Phase 17) - its tuning arrives as parameters. Ask only when a
  concrete port is actually blocked.
- **Space-in-name variables** (`Player Controller`, `Is Vehicle taken?`, `Debug Camera`) need a
  Blueprint rename before they could ever be ported. The list used to be longer; the legacy
  turret ones were deleted outright instead — see *DEAD CODE REMOVED*.

---

## 3. Project Facts

### Class structure (original, working)
```
AWheeledVehiclePawn (engine)
└── BP_TankController_Chaos          <- master BP: ALL shared logic + components
    ├── BP_T90_Controller_Chaos       <- per-tank: overrides data only (mesh, camo, wheels)
    ├── BP_Leopard2A7_Controller_Chaos
    ├── BP_M1A2_Controller_Chaos
    ├── BP_Merkava_Controller_Chaos
    ├── BP_Proxy_Controller_Chaos
    └── BP_VK1602Leopard_Controller_Chaos
```
Per-tank BPs are **data only**. The master BP holds the logic. This is already a correct
separation — the port should preserve it, with C++ sliding in under the master BP.

### Components (~23, all defined in the master BP)
`VehicleMesh` (root, prop name is `Mesh`), `VehicleMovementComp`, `Light_R`, `Light_L`,
`P_Exhaust`/`1`/`2`, `SlideBackRight`, `SlideBackLeft`, `SpringArmArcade`, `SpringArmSniper`,
`Camera`, `Brake_L`, `Brake_R`, `TurretMotor`, `TankEngine`, `Fire`, `Tank_Destroyed`,
`Decal`, `DestroyedFlames`, `TrackPath_R`, `Debug Camera` (note the space), `BP_TankWeapon`,
`MainGunSpringTimeline`, `AimTimeline`.

**These all stay in the Blueprint.** Do not recreate them in C++.

### Master BP `Event Tick` order (verified from the graph)
```
Branch → Set DeltaSeconds → Set ForwardSpeedMPH → Sequence:
  then_0: ChassisDistanceDefinition → Set SaggingDegreeR/L
  then_1: (if !UseGeometricTracks) UpdateTracksMID ×2 → TrackPathAnimations ×2
  then_2: (if UseGeometricTracks) TrackPathShift ×2 → TrackPathAnimations ×2 → SetTracksTransform ×2
  then_3: UpdateHUD → UpdateSound → UpdateCrosshairPositionAndSize → TracksDecal → HullAccelerationDefinition →
          AntennaCalculation
  then_4: TurretsAndGunsRotCalculation → Sequence(WheelRotationDefinition ×8, ForEach → Scattering)
```
**`ThrottleControl` and `TurningControl` are NOT in Tick.** They are called only from input
bindings. Attempt 1 wrongly added `TurningControl()` to `Tick()`.

### Input architecture
Enhanced Input. `IMC_Default` + ~25 `IA_*` actions in `/Game/YI_TankCollection/Inputs/`.
Input callbacks set state (e.g. `MoveRightAxis`) and call the control function directly.
An untouched axis fires **no** event — so nothing overwrites state while driving straight.

### Known-good baseline (proven)
The original Blueprint tank **drives correctly**. Verified by spawning
`BP_T90_Controller_Chaos` (pre-reparent) and calling `ThrottleControl(1.0)` repeatedly:
it moved **3372 units in ~3 seconds**. The identical test on the attempt-1 C++ tank produced
**zero net movement**. Use this as the regression test.

### Startup map is heavy
`Content/YI_TankCollection/Maps/Tank_T90/Controller_Demo_T90.umap` is **170 MB**.
Editor cold start is legitimately slow (several minutes). Distinguish *slow* from *hung*:
```bash
tasklist //FI "IMAGENAME eq UnrealEditor.exe" //V     # CPU Time rising = working, flat = hung
```

---

## 4. Attempt 1 — Regressions Found (reference list)

All were silent. All came from recreating components/values in C++ instead of leaving them
in the Blueprint. If the new port is done correctly, **none of these can happen again.**

| Component | Property | Original | Attempt 1 | Effect |
|---|---|---|---|---|
| `DestroyedFlames` | `bAutoActivate` | False | True | Tank on fire from spawn |
| `VehicleMovementComp` | `Mass` | 30000 | 1500 | 20× too light |
| `VehicleMovementComp` | `DifferentialType` | AllWheelDrive | RearWheelDrive | Drivetrain wrong |
| `VehicleMovementComp` | `IdleBrakeInput` | 1.0 | 0.0 | Rolls on slopes |
| `VehicleMovementComp` | `bLegacyWheelFrictionPosition` | True | False | Friction solve differs |
| `VehicleMovementComp` | `SleepThreshold` | 1.0 | 10.0 | — |
| `VehicleMovementComp` | `HandbrakeInputRate` | 3/3 | 12/12 | — |
| `VehicleMovementComp` | `YawInputRate` | 1.5/3 | 6/10 | Turn response |
| `VehicleMovementComp` | `TorqueControl.Enabled` | True | False | Arcade assist off |
| `VehicleMesh` | `bReplicates` | True | False | — |
| `VehicleMesh` | `PhysMaterialOverride` | PM_Metal | None | Surface behavior |
| `Tank_Destroyed` | collision profile | NoCollision | BlockAllDynamic | Wreck blocks world |
| `Light_R/L`, `Brake_L/R` | `IntensityUnits` | Lumens | Unitless | Wrong brightness |
| `Light_R/L` | `ShadowBias` | 0.008 (0.04 Proxy) | 0.5 | Shadow artifacts |
| `Debug Camera` | `RelativeLocation` | (-230,0,30) | (0,0,0) | Camera at origin |

**Logic/structure bugs (separate class of error):**
- `TurningControl()` added to `Tick()` — not in the original graph.
- Input mapping context added only in `BeginPlay`, gated on `GetController()`. For this pawn
  `PossessedBy` fires **before** `BeginPlay`, so the controller was null and input never bound.
- `SetBrakeInput(Throttle * -1)` applied full brake when reversing.
- `TankWeaponComponentClass` left unset → weapon system absent.
- Vehicle stuck in gear 0 (Neutral) — never auto-shifted; root cause never confirmed.

**Unresolved:** even after fixing all of the above, the attempt-1 tank still would not move.
Root cause never found. This is why we restart rather than keep patching.

---

## 5. The Phase Plan

Each phase is small, independently testable, and reversible. **Do not start a phase until
the previous one passes its test.** Commit after each green phase.

### Phase 0 — Baseline
Confirm the original Blueprints work before changing anything.
- All `Content/` Blueprints at original state, no C++ tank class in `Source/`.
- **Test:** editor opens; PIE runs; T90 drives (throttle → real displacement).
- Commit as the known-good baseline.

### Phase 1 — Empty C++ base class
Create `ATSTankControllerBase : AWheeledVehiclePawn`. `Blueprintable`.
**No components. No asset loads. No logic.** Constructor body effectively empty.
- **Test:** compiles; editor opens; nothing else changed.

### Phase 2 — Reparent the master BP
Reparent `BP_TankController_Chaos` to `ATSTankControllerBase`.
Components and all data stay in the Blueprint.
- **Test:** editor opens; **T90 still drives**; no visual/behaviour change at all.
- This is the critical gate. If driving breaks here, stop — nothing has been ported yet,
  so the fault is in the reparent itself.

### Phase 3 — Verify all six tanks
- **Test:** each of the six tanks opens, compiles clean, and drives.

### Phase 4+ — Move variables, one small group per phase
Order: simple scalars → structs → arrays. Match names and types exactly.
- **Test after each group:** values still show correct in the Blueprint defaults; tank drives.

**The move is simpler than expected — you do NOT hand-delete the Blueprint variable.**
Declare the `UPROPERTY` in C++ with the *exact* same name and type, rebuild, and reopen.
UE's Blueprint compiler sees the parent now provides that property, drops the duplicate
Blueprint variable by itself, and rebinds every existing Get/Set node to the native
property. Verified on the Phase 4 group: all 16 nodes kept their original node IDs, the
Blueprint compiled `UpToDate` with 0 errors / 0 warnings, and `remove_variable` then
reports "Variable not found" because it is already gone.

Mapping rules learned:
- BP `double` → C++ `double`; BP `float` → C++ `float`. Do not collapse them.
  (`ForwardSpeedMPH` is the one float in the scratch set; everything else is double.)
- BP `int` → `int32`; BP `struct:Vector` → `FVector` (`FVector::ZeroVector`);
  `struct:Rotator` → `FRotator` (`FRotator::ZeroRotator`).
- **Carry non-zero defaults across.** Most scratch defaults to 0, but not all —
  `TrackSpeedModifier` defaults to `1.0`. Initialising it to 0 would silently scale
  track animation to nothing. Read the default from the BP; never assume zero.
- **Copy misspellings verbatim.** `CurentRPMRatio` is missing an 'r' in the Blueprint.
  "Fixing" the spelling in C++ breaks the name match and orphans the data (RULE 4).
- Carry the BP category string across verbatim, including `|` subcategories
  (e.g. `Category = "Hidden (Used for logic)|Chassis"`), or the details panel regroups.
- `instance_editable: false` → `BlueprintReadWrite` only. Adding `EditAnywhere` would
  expose a variable the Blueprint deliberately hid.

**Pick each group by blast radius, not by convenience.** Before moving anything, run
`find_variable_references` and check whether any per-tank child Blueprint overrides the
default. Variables that are pure runtime scratch (default 0, written every tick, read
only inside the master BP) carry no per-tank data and therefore cannot reproduce the
attempt-1 failure mode. Move those first. Save variables that hold real tuning values
(`MaxSpeedKMH`, `WheelRadius*`, `TracksAmount`, ...) for later phases, and verify the
child override survives the move.

**A variable may be referenced only from code the drive test never runs.** Before moving one,
check *which graph* its references live in — an `Old` suffix or category, or a function absent
from Tick, means the normal drive test will never touch it and the value stays 0 all session.
**Do not read that as a failed rebind, and do not accept it as a pass either.** Prove it by
invoking the owning function directly and diffing the values across the call:
```
t.call_method('<TheFunction>')
```
This is how Phase 6's `TurretRotation` / `MGRotation` / `TurretYaw` / `TurretPitch` were proven,
via the then-existing `UpdateTurretRotation_Old` and `UpdateMachineGunRotation_Old`. Those
functions and those four properties have since been **deleted** as dead code — see
*DEAD CODE REMOVED* below — so the technique outlives its first example.

**Check the AnimBPs before moving anything chassis-related.** `ABP_Chaos_<Tank>` declares
its *own* variables with the same names as several pawn variables — it Sets them in its
EventGraph (copying from the pawn) and Gets them in the AnimGraph. Confirmed on
`ABP_Chaos_T90`:
- Externally consumed → **higher risk class, own phase, own test:** `SaggingDegreeR/L`,
  `WheelRotFrontL/R`, `WheelRotMiddleL/R`, `WheelRotRearL/R`, `WheelRotAccessoryL/R`.
- Only its own bools, no pawn coupling → safe: `ChassisLockedL/R`.
- Not referenced at all: `HullZRot`, `ChassisAcceleration*`, `ChassisDistance*Component*`.

Because names are preserved exactly, an external consumer should keep resolving — but it
widens the blast radius, so these get their own phase and a test that checks the *animation*
(wheels turning, track sag), not just that the value is non-zero on the pawn.

Watch for a naming trap here: some `ABP_Chaos_T90` nodes read `WheelRot AccessoryR`
**with a space**, while the pawn's variable is `WheelRotAccessoryR` without one. They are
two different variables that look identical at a glance.

**Variables with spaces in the name cannot be ported directly** — `Player Controller`,
`Is Vehicle taken?`, `Debug Camera`. (Five more once belonged here — `Turret Rotation Speed`,
`MG Yaw`, `Clipping Range Min/Max`, `Turret Height Range Clip`, `MGRotation Speed` — all dead
legacy turret variables, since deleted rather than renamed. Deleting beats renaming when the
value has no readers.) A C++ identifier cannot
contain a space, so an exact name match is impossible and RULE 4 cannot be satisfied.
These need a deliberate rename phase of their own (rename in the BP first, let the
editor fix up every node, verify, commit — *then* port). Do not sneak a rename into an
unrelated phase.

### Phase N — Move functions, one function per phase
Order: **leaf functions first** (no callers inside the graph), working upward.
For each: implement in C++ as `BlueprintCallable`, delete the Blueprint version, recompile.
- **Test after each:** the specific behaviour that function drives still works, and the tank
  still drives.

**Never move `Tick`, input bindings, or `ThrottleControl`/`TurningControl` until every
function they call has already been moved and individually verified.**

---

## 5b. Progress Log

| Phase | Status | Result |
|---|---|---|
| 0 — Baseline | ✅ PASS | T90 drove **7921 units / 3s**. Editor clean. |
| 1 — Empty C++ base | ✅ PASS | `ATSTankControllerBase : AWheeledVehiclePawn` created, empty ctor. Builds; editor opens; class registers. |
| 2 — Reparent master BP | ✅ PASS | `BP_TankController_Chaos` reparented. All **21 BP components intact** + 2 native. `Mass=30000`, `AllWheelDrive`, `IdleBrakeInput=1.0` all correct **with zero hand-copying**. T90 drove **6310 units / 3s**. Log clean — zero errors, and no `CreateWidget null class`. |
| 3 — All six tanks | ✅ PASS | All 6 load, correct values, chain = `ATSTankControllerBase → BP_TankController_Chaos → BP_<Tank>_Controller_Chaos`. |
| 4 — Vars: chassis distance accumulators | ✅ PASS | `ChassisDistanceR/L`, `ChassisDeltaDistanceR/L` moved to C++. CDO now reports `owner_class: TSTankControllerBase` for exactly those 4; all other ~150 still `BP_TankController_Chaos_C`. All 16 Get/Set nodes rebound (same node IDs). BP compiles `UpToDate`, 0 errors/warnings. PIE: values accumulate correctly (see below), 0 `Accessed None`, 0 BP runtime errors. |
| 5 — Vars: chassis accel / rot / move scratch | ✅ PASS | `ChassisAccelerationR/L`, `HullZRot`, `ChassisDistanceZRotComponentR/L`, `ChassisDistanceXMoveComponentR/L` moved. All 11 native props present on the CDO; `remove_variable` reports "not found" for the moved names. BP `UpToDate`, 0 errors/warnings, 0 errored BPs. PIE clean. Graceful editor close worked — no recovery modal. |
| 6 — Vars: turret / MG scratch (**first structs**) | ✅ PASS, partly **since deleted** | `MainTurretAndGunRotation`, `TurretRotation`, `MGRotation` (FRotator), `TurretYaw`, `TurretPitch` (double), `TurretBlocking`, `IsTurretRotating` (bool). All 18 native props present; moved names gone from the BP. `TurretRotation` still 10 refs / same node IDs. BP `UpToDate`, 0 errors/warnings, 0 errored BPs. PIE clean. **`TurretRotation`, `MGRotation`, `TurretYaw`, `TurretPitch` were later deleted as dead code** — see *DEAD CODE REMOVED*. |
| 7 — Vars: antenna / UI / misc (**FVector, float, int32**) | ✅ PASS | 14 moved: `HullSpeedWorld`, `HullAccelerationWorldInverted`, `TurretSpeedLocalInverted` (FVector), `CrosshairTraceClamp`, `AimPointCorrectionUI`, `CurrentAmplitudeMultiplierR/L`, `FilletsCompensation`, `HullDeltaXLocation`, `DeltaSeconds`, `CurentRPMRatio`, `TrackSpeedModifier` (double), `ForwardSpeedMPH` (float), `DamageCausedUI` (int32). 32 native props total. All 6 tanks match master on every default. BP `UpToDate`, 0 errors/warnings, 0 errored BPs. PIE clean. **13/14 proven at runtime; `DamageCausedUI` structural only** (see below). |
| 8 — Vars: scratch **ARRAYS** | ✅ PASS | 11 moved: `VibrationOffset_R/L`, `FinalScattering` (TArray&lt;double&gt;), `SplinePointLocation`, `SplinePointPerpendicularVectors`, `AntennaCurrentSpeed` (TArray&lt;FVector&gt;), `CopyPointIndices` (TArray&lt;int32&gt;), `TurretsRotUnstabilized`, `TurretsRotPrevFrame`, `GunsRotUnstabilized`, `GunsRotPrevFrame` (TArray&lt;FRotator&gt;). 43 native props. All lengths match across all 6 tanks. BP `UpToDate`, 0 errors/warnings, 0 errored BPs. |
| 9 — Vars: **REPLICATED** | ✅ PASS — **verified not a regression** | Properties moved correctly: 46 native props, `UPROPERTY(Replicated)` + `DOREPLIFETIME`, lengths correct on all 6 tanks, 0 LogNet warnings. A/B listen-server test (2026-09-04) at `c783cbf` vs current gives **identical** behaviour in both directions, so the port neither broke nor fixed replication. The multiplayer turret faults are PRE-EXISTING — see below. |
| 10 — Vars: object / component **references** | ✅ PASS | 7 moved: `BaseTrackMaterial`, `RightTrackMID`, `LeftTrackMID`, `TracksInstances_R/L`, `TrackPath_L`, `VehicleMovement`. 53 native props. **All 21 SCS components still intact.** BP `UpToDate`, 0 errors/warnings, 0 errored BPs. PIE clean. 6/7 runtime-proven. |
| 11 — Vars: first real tuning values | ❌ **FAILED, REVERTED** | Moving the four `WheelRadius*` lost **19 of 24 per-tank overrides** — every tank fell back to the master default. Reverted; all overrides restored, nothing lost. See below. |
| 11 (retry) — vibration/sagging tuning, **middle path** | ✅ PASS | 9 moved: `SpeedInfluence`, `MaxSpeedInfluence`, `AccelerationInfluence`, `MaxAccelerationInfluence`, `TrackFrequency`, `DecayRate`, `InteractionAmplitudeMultiplier`, `SaggingMaxDistance`, `ProportionalCoefficient`. 62 native props. All 9 verified correct on master **and all 6 tanks** (`problems=0`). BP `UpToDate`, 0 errors/warnings, 0 errored BPs. PIE clean. |
| 12 — **First function move**: `VibrationCalculation` | ✅ PASS | Ported 1:1 to C++. Call site in `PointLocationCalculation` rebound with **every pin intact, 0 orphaned**, node retargeted to `TSTankControllerBase`. Math verified **bit-exact** (delta 0.000e+00) on 3 input pairs. BP `UpToDate`, 0 errors/warnings, PIE clean. |
| 13 — `HullAccelerationDefinition` | ✅ PASS | Ported 1:1. Call site retargeted to `TSTankControllerBase`, all pins intact, 0 orphaned. `HullSpeedWorld` matches the mesh's physics velocity **exactly** (delta 0.0000) at two sample points; inverted acceleration non-zero. BP `UpToDate`, PIE clean. |
| 13a — `SaggingCalculation` | ⛔ BLOCKED, later UNBLOCKED | Parameter shadowed a moved member. Resolved in Phase 15/16. |
| 14 — Tuning w/ **override re-application** + `UpdateTracksMID` | ✅ PASS | Moved `TilingSegmentLength`, `InvertTrackDirection` (both differ per tank), re-applied all overrides, then ported `UpdateTracksMID`. Overrides **survived an editor restart** (`problems=0`). Function math matches to float precision. |
| 15 — **Rename** `SaggingCalculation` param (manual) | ✅ PASS | `HullDeltaXLocation` -> `InHullDeltaXLocation`, done by hand in the editor. Both call sites kept their wiring; signature clean; 0 errored BPs. |
| 16 — `SaggingCalculation` | ✅ PASS | Ported 1:1 as **BlueprintPure**. Call site retargeted, no exec pins added, 0 orphaned. Math **bit-exact** (delta 0.000e+00) across both branches and both clamp boundaries. |
| 17 — `UseGeometricTracks` + `WheelRotationDefinition` | ✅ PASS | Only member the function reads is `UseGeometricTracks` (True on all 6, zero override risk). **All 8 call sites** retargeted, 0 orphaned. Math **bit-exact** across both track modes and both wheel sides. |
| 18 — `Stabilization` + `RecalculateGunAndTurretRotation` | ✅ PASS | Verified live (1 EventGraph call site) before porting. `Stabilization` false on master + all 6 tanks, no override to lose. `RotCorrector` is a BP function-LOCAL, stays a C++ local. All 8 comparisons **bit-exact** across BOTH `Stabilization` branches; turret yaw lands 18.237/21.763 mirrored around the seed of 20.0, proving the sign select. `G[0]` proves ComposeRotators is quaternion composition, not addition (`A+B` would give `(5.0,-3.339,-4.0)`). Call site retargeted, 0 orphaned pins. BP `UpToDate`, PIE `ok:true`. |
| 19 — `SelfCollisionCheck` (needs GOLDEN-VALUE method) | ⬜ next | Last portable function. See the CORRECTION section. |
| — SCS component access pattern | ⬜ blocked | Unlocks 11 more functions. A decision, not a port. |

**Phase 14 — the override re-application procedure, proven end to end.**
The loss happened exactly as predicted, then was recovered:
```
P14PRE     TilingSegmentLength : all six = 70.0   (lost 6)
P14PRE     InvertTrackDirection: five = False     (lost 5)   SUMMARY lost=11
P14APPLY   set per child + save
P14RESTART TilingSegmentLength master=70.0 : T90=69.58 Leo=66.424 M1A2=78.66 Merk=42.56 Proxy=36.7 VK=29.15
P14RESTART InvertTrackDirection master=False: T90..Proxy=True VK=False    problems=0
```
**Verify after an editor RESTART, not just in-session.** In-memory CDO values look correct
immediately after `set_editor_property` whether or not they serialised. Only a restart proves
the override actually reached the `.uasset`.

`UpdateTracksMID` numeric proof — and note it read T90's *overrides* (69.58 / True), not the
master defaults, which is what proves the re-application works functionally:
```
dist=100.0 got=-0.437194586 expected=-0.437194596 delta=1.035e-08 MATCH
dist=250.0 got=-0.592986465 expected=-0.592986490 delta=2.587e-08 MATCH
dist=33.3  got=-0.478585809 expected=-0.478585801 delta=8.952e-09 MATCH
```
Deltas ~1e-8 are float32 rounding in `SetScalarParameterValue`, not a logic difference. Expect
that whenever a value round-trips through a float material parameter; demand 0.000e+00 only
where the whole path is double.

This function only runs when `UseGeometricTracks` is FALSE, and every tank ships True, so it
never executes in a normal PIE session — it was verified by direct invocation.

### Function port blockers — survey before picking the next one
Every remaining small leaf function is blocked on something. Check these before starting:

| Function | Blocker |
|---|---|
| `SaggingCalculation` | param `HullDeltaXLocation` shadows the moved member — needs a BP param rename first |
| `SplineFilletsCompensation` | reads `TrackThickness` (per-tank); moving it would then shadow `WheelRotationDefinition`'s param |
| `FindSplineXClosestPoint` | reads `TrackPath_R`, a Blueprint **SCS component** — C++ cannot name it without a runtime lookup shim |
| `WheelRotationDefinition` | ~~blocked~~ PORTED in Phase 17 - its tuning arrives as parameters, so nothing needed moving |

**Functions that touch SCS components need a decision, not a port.** Components stay in the
Blueprint under RULE 1, so C++ reaching one means either a name/class lookup at runtime or a
`TObjectPtr` cached at BeginPlay. Both are new code, not a 1:1 move — decide the pattern
deliberately before porting any such function.

**Phase 13 runtime proof:**
```
A  hull=(158.612,-7.397,-86.710)  meshV=(158.612,-7.397,-86.710)  accel=(-40.514,19.023,52.201)
B  hull=(461.341,-0.958,11.194)   meshV=(461.341,-0.958,11.194)   |hull-meshV|=0.0000
```
Ordering matters in this one: the subtraction reads the PREVIOUS frame's `HullSpeedWorld`
before it is reassigned. Swapping those two lines would make the result permanently zero and
still compile, still drive, still look fine.

## ⛔ BLOCKER — a function parameter cannot shadow a moved member

UHT rejects this outright:
```
Error: Function parameter: 'HullDeltaXLocation' cannot be defined in 'SaggingCalculation'
as it is already defined in scope 'ATSTankControllerBase' (shadowing is not allowed)
```
Blueprint happily allows a function parameter with the same name as a member variable. **C++
does not.** `SaggingCalculation` takes a parameter `HullDeltaXLocation`, and the member of that
name moved to C++ in Phase 7, so the function can no longer be ported as-is.

There is no clean escape: the parameter name cannot change (call-site pins rebind by name) and
the member name cannot change (RULE 4). `UPARAM(DisplayName=...)` only alters the label, not the
internal pin name used for reconnection, so it would orphan the link.

**Moving a variable can retroactively block a function port.** Before moving any variable,
check whether a Blueprint function takes a parameter of the same name. Known collisions:

| Function | Parameter | Collides with | Status |
|---|---|---|---|
| `SaggingCalculation` | `HullDeltaXLocation` | member moved in Phase 7 | already blocked |
| `WheelRotationDefinition` | `TrackThickness`, `WheelSpeedCorrectionUV` | still BP variables | ported anyway (Phase 17) - but NEVER move these two, it would break it |

To unblock one, rename the parameter in the Blueprint first, let the editor fix up the call
sites, verify, commit — then port. Treat that as its own phase; do not fold a rename into a function move.

### ⚠ `set_function_params` CANNOT rename — it is purely ADDITIVE
Do not reach for it to rename a parameter. Passing the full intended signature does not
replace the old one; it **appends a second copy of every parameter**, suffixing name clashes:
```
inputs: SaggingDegree, HullDeltaXLocation, ChassisDeltaDistance, ChassisLocked,
        SaggingDegree1, InHullDeltaXLocation, ChassisDeltaDistance1, ChassisLocked1
outputs: SaggingDegreeNew, SaggingDegreeNew1
```
There is no rename-parameter action in the MCP surface (`rename_variable` and `rename_function`
exist; a parameter equivalent does not). **Renaming a function parameter is a manual edit in
the Blueprint editor's function Details panel**, where UE renames the pins and fixes up every
call site itself.

**Recovery, if it happens anyway:** the damage is in memory only until something saves. Check
`git status` first — if the `.uasset` is unmodified, nothing has been lost. Then either close
the editor and answer *Don't Save*, or revert in place with:
```python
pkg = unreal.load_package('/Game/.../BP_TankController_Chaos')
unreal.EditorLoadingAndSavingUtils.reload_packages([pkg])
```
That raises a modal ("Would you like to reload these assets? This will revert any changes") —
answer **Yes**, which blocks MCP until a human clicks it. Verified full recovery afterwards:
signature back to 4 inputs / 1 output, both call sites pin-for-pin identical to the captured
baseline, BP `UpToDate`, 0 errored BPs, and PIE healthy with the Phase 14 overrides intact
(`sagR=1.0000 tile=69.58 invert=True`).

**Capture the baseline before touching a signature.** `get_node_details` on every call site,
recording each pin and the variable feeding it, is what made it possible to prove the recovery
was exact rather than merely plausible.

### Renaming a Blueprint function parameter — it works, and it is safe
Done manually in the function's Details panel (there is no MCP action for it). The editor
renames the pin on every call site and keeps the connection. Verified on
`SaggingCalculation`: both EventGraph call sites showed the pin as `InHullDeltaXLocation`
still wired to the same `HullDeltaXLocation` variable, every other pin unchanged, nothing
orphaned, 0 errored Blueprints. Capture each call site's pins first so the check is a
comparison, not a guess.

**Phase 16 numeric proof** — chosen to hit BOTH branches and BOTH clamp limits, not just a
happy path:
```
sd=0.5 hx=2.0  cd=0.0  lock=True   got=0.400000000 expected=0.400000000 delta=0.000e+00
sd=0.2 hx=0.0  cd=3.0  lock=False  got=0.350000000 expected=0.350000000 delta=0.000e+00
sd=0.9 hx=-4.0 cd=0.0  lock=True   got=1.000000000 expected=1.000000000 delta=0.000e+00  (upper clamp)
sd=0.1 hx=0.0  cd=-5.0 lock=False  got=0.000000000 expected=0.000000000 delta=0.000e+00  (lower clamp)
```
A single mid-range input would have passed even if the clamp or the branch select were wrong.
Pick inputs that exercise every path through the function.

### A function taking its tuning as PARAMETERS needs none of it moved
`WheelRotationDefinition` looked like the worst case — it uses `WheelRadius`,
`TrackThickness`, `WheelSpeedCorrectionUV` and four start angles, all per-tank values. It was
not. Every one of those arrives as a **parameter**, passed in by the Blueprint callers. The
only member it reads is `UseGeometricTracks`, which is True on all six tanks.

So it ported with **zero** tuning values moved and **zero** override risk. Read the graph
before assuming a function needs its tuning migrated — check whether the values come from
`FunctionEntry` pins or from `VariableGet` nodes.

### ⛔ PERMANENT — never move `TrackThickness` or `WheelSpeedCorrectionUV`
They are parameter names of `WheelRotationDefinition`. Moving either to C++ would make the
parameter shadow a `UPROPERTY`, which UHT rejects, retroactively breaking this port (the
Phase 13 failure mode). C++ never needs them — they arrive as parameters. Same reasoning
applies to any per-tank value that is only ever passed in.

**Phase 17 numeric proof** — four combinations, cross-checking each other:
```
geo=True  left=True   got=-660.919818774 expected=-660.919818774 delta=0.000e+00
geo=True  left=False  got=-659.919818774 expected=-659.919818774 delta=0.000e+00
geo=False left=True   got=-665.094443949 expected=-665.094443949 delta=0.000e+00
geo=False left=False  got=-664.094443949 expected=-664.094443949 delta=0.000e+00
live wheels FR=-501.520 MR=-344.406 RR=-445.058
```
Left/right differ by exactly 1.0, matching the `lg=1, rg=2` inputs, so the side select is
right. Geo/UV differ by the speed correction's effect on circumference, so that branch is
right too. Distinct live wheel values show the 8 call sites feeding different per-tank radii
through the C++ function. Test `UseGeometricTracks` both ways by setting it on the live PIE
actor and restoring it afterwards — the UV path is otherwise never exercised.

**Phase 12 numeric proof** — computed independently in Python and compared:
```
amp=2.0 phase=30.0   got=0.412081776  expected=0.412081776  delta=0.000e+00 MATCH
amp=0.5 phase=123.4  got=-0.494520258 expected=-0.494520258 delta=0.000e+00 MATCH
amp=3.0 phase=0.0    got=2.003125004  expected=2.003125004  delta=0.000e+00 MATCH
```

## Moving a FUNCTION — the procedure

Functions behave differently from variables in one important way: **a name clash is a hard
compile error, not an auto-resolve.** With the C++ function added and the Blueprint one still
present you get:
```
The function name in node VibrationCalculation is already used
Overriden function is not compatible with the parent function. Check flags: Exec, Final, Static.
```
That is the expected intermediate state. Add C++ first, then `remove_function` on the
Blueprint, then recompile — the call sites rebind by name. Doing it in this order keeps the
call site resolving to *something* the whole time.

### Signature must match exactly — pins rebind by NAME
Read the Blueprint signature with `get_function_signature` before writing any C++.
- **`is_pure: false` → `BlueprintCallable`, never `BlueprintPure`.** An impure Blueprint
  function's call site wires exec pins; a pure C++ function has none, orphaning them.
- **A NAMED output must become an out-parameter with that exact name.** `VibrationCalculation`
  outputs `VibrationOffset`; returning a `double` from C++ produces a pin called `ReturnValue`
  and silently orphans the link. `void F(..., double& VibrationOffset)` preserves it.
- **Parameter names must match** — pins are matched by name, not position.
- Carry `Category` and the description across (`meta=(ToolTip=...)`) so the node reads the same.

### Verify a moved function numerically, not just "it ran"
Call the C++ function directly in PIE with several input pairs and compare against the formula
computed independently. `delta=0.000e+00` proves a 1:1 port; "the tank still drives" does not.
Then confirm the downstream consumers still populate.

Confirming the rebind: `get_node_details` on the call site should show the title retargeted
(`Target is TSTank Controller Base`) and every pin with `is_orphaned: false`.

Kismet equivalents: `DegSin(A)` is `FMath::Sin(FMath::DegreesToRadians(A))` — same operation,
not a re-derivation. Check the Kismet source before substituting any math node.

**Phase 11 retry runtime proof** — consumers show the values are actually read:
```
START  trackFreq=1500.0 sagMax=20.0 propCoef=5.0 decay=0.30 maxSpdInf=0.60
END    consumers: sagR=1.0000 sagL=1.0000 ampR=0.6000 vibR0=-0.9278 vibLen=19
```
`ampR` = exactly 0.6000, the `MaxSpeedInfluence` cap. `sagR`/`sagL` = 1.0000 is
`SaggingCalculation`'s `Clamp(0,1)` result from dividing by `SaggingMaxDistance`.

### THE MIDDLE PATH — the rule for tuning values from here on
Move a tuning value **only** when a C++ function will actually read it, and prefer values with
no override risk.

- **Safe to move:** the 26 values identical across all six tanks. No override exists to lose.
- **Move only when a function port needs it:** the 18 values that genuinely differ. Each one
  requires the full re-application procedure above — record, move with an `Edit` specifier,
  re-apply all six overrides, re-verify. Do not batch these for convenience.
- **Never move "for completeness."** A tuning value sitting in the Blueprint costs nothing.
  Moving one buys nothing unless C++ reads it, and it carries permanent regression risk.

Run the six-tank comparison on **every** tuning-value phase, even when the values are known to
be identical. It is cheap, and it is the only thing that catches the failure mode.

## ⚠ THE BIG ONE — child Blueprint overrides do NOT survive the move

**Moving a variable to C++ preserves graph node references but DISCARDS every per-tank
Class Defaults override.** The override is serialised against the Blueprint-owned property;
once that property becomes native, the child's stored value no longer resolves and the tank
silently falls back to the C++ default.

This is precisely what killed attempt 1. Phases 4–10 never exposed it because every value
moved there was identical across all six tanks — the fallback happened to equal the override.
The first phase to move genuinely differing values reproduced it immediately:
```
WheelRadiusFront  master=22.5
   T90 expect=23.19 got=22.5 *** LOST ***      Proxy expect=47.3 got=22.5 *** LOST ***
SUMMARY lost_overrides=19
```

**Recovery is easy IF you have not saved anything.** The loss happens in memory at load; the
child `.uasset` files on disk keep their overrides. `git checkout` the header, rebuild, and the
values return (verified: `still_lost=0`). **Never save a child Blueprint after a failed
tuning-value move — that bakes the loss in permanently.**

### Required procedure for ANY variable with per-tank overrides
1. **Before moving**, dump the value from all six tanks and record it (table below).
2. Move the property to C++ with an `Edit` specifier — `EditDefaultsOnly` when the Blueprint
   has Instance Editable unchecked, `EditAnywhere` when it is checked. Without an `Edit`
   specifier the property will not even appear in Class Defaults, so it can never be re-tuned.
3. **Re-apply every override explicitly** (`set_cdo_property` per child Blueprint) and save the
   six child Blueprints.
4. Re-verify all six against the recorded table. Only then is the phase green.

Step 3 is not optional and is easy to forget, because the tank still drives perfectly with the
wrong values.

### Per-tank override table — 18 variables that genuinely differ
Captured before the Phase 11 attempt. **This is the recovery data. Do not delete it.**

| Variable | master | T90 | Leo | M1A2 | Merk | Proxy | VK |
|---|---|---|---|---|---|---|---|
| MaxSpeedKMH | 60 | 60 | 69 | 69 | 65 | 60 | 60 |
| MaxTurningSpeed | 45 | 45 | 45 | 45 | 45 | 45 | 36 |
| TracksAmount | 50 | 78 | 80 | 75 | 120 | 70 | 68 |
| WheelRadiusFront | 22.5 | 23.19 | 22.716 | 29.94 | 23.2 | 47.3 | 32.7 |
| WheelRadiusMiddle | 38 | 36.02 | 30.583 | 29.94 | 28.79 | 36.25 | 45.35 |
| WheelRadiusRear | 22 | 27.16 | 23.6 | 27.1 | 24.64 | 28.1 | 32.35 |
| WheelRadiusAccessory | 11 | 11 | 11 | 11 | 11 | 11 | 12 |
| TilingSegmentLength | 70 | 69.58 | 66.424 | 78.66 | 42.56 | 36.7 | 29.15 |
| TrackThickness | 4 | 7.26 | 5.6976 | 7.86 | 3.97 | 6.0 | 6.6 |
| WheelStartingAngleGeoR | 0 | -12 | -2 | 9 | 5 | 2 | 0 |
| WheelStartingAngleGeoL | 0 | -12 | 0 | 3 | 10 | 2 | 0 |
| WheelStartingAngleUVR | 0 | -14 | -5 | 3 | -3 | 0 | 0 |
| WheelStartingAngleUVL | 0 | -14 | -3 | 6 | -2 | 0 | 0 |
| WheelSpeedCorrectionUV | 0 | -0.4 | -5.6 | 0.2 | 0.3 | 0.0 | 0.1 |
| MiddleWheelXOffset | 0 | 0 | 16 | -23.561 | -14 | 0 | 0 |
| InvertTrackDirection | **False** | True | True | True | True | True | False |
| TargetArmLengthMax | 2000 | 2000 | 2000 | 2000 | 2000 | 2000 | 1800 |
| TargetArmLengthMin | 1000 | 1000 | 1000 | 1000 | 1000 | 1000 | 800 |

Note `InvertTrackDirection`: the master default is **False** but five of six tanks override it to
True. Losing that flips the track direction on five tanks.

These 26 are identical across all six tanks and therefore carry no override risk:
`SaggingMaxDistance=20, ProportionalCoefficient=5, SpeedInfluence=0.3, MaxSpeedInfluence=0.6,
AccelerationInfluence=0.2, MaxAccelerationInfluence=1.0, TrackFrequency=1500, DecayRate=0.3,
InteractionAmplitudeMultiplier=0.4, FrontSagTangent=0, RearSagTangent=0, GeoTracksFlipR/L=False,
ReverseTurnInReverse=False, UseGeometricTracks=True, HealthMax=100, LightIntensityLights=25,
EmissiveIntensityLights=5, GravityForce=-70, SniperCameraMaxZoom=4, CameraZoomStep=1000,
SniperLagSpeed=30, TargetArmLengthArcade_2=-60, EditorSplinePreview=True, PhysWheelsAmount=0,
UniqueTrackMeshesAmount=0`

**Phase 10 runtime proof:**
```
midR=MID_MI_Tank_T90_Track_1   midL=MID_MI_Tank_T90_Track_0
instR=4  instL=4
splineL=NODE_AddSplineComponent-0   move=VehicleMovementComp   baseMat=NULL
```
`TrackPath_L` resolving to a `NODE_AddSplineComponent` confirms it is a runtime-created
reference, not an SCS component. `VehicleMovement` correctly caches the native
`VehicleMovementComp`.

### A reference VARIABLE is not a COMPONENT — check before moving
Run `get_components` first. In this Blueprint the real SCS components include `TrackPath_R`
and `BP_TankWeapon`; the similarly-named `TrackPath_L` and `VehicleMovement` are plain
variables holding runtime-assigned references. Note the asymmetry: **`TrackPath_R` is a
component, `TrackPath_L` is a variable.** Moving a variable is fine; declaring a `UPROPERTY`
over an SCS component is the RULE 1 mistake that sank attempt 1. Re-run `get_components` after
the move and confirm the count is still 21.

Use `TObjectPtr<T>`, not raw `T*`: UHT runs with `-WarningsAsErrors` and raw object pointers in
a `UPROPERTY` can be reported as a member-pointer violation.

### NOT PORTABLE — the turret/ballistics chain and the vendor structs (surveyed 2026-09-04)
The same "C++ cannot name a Blueprint-generated type" rule that parks `HUD` and `Crosshair` also
parks a large part of the turret system. Surveyed against the live Blueprint, not inferred:

| Thing | Blocking type | Kind |
|---|---|---|
| `UpdateTurretRotation`, `UpdateGunRotation`, `BallisticsCalculation` | param `GunProjectile` is `class:BP_ProjectileMaster_C` | Blueprint class asset |
| `TurretsAndGunsRotCalculation` | calls all three above | transitive |
| `AntennaCalculation` | param `AntennaParameters` is `array:struct:S_Antenna` | Blueprint UserDefinedStruct |
| `CamoVariations`, `TankSplineAnim`, `AntennaParameters` (variables) | `S_CamoOptions` / `S_TankSplineAnim` / `S_Antenna` | Blueprint UserDefinedStruct |

`BP_ProjectileMaster` lives at `/Game/YI_TankCollection/Blueprint/Master/Projectiles/`, and the
three structs at `/Game/YI_TankCollection/Blueprint/Master/Data/`. All are `.uasset`, so none can
appear in a C++ signature.

**`Source/Tank_Sim_V2/Tank/TSTankControllerChaosTypes.h` does NOT unblock this.** It declares
native mirrors (`FTSTankSplineAnim`, `FTSAntenna`, `FTSCamoOptions`) written during the framework
scaffold, but a mirror is a *different type*. Retyping the Blueprint variable from `S_Antenna` to
`FTSAntenna` is a type change, which orphans the stored data and breaks every node — RULE 4. The
header is currently unused by the port. Unblocking these for real means a deliberate struct
migration phase (retype, re-author the per-tank data, re-verify all six), which buys nothing
unless C++ needs to read them.

**Consequence for planning: the turret aiming chain stays in Blueprint permanently**, including
the `TurretsAndGunsRotCalculation` graph that carries the multiplayer aim fix.

### Remaining port surface (measured 2026-09-04, after the dead-code cleanup)
32 functions (~1,656 nodes), 5 macros (202 nodes), EventGraph (742 nodes), 73 variables.

| Category | Count | Nodes | Note |
|---|---|---|---|
| Permanently Blueprint-resident | 11 fns | ~533 | 3 projectile + 7 UI + `TurretsAndGunsRotCalculation` |
| Blocked on the SCS-component decision | 11 fns | ~570 | all the spline / ISM / camera functions |
| Open with no new decision needed | **1 fn** | 67 | `SelfCollisionCheck` only — see the correction below |
| Last by rule | 3 fns + EventGraph | ~818 | `ThrottleControl`, `TurningControl`, `UserConstructionScript`, Tick/input |

### ⚠ CORRECTION — classify a function by its BODY, not its signature
The "open" column above originally listed **five** functions. Three of them were wrong. They
were classified from `get_functions` output — parameter and return types — which says nothing
about what the body reads. Inspecting the graphs moved three straight into the blocked groups:

| Function | Looked like | Actually reads | Verdict |
|---|---|---|---|
| `CameraPitchLimit` | `double` in, `double` out — pure math | `BP_TankWeapon` (`BP_TankWeapon_C`), its `Weapons` array of `S_Weapon`, and the `Camera` SCS component | NOT PORTABLE |
| `ScatteringCalculation` | no params at all | `BP_TankWeapon` | NOT PORTABLE |
| `ChassisDistanceDefinition` | no params at all | `TrackPath_R`, an SCS component | blocked on the SCS decision |

`CameraPitchLimit` is the cautionary one: a `double`-in/`double`-out signature is exactly what a
safe leaf function looks like, and its body reaches two Blueprint-generated types and a
component.

**Cheap way to check before committing to a port:** `search_nodes` for the known blocking
symbols across the whole Blueprint and read off which graphs appear — one query per symbol
covers every function at once, far cheaper than dumping each graph:
```
search_nodes  query: "BP_TankWeapon"      -> 26 hits across 11 graphs
search_nodes  query: "TrackPath"          -> 52 hits across 12 graphs
```
Anything naming `BP_TankWeapon`, `BP_ProjectileMaster`, `HUD`, `Crosshair`, an `S_*` struct, or
an SCS component is not a candidate.

**Net remaining after Phase 18: exactly ONE portable function, `SelfCollisionCheck`.**
It is clean — only the native pawn `Mesh`, three line traces and native math — and is called
once, from `UpdateGunRotation` (itself permanently Blueprint-resident, which is fine; a C++ leaf
under a Blueprint caller is the established pattern).

**It needs a different verification method.** Every port from Phase 12 on was proved by a
bit-exact numeric diff against a formula recomputed independently. `SelfCollisionCheck`'s output
depends on three `LineTraceByChannel` calls against world geometry, so there is no closed-form
expectation to diff against. Use golden values instead:
1. **Before** touching anything, pin the tank's transform explicitly and call the *Blueprint*
   version across several input sets; record the outputs.
2. Port, rebuild, re-pin the identical transform, call the C++ version with the identical inputs.
3. Diff against the recorded goldens.
Capturing the goldens first is not optional — once `remove_function` runs, the reference
implementation is gone and there is nothing left to compare against.

**The port cannot reach 100%, and should not.** About a third of the remaining function nodes are
permanently Blueprint-owned by design — that is the core principle working, not a shortfall.

### NOT PORTABLE — variables typed as Blueprint-generated classes
`HUD` (`W_MainHUD_C`), `Crosshair` (`W_Crosshair_C`) and `BPC_TankWeapon` (`BP_TankWeapon_C`)
cannot be moved. C++ cannot name a Blueprint-generated type, and widening them to a native base
(`UUserWidget*`, `UActorComponent*`) would break every graph node that calls a Blueprint-only
member on them — a type change also violates RULE 4. **These stay in the Blueprint permanently.**
That is the correct end state, not a deferral: C++ owns logic, Blueprint owns data and the
Blueprint-typed references.

### `BaseTrackMaterial` — read twice, never written
2 reads inside the `Set Track Dynamic Material` macro, **0 writes anywhere**, and `None` on the
master and all six tanks. NULL at runtime is therefore correct and unchanged, not a broken
rebind — verified structurally only, like `DamageCausedUI` in Phase 7. A pre-existing oddity
(the macro reads something nothing sets, and the MIDs are still created), left alone.

**Phase 9 runtime proof:**
```
START  turretsRot=10 gunsRot=10 rep=(0.000,0.000,0.000)  t0=(0.000,0.000,0.000)
END    turretsRot=10 gunsRot=10 rep=(4.098,-0.036,-0.004)
END    turrets0=(0.000,-0.602,0.000)  guns0=(5.688,0.000,0.000)
```

### Moving a replicated Blueprint variable — the least visible failure in the port
A Blueprint variable with "Replicated" ticked **silently stops replicating** the moment it
becomes a C++ property unless BOTH exist:
1. the `Replicated` specifier on the `UPROPERTY`, and
2. a `DOREPLIFETIME` entry in `GetLifetimeReplicatedProps`.

Miss either and the value still reads and writes perfectly in a single-player PIE session.
Nothing in the standard test catches it. **Any future phase that moves a replicated variable
must add its `DOREPLIFETIME` line in the same commit.**

Check for RepNotify before choosing the specifier: search the Blueprint for `OnRep` nodes and
`OnRep_*` functions. This project has none, so all three are plain `Replicated`, never
`ReplicatedUsing`.

## 🧹 DEAD CODE REMOVED (2026-09-04)
The legacy turret subsystem is gone. It was never called, and it actively misled this session:
`Rep_ControlRotation`'s name suggested it drove the turret, so a whole multiplayer fix was built
around it before its only two readers turned out to be dead functions.

Removed:
| Thing | Kind | Why |
|---|---|---|
| `UpdateTurretRotation_Old` (79 nodes) | BP function | never called |
| `UpdateMachineGunRotation_Old` (22 nodes) | BP function | never called |
| `ReplicateControlRotation` (17 nodes) + its Tick call | BP function | wrote `Rep_ControlRotation` every frame; nothing read it |
| `Rep_ControlRotation` | C++ (was **Replicated**) | 0 readers; was costing bandwidth for nothing |
| `TurretRotation`, `MGRotation`, `TurretYaw`, `TurretPitch` | C++ | orphaned once the `_Old` functions went |
| 10 `Old`-category variables | BP | `Turret Rotation Speed`, `Clipping Range Min/Max`, `TurretVerticalRange`, `Turret Height Range Clip`, `Turret Rotation Speed Vertical`, `MGVerticalRange`, `MGRotation Speed`, `MG Pitch`, `MG Yaw` |

Native property count: **65 → 60**. The `DOREPLIFETIME` for `Rep_ControlRotation` went too, so only
`TurretsRot` and `GunsRot` remain replicated.

### How the deletion was made safe
1. **Project-wide grep of the `.uasset` binaries**, not just per-asset node searches — Blueprint
   function and variable names appear as strings in the packages, so
   `grep -rl "UpdateTurretRotation_Old" --include="*.uasset"` finds every referencing asset. Both
   names appeared **only** in the master Blueprint.
2. **Followed the cascade.** Deleting the two `_Old` functions dropped `Rep_ControlRotation` to
   0 reads, which made `ReplicateControlRotation` dead, which orphaned four more C++ properties.
   Re-run `find_variable_references` after each removal rather than assuming the first pass found
   everything.
3. **`TurretVerticalRange` hits all six AnimBPs** in a grep — that is the AnimBP's OWN same-named
   variable, not a reference to the pawn's. Separate storage, separate class. Do not read a
   name collision as a dependency.

Verified after: BP `UpToDate`, 0 errored BPs, PIE clean (0 `Accessed None`, 0 index warnings),
tank drove 379 units, turret/wheels/sagging all still working.

## 🗺 END-TO-END LOGIC FLOW (traced from code 2026-09-07 — read this before touching the framework)

Menu → session → travel → lobby → assignment → spawn → gameplay request. Traced from the source,
not inferred. **Section 8 below records a genuine contradiction between two parallel workstreams —
read it before building on either side.**

### 1. Boot and the menu map
```
GameDefaultMap / EditorStartupMap = /Game/TankSimulation/Maps/MainMenu
MainMenu World Settings GameMode  = /Game/TankSimulation/Blueprints/BP_TSGameMode
```
`UTSUISubsystem` hardcodes `MenuMapNames = { "MainMenu" }` and treats widgets whose class name
contains Login / SessionBrowser / SessionList / MainMenu / HostMenu as menu widgets. On every
**non-menu** map load it sweeps them from the viewport, because a widget owned by the
GameInstance survives `ServerTravel` and would otherwise sit on top of the game.

**Nothing in C++ creates the menu widgets.** The only `CreateWidget` calls are the role-debug and
team/role-selection panels. `BP_TSGameMode` creates none. So `WBP_Login` / `WBP_SessionBrowser`
must be spawned by the MainMenu **level Blueprint** (or a menu GameMode). That is the Blueprint
side of the boundary, and it is where to look when "the menu shows nothing".

### 2. Hosting
```
WBP_SessionBrowser
  → UTSGameInstance::CreateSession(MaxPlayers, bIsLAN, bIsPresence, MapPath)   [BlueprintCallable]
  → UTSSessionSubsystem::CreateSession
       destroys a stale NAME_GameSession first and re-enters from the destroy callback
       generates a lobby code, broadcasts OnLobbyCodeGenerated
  → HandleCreateSessionComplete(success)
  → World->ServerTravel("<MapPath>?listen?LobbyCode=<code>")
```
`MapPath` empty falls back to **`/Game/TankSimulation/Maps/WarZone`**. `bUseLobbiesIfAvailable` is
deliberately false — `OnlineSubsystemNull` has no lobby backend.

### 3. Joining
```
FindSessions → HandleFindSessionsComplete → results list
JoinSession(index) → HandleJoinSessionComplete
  → GetResolvedConnectString → PC->ClientTravel(ConnectString, TRAVEL_Absolute)
```

### 4. Arrival: who becomes host
`ATSGameMode::PostLogin` designates the host **before** `Super::PostLogin`, because the parent
restarts the player and `GetDefaultPawnClassForController` reads `bIsHost` to choose the pawn.
Designating afterwards spawns the host into the wrong pawn.

| NetMode | Host designation |
|---|---|
| Listen server | the local controller (whoever created the session) |
| Dedicated server | first to connect, unless `bFirstPlayerHostsOnDedicatedServer` is off |
| **Standalone** | **nobody** — by design, or the lone player would spawn into the free camera with no way to play |

That last row explains a confusing observation: in PIE the play mode has been left on **listen
server**, so player 0 comes up as host with `TSHostCameraPawn`. A true standalone run has no host
at all and the player is an ordinary crew candidate. **Check the net mode before drawing any
conclusion about host behaviour.**

The host holds **no team, no crew role and no seat** — `TryAssignTeam` and `TryAssignRole` both
refuse `PS->IsHost()` outright — and possesses `HostCameraPawnClass` (`ATSHostCameraPawn`).

### 5. Team and role assignment — TWO paths
```
self-serve : ServerRequestTeamChange / ServerRequestRoleChange        (a player picks their own)
host-driven: ServerHostAssignPlayerToTeam / ...ToRole / ...ClearPlayerAssignment
```
Both land on `ATSGameMode::TryAssignTeam` / `TryAssignRole`. The host RPCs **re-check
`IsMatchHost()` server-side** — a Server RPC's `HasAuthority()` is trivially true, so without that
any client could assign anyone.

`bAutoShowSelectionUI` defaults to **false**: the intended lobby is host-driven, so no selection
panel pops up on its own. Set it true for a free-for-all lobby.

### 6. Tank spawn
One tank per team, created lazily by `GetOrSpawnTankForTeam` on first team/role assignment.
`bPreSpawnTeamTanks` (default off) instead spawns `NumTeamsToPreSpawn` tanks at BeginPlay.

Spawn transform comes from an actor **tagged** `TSTeamSpawn_TeamA`..`TeamD`, or a `PlayerStart`
with that `PlayerStartTag`. With neither it logs a warning and falls back to a world-origin offset
at **z=200** — which is a long drop and will leave the tank bouncing on a low floor.

### 7. A gameplay request, end to end
```
VR pawn input → ATSTankPlayerController::Server<Action>          (the PC owns a NetConnection)
              → PS->GetAssignedTank() → FindComponentByClass<U...Component>
              → Try<Action>(Requester, ...)
                   factor 1: FTSPermissions::HasFullAccess(role, capability)
                   factor 2: Crew->HasAccess(Requester, RequiredRole)   ← seat ON THIS TANK
              → writes replicated state → OnRep_ → ITSTankInterface::Execute_BP_<Action>
              → Blueprint does the tank-specific work
```
**Every Server RPC lives on the PlayerController**, never on the tank, because a Server RPC is
silently dropped unless the calling client owns the actor.

### 8. ⚠ A DORMANT possession path (originally mis-recorded here as a live contradiction)

Two parallel workstreams disagree, and the code currently contains both answers.

**`ATSGameMode::HandlePlayerReadyToSpawn` possesses the tank:**
```cpp
APawn* Tank = GetOrSpawnTankForTeam(TeamId);
if (Tank) { PlayerController->Possess(Tank); }
```

**The three-crew design says nobody possesses it.** A Pawn has exactly one Controller, so
possession cannot give three crew members a tank each; they possess their own `ATSVRPawn` and
reach the tank through validated RPCs. Built on that assumption:
- `IsLocalGunnerOfThisTank()`, added precisely because `IsLocallyControlled()` is false on an
  unpossessed tank
- `bRequiresControllerForInputs=False` on all six tanks, because Chaos discards input when there
  is no controller
- crew seat attachment in `ATSVRPawn::UpdateCrewStationAttachment`

Both cannot be right. Under the possession path only one crew member gets the tank and the other
two have no pawn relationship to it; under the three-crew path `HandlePlayerReadyToSpawn` should
seat the player rather than possess.

**CORRECTION after tracing further: the possession path is currently UNREACHABLE, so the two
models are not actually fighting.** `HandlePlayerReadyToSpawn` returns immediately when the
controller already has a pawn:
```cpp
if (!PlayerController || PlayerController->GetPawn()) { return; }
```
`AGameModeBase::PostLogin` has already restarted the player into `DefaultPawnClass`
(`BP_TSVRPawn`), so a PlayerController always has a pawn by then and the `Possess(Tank)` line
never runs. `ReadyToSpawn` is BlueprintCallable and nothing calls it yet either.

Measured confirmation: the tank reports `controller=None, locallyControlled=False` at runtime,
host players get `TSHostCameraPawn` and everyone else `BP_TSVRPawn`.

So **the VR-pawn model is the one executing**, and the three-crew work built on it is consistent
with reality. The possession code is a leftover that would only fire for a pawnless controller.
Decide deliberately whether to delete it or wire `ReadyToSpawn` into the lobby flow — but it is
dormant, not a live conflict. The turret predicates are additive
(`HasAuthority() || IsLocallyControlled() || IsLocalGunnerOfThisTank()`) and hold either way.

### 9. Known holes at the time of tracing
- `MainMenu.umap` and `WarZone.umap` are **untracked** — `.gitignore` line 86 excludes
  `/Content/TankSimulation/Maps`. Only `M_FrameworkTest` is tracked. A fresh clone gets no menu
  map and cannot launch.
- `BP_TeamMatchGameMode` is referenced by nothing.
- `UTSRoleDefinition` (`DA_Role_Driver/Gunner/Commander`) is referenced by no C++ at all.
- `UTSVoiceSubsystem` logs `no IVoiceChat implementation is loaded - voice will be a no-op`.
- Standalone logs `Using CommonUI without a CommonGameViewportClient derived game viewport client`
  — CommonUI input routing will misbehave until the viewport client class is set.

### 10. Running the game standalone from Git Bash
A `/Game/...` argument gets rewritten by MSYS path translation into
`C:/Program Files/Git/Game/...` and the map load fails. Prefix the command with
`MSYS_NO_PATHCONV=1`:
```bash
MSYS_NO_PATHCONV=1 "C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" \
  "C:\Projects\Tank_Sim_V2\Tank_Sim_V2.uproject" "/Game/TankSimulation/Maps/MainMenu" \
  -game -windowed -resx=1280 -resy=720 -log -abslog="...\Saved\Logs\Standalone.log"
```

---

## Unreal multiplayer — the model, and why this tank's turret does not replicate
Researched from Epic's docs (links at the end of this section). Read this before touching
anything networked in this project.

### The model in four rules
1. **The server is authoritative.** It holds the one true game state. Clients own remote proxies
   of Pawns and ask the server to do things.
2. **Property replication flows SERVER → CLIENT ONLY.** A client writing a replicated property
   changes only its own copy; the value never travels upward and is overwritten on the next
   update from the server. There is no such thing as "replicating up".
3. **Client → server is done with a Server RPC.** That is the only upward channel.
4. **Prefer replicated properties over RPCs for state**, and RepNotify over extra RPCs. RPCs are
   for transient events. State that changes constantly should be a replicated property, driven
   by the server.

### Server RPC rules that bite
- **The calling client must OWN the actor.** A Pawn possessed by that client's PlayerController
  is owned. If the actor is not owned by the caller's connection, **the RPC is silently dropped
  and never executes anywhere** — no error at the call site. Symptom: "the call does nothing".
- **Unreliable by default, and correct for per-frame data.** Reliable RPCs cost bandwidth and
  suspend later RPCs until acknowledged; a reliable RPC fired every frame can disconnect the
  client. Aim data every frame must be **Unreliable**.
- `WithValidation` is a trust-and-verify hook; a failed validation **disconnects** the caller.
- A `UFUNCTION(Server, ...)` called from a Blueprint graph must ALSO be `BlueprintCallable`, or
  the graph refuses it with *"should not be called from a Blueprint"*.

### ⚠ THE ONE THAT EXPLAINS THIS PROJECT
**Control rotation does NOT replicate automatically for a `Pawn`.** It does for `Character`,
which is why most tutorials "just work" and this does not. `BP_TankController_Chaos` derives from
`AWheeledVehiclePawn` → `APawn`, so **the server has no idea where a client is aiming.** The
engine replicates view *pitch* only (`RemoteViewPitch`), and this project has
`bUseControllerRotationYaw = false`, so yaw never arrives either.

Epic's own answer for Pawns is manual: on tick, read the aim on the owning client, send it to the
server with a Server RPC, and store it in a replicated property on the server — which then
replicates back down to everyone. Do not multicast it.

### How that maps to this tank (measured, not assumed)
The turret is **not** driven by control rotation. `TurretsAndGunsRotCalculation` aims by
**line-tracing from the Camera component**:
```
Get Camera -> GetWorldLocation + GetForwardVector -> LineTraceByChannel
           -> TargetPoint (local var) -> StabilizingRotation
           -> UpdateTurretRotation / UpdateGunRotation -> TurretsRot / GunsRot
```
`Rep_ControlRotation` was **vestigial** — its only readers were `UpdateTurretRotation_Old` and
`UpdateMachineGunRotation_Old`, both dead code. Replicating it changed nothing visible. All three
have since been deleted (*DEAD CODE REMOVED*). The lesson that cost the time: **verify a
variable's consumers with `find_variable_references` before building on its name.**

So the server's copy of a client's tank traces from a camera that is not looking anywhere useful,
and the turret sits still. That is the whole bug.

### Sources
- [Networking Overview](https://dev.epicgames.com/documentation/unreal-engine/networking-overview-for-unreal-engine)
- [Remote Procedure Calls](https://dev.epicgames.com/documentation/unreal-engine/remote-procedure-calls-in-unreal-engine)
- [Networking and Multiplayer](https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-and-multiplayer-in-unreal-engine)
- [Control rotation not replicating on Pawn (forum)](https://forums.unrealengine.com/t/cant-replicate-control-rotation/1971369)
- [AimOffset replication in multiplayer (article)](https://sohelmoon.medium.com/unreal-engine-4-aimoffset-replication-in-fps-multiplayer-6fe8594b7311)

## ✅ FIXED & VERIFIED — HUD "Accessed None" spam in multiplayer (2026-09-04)
`UpdateDamageCausedUI` wrote to the HUD widget unconditionally:
```
Blueprint Runtime Error: "Accessed None trying to read (real) property HUD"
  Node: Set Visibility / SetText (Text)   Graph: UpdateDamageCausedUI
```
`HUD` is created only for the pawn a player is actually looking through (written in
`PawnSwitching` and the EventGraph). In multiplayer the server also owns the client's tank, and
that copy has no HUD — so every damage event tried to update a widget that does not exist there.

Fix: a `Branch(IsValid(HUD))` immediately after the first `Set DamageCausedUI`:
```
Inputs -> Set DamageCausedUI -> Branch(IsValid(HUD))
                                  True  -> Set Visibility -> SetText -> Delay -> ... (existing)
                                  False -> Outputs
```
**Verified in a multiplayer session: no HUD errors.** The damage counter still accumulates; only
the widget work is skipped. Pre-existing bug, not
port-related — `HUD` was never moved to C++ (it is a Blueprint-only type, see NOT PORTABLE).

**The same guard is now applied to the other UI entry points** (2026-09-04). All of these read
`HUD` and would fail the same way on a non-local pawn, so each gained
`Branch(IsValid(HUD))` at its function entry, true path into the original chain, `else`
left unconnected (they have no outputs, so execution simply ends):
`UpdateHealthBar`, `UpdateZoomRatioUI`, `UpdateChosenVehicleUI`, `UpdateChosenWeaponUI`,
`WeaponSlotsDisplay`.

Two deliberate exclusions:
- **`UpdateHUD` was already guarded** by the original author — its entry runs through an
  `Is Valid` macro before anything else. Nothing to do.
- **`ReloadWeaponUI` is deliberately NOT guarded.** Unlike the others it is not pure UI: its
  Outputs node has TWO exec pins (`Reloaded` / `NotReloaded`) that drive caller logic, and its
  reload progress is stored in the widget itself (`Get Percent` / `Set Percent` on the progress
  bar). Skipping it would leave the caller with no exec path and could break reloading. A guard
  was started, then reverted. If it ever spams, it needs a considered fix, not this pattern.

**Check before guarding: is the function pure UI?** Every exec node a widget call, and no
outputs. If it returns exec pins or stores state, gating the whole thing changes behaviour.


## ✅ FIXED — multiplayer turret aiming (both directions verified 2026-09-04)

**FINAL STATE — both directions work.**

| Direction | Originally | After fix 1 | After fix 2 |
|---|---|---|---|
| Server turret → seen by client | works, jittery | **smooth** ✅ | smooth ✅ |
| Client turret → seen by server | **never worked** | never worked | **works** ✅ |

Neither fault was caused by the Blueprint → C++ port; both were pre-existing, proven by an A/B
test at `c783cbf` before any replicated variable moved to C++.

**Not caused by the port. Proven by A/B test, not by argument.** The same listen-server test was
run at `c783cbf` (Phase 8, before replicated variables moved to C++) and at the current tip.
Results were identical:

| Direction | Pre-port `c783cbf` | Post-port (current) |
|---|---|---|
| Server turret → seen by client | works, jittery | works, jittery |
| Client turret → seen by server | does not work | does not work |

Phase 9 therefore neither broke nor fixed anything, and is marked verified. What remains are two
faults that have always been in the Blueprint:

**1. The client's aim never reaches the server.** Replication is server → client only. The
then-present `ReplicateControlRotation` wrote `Rep_ControlRotation` when `HasAuthority() OR
IsLocallyControlled()`, so a client wrote only its own copy and it never travelled upward. There
was no Server RPC anywhere in this Blueprint. Client turret aiming had never worked in
multiplayer. (That function and that variable are now deleted; the working upward channel is
`ServerSetAimPoint` — see FIX 2.)

**2. The jitter on server → client.** `Event Tick` is gated only on `NOT Destroyed` — no
`HasAuthority`, no `IsLocallyControlled`. A client runs `TurretsAndGunsRotCalculation` every
frame and overwrites `TurretsRot`/`GunsRot` locally, fighting the value replicated from the
server. The two writers alternate, which is what the jitter is.

### ✅ FIX 1 VERIFIED — jitter gate (tested in multiplayer 2026-09-04)
`Event Tick` now gates the `TurretsAndGunsRotCalculation` call on a new C++ helper
`IsTurretSimulatedLocally()` = `HasAuthority() || IsLocallyControlled()` — deliberately the same
condition `ReplicateControlRotation` used at the time (that function has since been deleted).

Confirmed first that `TurretsAndGunsRotCalculation` is the **only** writer of
`TurretsRot`/`GunsRot`: its `Get` feeds a `SetArrayElem`, whereas `ScatteringCalculation` only
reads through `GetArrayItem`. So gating this one call is sufficient.

Graph shape (EventGraph, `K2Node_IfThenElse_2` + `K2Node_CallFunction_7`):
```
Sequence_4.then_4 -> Branch(IsTurretSimulatedLocally)
                       True  -> TurretsAndGunsRotCalculation -> ExecutionSequence_0
                       False -> ExecutionSequence_0
```
The False path goes **straight to `ExecutionSequence_0`** on purpose: that sequence holds the 8
`WheelRotationDefinition` calls and the scattering pass, which must still run on remote tanks.
Skipping only the turret recompute is the entire change.

Single-player regression check: `gate=True`, turret and gun rotations still non-zero, wheels
still turning, PIE clean, 0 errors. Offline behaviour is unchanged by construction — a
single-player pawn is both authority and locally controlled, so every gate passes.

**SINGLE PLAYER VERIFIED BY HAND 2026-09-04** — turret aims normally offline after both fixes.
Worth stating why this needed a separate check: the editor's Play settings had been left on
listen-server, so every `run_pie_smoke` "single player" run in this session was actually a
2-player listen server (two PIE worlds, a window titled *NetMode: Client 1*). Claims of
"single player verified" made from those runs were wrong. **Check the net mode before calling a
PIE run single player** — `grep UEDPIE_ ` in the log shows how many worlds were created.
single-player pawn is both authority and locally controlled.

**VERIFIED IN MULTIPLAYER 2026-09-04.** Two-window listen-server test: the client's view of the
server's turret is now **smooth** — the jitter is gone. Wheels keep turning on the remote tank,
confirming the False path still reaches `ExecutionSequence_0`.

Updated result table:

| Direction | Before fix 1 | After fix 1 |
|---|---|---|
| Server turret → seen by client | works, jittery | **works, smooth** ✅ |
| Client turret → seen by server | does not work | does not work (fix 2, not attempted) |

### ✅ FIX 2 VERIFIED (retargeted) — send the client's AIM POINT, not its control rotation
The first attempt sent `Rep_ControlRotation` and did nothing, because nothing live reads that
variable. Retargeted at what the turret actually consumes: the `TargetPoint` produced by the
camera line trace.

C++:
```cpp
bool ShouldSendAimToServer() const { return !HasAuthority() && IsLocallyControlled(); }
UPROPERTY(BlueprintReadWrite) FVector ReceivedAimPoint;      // written on the SERVER only
UFUNCTION(Server, Unreliable, BlueprintCallable) void ServerSetAimPoint(FVector);
```
Graph, inside `TurretsAndGunsRotCalculation`:
```
LineTrace -> SelectVector(hit, traceEnd, by IsThereTarget)          [existing]
          -> SelectVector(that, ReceivedAimPoint, by IsLocallyControlled)   [NEW]
          -> Set TargetPoint
          -> Branch(ShouldSendAimToServer) -> ServerSetAimPoint(TargetPoint) [NEW]
          -> Set StabilizingRotation                                [existing, both paths]
```
So the owning client traces as before and sends the point up; the server stops trusting its own
meaningless trace for that pawn and uses the received point. The server still runs all the turret
maths and stays authoritative.

`ReplicateControlRotation` was first restored to its original form since that whole path was a
dead end, and later **deleted entirely** along with `Rep_ControlRotation` — nothing read either.

Evidence the channel works (server-world probe, 3 pawns):
```
[2] BP_T90_Controller_Chaos_C_2  recvAim=(16064.0,173.1,-4.4)  turret0yaw=0.473
```
A real aim point traced on the client and stored on the server — the upward channel that has
never existed in this project. BP `UpToDate`, 0 errored BPs, PIE clean.

**VERIFIED 2026-09-04.** Two-window listen-server test: the client's turret is now visible on the
server, and the server -> client direction remains smooth. Multiplayer turret aiming works in
both directions for the first time in this project.

### Both fixes were FEATURE work, not port repair — and were done deliberately as such
They changed gameplay behaviour and needed a two-window manual test, so they were kept out of the
port's behaviour-preserving phases and verified separately. If more multiplayer work follows,
treat it the same way: its own commit, its own listen-server test, never folded into a phase.

### A/B testing is how you separate "port broke it" from "always broken"
The first test reported only "turret not replicating", which was too coarse — it could not
distinguish the two directions, and would have led to blaming the port. Checking out the
pre-port commit, rebuilding, and re-running the identical test settled it in one pass. Test each
direction separately, and record the pre-port result before drawing any conclusion.

### Replication — now tested (was a known gap, closed 2026-09-04)
Phases 4–17 were verified in **standalone** PIE, which does not run client-server replication at
all. That gap is now closed by the A/B listen-server test above. The properties exist, carry the
specifier, are registered, hold their array lengths, are written during play, and behave
identically before and after the port. The faults that remain are pre-existing gameplay gaps, not
port defects.

`run_pie_smoke` / `get_game_world` still reach only one PIE world, so any future replication
question needs a manual two-window test — see the human-only task table.

### AnimBP reads these directly off the pawn
Unlike `WheelRot*`/`SaggingDegree*` (where the AnimBP declares its own same-named copies),
`ABP_Chaos_<Tank>` has **no** `TurretsRot`/`GunsRot` variables of its own — it reads the pawn's
through its `TankPawn` reference. So these names matter across an asset boundary too.

Related trap: the AnimBP **does** declare its own `TurretRotation`, `TurretYaw`, `TurretPitch`,
`ClippingRangeMin/Max`, `TurretRotationSpeed`, `TurretHeightRangeClip` — same names as pawn
variables moved in Phase 6, but separate storage. Do not confuse the two.

**Phase 8 runtime proof:**
```
VibrationOffset_R=19  VibrationOffset_L=19  SplinePointLocation=9
SplinePointPerpendicularVectors=9  CopyPointIndices=9  FinalScattering=2
AntennaCurrentSpeed=30  TurretsRot{Unstabilized,PrevFrame}=10  GunsRot{Unstabilized,PrevFrame}=10
SAMPLE antenna0=(169.7875,24.0094,-13.6778) turretUnstab0=(0,0.1083,0) vibR0=-0.1011
```
The empty-by-default arrays get **sized by the Blueprint at runtime** (19/9/9/9/2), proving the
graph populates the native property. Sampled elements carry real data, so writes reach actual
elements, not just the container.

### Array length is the thing that will bite you
Several arrays ship pre-sized and the graph indexes into them directly. An empty array where
the graph writes index 29 produces
`Attempted to access index N from array 'X' of length 0`. Reproduce the sizes in the
constructor:
```cpp
AntennaCurrentSpeed.Init(FVector::ZeroVector, 30);
TurretsRotUnstabilized.Init(FRotator::ZeroRotator, 10);   // and the other three
```
This is the only thing the constructor is allowed to grow. It creates no components and loads
no assets, so RULE 1 and RULE 2 still hold.
Add these to `run_pie_smoke` `log_patterns.must_absent` for every array phase:
`"Attempted to access index"`, `"out of bounds"`.

### FIXED — AnimBP VibrationOffset warm-up race (was pre-existing, not caused by the port)
**Fixed 2026-09-04 in all six `ABP_Chaos_*`. `run_pie_smoke` now returns `ok:true` with
`"Attempted to access index": 0` on every tank map. Any index warning you see from here on IS a
regression — treat it as one.**

The bug: each `ABP_Chaos_<Tank>` reads its **own** `VibrationOffsetR` / `VibrationOffsetL` (note:
**no underscore**, unlike the pawn's `VibrationOffset_R`) at literal indices up to 18 while those
arrays were still at their length-0 default — **60 warnings per PIE session** on T90. Confirmed
pre-existing: present in `Tank_Sim_V2-backup-2026.09.03-20.55.50.log`, from the Phase 0–3 era,
before any variable was moved. Zero warnings ever named a pawn array.

Root cause was a warm-up race, not a bad wire. The `Set VibrationOffsetR` node is correctly fed by
`Get VibrationOffset_R` off `TankPawn`, but the whole copy chain sits behind
`Event Blueprint Update Animation → Delay(0.0) → IsValid(TankPawn) → Sequence`. The latent `Delay`
defers the copy past the first few AnimGraph evaluations, and the `IsValid` guard means the copy
**never** runs when there is no pawn owner at all — hence the 12 warnings at frame `[0]`, before
PIE even starts.

The fix: give each AnimBP's own array a **zero-filled default** sized to `max literal index + 1`
(`set_cdo_property`, arrays passed as a JSON list). Zero offset is exactly what the failed reads
already returned, so the pre-copy frames render identically, and the per-frame copy overwrites the
whole array as soon as the pawn is available. Lengths: T90 19, Leopard2A7 18, M1A2 14, Merkava 13,
ProxyTank 13, VK1602Leopard 16 (R and L identical per tank; T90's 19 matches the pawn's runtime
`VibrationOffset_R` length exactly).

The `Delay` node was deliberately **left in place** — removing it would not fix the no-pawn frames
and would change the copy cadence, i.e. the vibration timing. The one-frame lag between pawn and
AnimBP values is pre-existing and intact.

Generalise this: **an AnimBP array copied from the pawn must have a non-empty default whenever the
AnimGraph indexes it with a literal.** The AnimGraph evaluates on frames the EventGraph copy has
not reached yet, and in the editor preview it evaluates with no pawn at all.

**Phase 7 runtime proof:**
```
START  dt=0.00000  mph=0.000   rpm=0.0000   hullSpd=(0.00,0.00,0.00)  trackMod=1.000
END    dt=0.33333  mph=10.069  rpm=72.5816  trackMod=1.000
END    hullSpd=(489.87,-0.82,13.70) hullAcc=(-39.84,-0.12,-3.44) turretSpd=(473.865,-3.404,-17.593)
END    ampR=0.6000 ampL=0.6000 fillets=3.6300 hullDX=150.0344 crosshair=27596.5469 aimCorr=82.2626 dmg=0
```
`ampR`/`ampL` land on exactly 0.6000, matching the `MaxSpeedInfluence` cap. `TrackSpeedModifier`
holds 1.0, which is correct — nothing in this scenario modifies it.

**`DamageCausedUI` was NOT exercised at runtime.** Its only writer is the *macro*
`UpdateDamageCausedUI`. Macros are inlined at compile time and are not UFunctions, so unlike
a real function they cannot be invoked via `call_method`, and reaching it needs a
real damage-caused event. It is verified structurally: present natively, absent from the BP
variable list, and its 4 graph references still resolve. Recorded as a known gap rather than
counted as a pass. **A variable whose only writer is a macro cannot be runtime-proven this way** —
either drive the real gameplay event or record the gap honestly.

**Guard for every future phase — compare all six tanks before moving:**
```python
mcdo = unreal.get_default_object(master_bp.generated_class())
for each child BP: cdo.get_editor_property(name) != mcdo.get_editor_property(name) -> OVERRIDE
```
This is the direct check against attempt 1's failure mode. It becomes mandatory, not optional,
once real tuning values (`MaxSpeedKMH`, `WheelRadius*`, `TracksAmount`) start moving.

**Phase 6 runtime proof** — `FRotator` structs marshal correctly, and the legacy group was
proven by *calling the `_Old` functions directly* rather than accepting a static check.
(Historical: that legacy group was later deleted as dead code. The proof stands as the record of
why the move was sound at the time.)
```
START           main=(0.000,0.000,0.000)    blocking=False  rotating=False
LIVE (turned)   main=(-0.115,-3.794,14.058) blocking=False  rotating=True
LEGACY-BEFORE   turretRot=(0,0,0)  mgRot=(0,0,0)  yaw=0.000  pitch=0.000
LEGACY-AFTER    turretRot=(5.051,0.058,0.003)  mgRot=(5.051,0.058,0.003)  yaw=0.058  pitch=5.051
```
Internally consistent: `TurretPitch` == `TurretRotation.pitch`, `TurretYaw` ==
`TurretRotation.yaw`, and pitch tracks the injected control-rotation pitch of 5.0.

**Structs need no special handling.** `FRotator` moved exactly like a scalar — same
name/type/category match, same automatic rebind. Initialise with `FRotator::ZeroRotator`
to match the Blueprint's `(0,0,0)` default.

**Phase 5 runtime proof:**
```
START   accelR=0.0000   accelL=0.0000   HullZRot=0.0000  ZRotR/L=0.0000   XMoveR/L=0.0000
2.0s    accelR=-0.4364  accelL=0.5112   HullZRot=-1.4377 ZRotR/L=-7.1887  XMoveR/L=130.1542
4.5s    accelR=12.5046  accelL=13.4533  HullZRot=-1.3081 ZRotR/L=-6.5403  XMoveR/L=688.2112
```
`ZRot`/`XMove` are exactly R==L (correct on a straight line), and `XMove`=688.21 at 4.5s
independently agrees with the Phase 4 `ChassisDistance` reading at the same timestamp.

**First valid same-harness drive comparison:** Phase 4 end X=-369.93, Phase 5 end X=-367.84
from the same start (230.1 vs 232.2 units). <1% apart — no regression. Use `run_pie_smoke`
numbers only against other `run_pie_smoke` numbers, never against the 7921/6310 baselines.

**Phase 4 runtime proof** — the Blueprint graph writing the *native* properties during PIE:
```
START   dR=0.0000    dL=0.0000    deltaR=0.0000    deltaL=0.0000
1.5s    dR=75.5625   dL=69.0319   deltaR=40.2997   deltaL=34.6010
3.0s    dR=243.5395  dL=228.2148  deltaR=50.1447   deltaL=51.6305
4.5s    dR=700.1340  dL=688.3158  deltaR=118.5525  deltaL=119.5302
```
Monotonic accumulation, R/L symmetric on a straight line, deltas rising with speed.

**Proof the architecture is right:** every value that silently regressed in attempt 1
(section 4 table) is correct here automatically, because it was never moved out of the
Blueprint in the first place.

Current C++ surface: `Source/Tank_Sim_V2/Tank/TSTankControllerBase.{h,cpp}` — empty constructor
by design, plus the 4 Phase 4 chassis distance properties.

---

## ✅ VR crew wiring — team tank, role inputs, crew seats (2026-09-07)

Feature work, not port repair: it changes gameplay behaviour and needs a two-window listen-server
test, so it gets its own commits and its own test, exactly like the multiplayer turret fixes above.

The ask was "spawn the VK1602 per team, driver drives with WSAD, gunner aims with the mouse, each
role sits in its own seat". Almost all of the machinery already existed. Three things were broken,
and all three were invisible.

### ⚠ UE 5.7 moved Input Mapping Context keys — `Mappings` is DEAD
This is the big one, and it silently voids every IMC authored by a script written before 5.7.

```cpp
UE_DEPRECATED(5.7, "Use the DefaultKeyMappings struct instead.")
TArray<FEnhancedActionKeyMapping> Mappings;              // ignored at runtime
FInputMappingContextMappingData   DefaultKeyMappings;    // the ONLY one read
```
`UInputMappingContext::ForEachKeyMapping` iterates `DefaultKeyMappings.Mappings` and
`MappingProfileOverrides` — never `Mappings`. `PostLoad` migrates the old array **once**, guarded on
`GetLinkerCustomVersion(...) < EnhancedInputMappingContextProfileMappingsUpdate`, so an asset that
has already been re-saved under 5.7 keeps a fully populated `Mappings` array that does nothing at
all.

Found state: `IMC_Shared` had six mappings in the dead array and an EMPTY `DefaultKeyMappings`;
`IMC_Gunner` had lost fire/MG/reload the same way; `IMC_Driver`'s WSAD had survived into the live
array but with **no modifiers**, so all four keys wrote +X and forward/back/left/right were one
input. Nothing logs a warning for any of this — the keys simply do nothing.

**Reading an IMC through `get_editor_property('mappings')` tells you nothing.** Use
`project_query export_asset_text` and read which array the keys are actually in.

**Axis modifiers are mandatory for WSAD on an Axis2D action.** `IA_Drive` is Axis2D and
`Input_Drive` reads Throttle from `Y`, Steering from `X`, but a 1D key only ever writes X:
```
W  Swizzle YXZ            S  Negate + Swizzle YXZ            A  Negate            D  (none)
```

**A modifier must be outered to the IMC asset.** `unreal.InputModifierNegate()` from Python lands in
`/Engine/Transient`; the reference saves, but the instanced subobject does not, and the modifier
comes back **null** on the next load — the axis remap is silently lost. Build them with
`unreal.new_object(cls, outer=imc, name=...)` and prove it by reloading the package
(`EditorLoadingAndSavingUtils.reload_packages`) and re-reading.

### The role mapping context never reached a client
`ATSVRPawn` applied the role IMC and subscribed to `OnAssignmentChanged` from `PossessedBy`, which
runs on the **server only**. Every remote crew member therefore had no input context and no seat,
and it looked fine in standalone PIE because there the server is the only machine.

`APawn::NotifyControllerChanged()` is the correct hook — `Pawn.cpp` calls it from `PossessedBy`,
`OnRep_Controller` **and** `UnPossessed`. `OnRep_PlayerState` has to lead there too: on a client the
controller and the PlayerState arrive in either order and the role is only readable once both are
in. Unbind the previous PlayerState first, or a possession change leaves a stale subscription
driving this pawn's seat for a player it no longer represents.

### The gunner's mouse moved nothing
`Input_AimTurret` traced along the camera's forward vector, but on a desktop nothing ever rotated
that camera, so the ray fired straight out of the hull for ever. The action value is a look delta,
not an aim point; it now turns a seat-relative view rotation that the same trace reads. **Relative,
not world, and not the controller's rotation** — the seat rides the hull, so the view must turn with
the tank. Skipped while `GEngine->XRSystem->IsHeadTrackingAllowed()`, where the head is already the
aim and a written relative rotation would fight the tracked pose.

### RULE 2 was being violated in shipped code
`ATSGameMode`'s constructor filled `DefaultTankClass` with `ConstructorHelpers::FClassFinder` on
`BP_T90_Controller_Chaos` — the exact construct RULE 2 forbids (boot deadlock, packaged-build
crash). It also meant a GameMode Blueprint that had never set the field *looked* configured and
quietly spawned a T90. Removed; the class is Blueprint data now, and `GetTankClassForTeam` logs an
error when nothing is set. **Consequence:** a GameMode Blueprint relying on that inherited default
now has none. `/Game/TankSimulation/Core/BP_TSGameMode` is such an asset (a stale duplicate,
referenced by no map) — it will log the error if anything ever uses it.

### Seat placement — take the values a human already authored
`DriverSeat`/`GunnerSeat`/`CommanderSeat` on the master BP were untuned placeholders. The demo level
`Controller_Demo_VK1602Leopard` already held better ones as **per-instance overrides** on its placed
tank, measured against the Leopard interior; those are now the Blueprint defaults, so a
runtime-spawned tank seats its crew where someone had already decided they belong:
```
DriverSeat (88,-41,106)   GunnerSeat (20,29,147)   CommanderSeat (-30,23,144)
interior mesh bounds  X[-81..182]  Y[-93..93]  Z[50..216]   (all three inside)
```
Two traps worth keeping:
- **Measure the interior, not the hull.** `SK_VK1602Leopard` is 571x303x325; the crew compartment is
  the separate `Tank_SkeletalMesh` component (`WW2_VK1602Leopard_Interior`), 264x187x166. Seats
  placed against the hull bounds land inside armour.
- **The demo level's tank is named `BP_PZV_Controller_Chaos_C_2` but its class is
  `BP_VK1602Leopard_Controller_Chaos_C`.** The Blueprint was made from a Panther and the placed
  actor kept the old object name. Do not read an actor's name as its class.

### Runtime proof
```
BP_SetDriveInput(1.0, 0.0) on the spawned VK1602 -> speed 500.6 and 407.5 cm/s   (throttle path)
DefaultTankClass = BP_VK1602Leopard_Controller_Chaos_C   after a package RELOAD, not just in-session
seats resolve on the VK child: Driver (88,-41,106)  Gunner (20,29,147)  Commander (-30,23,144)
spawned tank carries TankCrew, TankControl, TankWeaponSystem, TankCommander
PIE clean: 0 Accessed None, 0 index warnings, 0 "has no scene component named"
```

**Still owed a human test** (see the human-only table): two-window listen server on `WarZone` with
one Driver and one Gunner, after closing the editor and rebuilding the **editor** target — the pawn
changes add `UPROPERTY`s, so Live Coding cannot carry them.


---

## ⚙ `bRequiresControllerForInputs` — the flag the whole crew model depends on

**Verified 2026-09-07 by reading the Chaos source and measuring, after this cost a long
debugging session.** An earlier note in this file claimed the flag was already False on all six
tanks. It was **True on all seven Blueprints, master included** — the value had never persisted.

`ChaosVehicleMovementComponent.cpp:1177`:
```cpp
bool bProcessLocally = bRequiresControllerForInputs
    ? (Controller && Controller->IsLocalController()) : true;

if (bProcessLocally && PVehicleOutput)
{
    // automatic gear shift 0 -> 1 lives HERE
    // CalcThrottleBrakeInput  lives HERE
    // the entire mechanical simulation lives HERE
}
```
Nobody possesses the tank under the three-crew model, so `Controller` is null and
`bProcessLocally` was false.

**The failure mode is silent and looks exactly like success.** `SetThrottleInput` still stores
`RawThrottleInput = 1.0`, so `GetThrottleInput()` reads 1.0 and every log line up the chain looks
right. What does not happen: the gearbox never leaves **Neutral (gear 0)** and the engine never
revs above its **600 RPM idle**. Symptom: "the tank does not move" with no error anywhere.

| | before | after |
|---|---|---|
| gear | 0, never shifts | 0 → 1 → 2 |
| engine RPM | 600 (idle) | 804 |
| forward speed | ~100 cm/s (sliding downhill) | 513 cm/s under power |

**Children do NOT inherit the master's value.** Each per-tank Blueprint holds its own stored
override, so setting it on `BP_TankController_Chaos` alone changes nothing. Set it on all seven
and verify:
```python
mv = [c for c in cdo.get_components_by_class(unreal.ChaosVehicleMovementComponent)][0]
mv.set_editor_property('bRequiresControllerForInputs', False)
```
`set_component_property` also works, and works on children too, despite the human-only table's
warning about inherited-component overrides — that warning is about creating a *new* override;
changing one that already exists is fine.

### ⚠ Speed is NOT evidence on a sloped map
The WarZone spawn is on an incline and an unpowered tank rolls at **~100 cm/s with
`CurrentDriveInput = (0,0)`**. Forward speed and displacement therefore cannot distinguish
"driving" from "sliding downhill", and were used to wrongly report success twice in one session.
Assert on **gear** and **engine RPM** instead, or on speed well above the roll rate.

### ⚠ Injecting an input ACTION skips the key mapping
`pie_inject_input_action` calls `InjectInputForAction`, which starts at the Input Action and
therefore proves nothing about the IMC key bindings — the exact layer UE 5.7's
`DefaultKeyMappings` deprecation breaks. A pass there means "the action is wired", not "W works".
To test the key layer you need a human at the keyboard, or a check of the live
`DefaultKeyMappings` array.

### Open question for multiplayer
With the flag False, `bProcessLocally` is true on **every** machine, so each client runs the
mechanical simulation on its own copy rather than only the authority. Epic leaves a comment right
above that line asking the same thing:
```cpp
// IsLocallyControlled will fail if the owner is unpossessed (i.e. Controller == nullptr);
// Should we remove input instead of relying on replicated state in that case?
```
Untested here. Watch for the same class of fault as the turret jitter (two writers fighting) in a
two-window listen-server test. Single-player and server-side driving are verified.

## 🤖 Automated listen-server testing (added 2026-09-07)

Two `-game` processes give a real server/client pair with a real NetDriver, which `run_pie_smoke`
cannot. The blocker used to be that a tank only spawns when a host clicks the lobby UI; the
`TSAuto*` URL options remove that.

```bash
# server
MSYS_NO_PATHCONV=1 "C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" \
  "C:\Projects\Tank_Sim_V2\Tank_Sim_V2.uproject" "/Game/TankSimulation/Maps/WarZone?listen" \
  -game -windowed -resx=800 -resy=450 -log -abslog="C:\Projects\Tank_Sim_V2\Saved\Logs\MPServer.log" &

# client - assigns itself and drives, unattended
MSYS_NO_PATHCONV=1 "C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" \
  "C:\Projects\Tank_Sim_V2\Tank_Sim_V2.uproject" "127.0.0.1?TSAutoTeam=A?TSAutoRole=Driver?TSAutoDrive=1,0,8" \
  -game -windowed -resx=800 -resy=450 -log -abslog="C:\Projects\Tank_Sim_V2\Saved\Logs\MPClient.log" &
```
Wait on `grep -aq "TSAuto: sequence complete"` in the client log, then diff the two logs. Close
both with `CloseMainWindow()`, never a kill.

**`-ExecCmds` does NOT work for this.** It runs during engine init, long before a PlayerController
or PlayerState exists, so an exec routes nowhere and logs *nothing at all* — which reads as "the
command is broken" rather than "it ran too early". That dead end is why the URL options exist.

Console commands (also usable by hand in `~`): `TSTeam A|B|C|D`, `TSRole Driver|Gunner|Commander`,
`TSClear`, `TSStartMatch`, `TSDrive <throttle> <steering> <seconds>`, `TSTankStatus`. They route
through the same Server RPCs the UI uses, so server validation is unchanged and they grant no extra
authority. Bodies compile out of Shipping.

**`TSDrive` holds the input on a timer, and must.** One `ServerSetDriveInput` is cleared by Chaos on
the very next tick; a single call measures a stationary tank and looks like a failure. Same trap as
calling a drive RPC in a Python loop — every call lands in one frame.

**Still human-only:** judging whether the client's *view* is smooth. Logs prove values, not jitter.

## 🪑 Crew seats, the interior mesh, and the turret (2026-09-08)

### Seats were all bolted to the HULL — two of the three were wrong
`DriverSeat` / `GunnerSeat` / `CommanderSeat` are SCS scene components at the root of
`BP_TankController_Chaos`, i.e. attached to the hull. That is correct for the Driver and wrong for
the other two: a Gunner and Commander sit in the turret basket and must traverse with the gun, or
the turret swings around them while they stay facing the hull's forward.

**It cannot be fixed by parenting in the Blueprint through this tooling.** The seats are SCS
components and the turret bone lives on `VehicleMesh`, an inherited NATIVE component;
`reparent_component` only sees SCS nodes and answers `New parent component not found: VehicleMesh`.
So `ATSTankControllerBase::AttachTurretCrewSeats()` does it at BeginPlay instead, with
**KeepWorldTransform** — designers keep placing seats in the viewport in hull space exactly as
before, and the attach only changes what they RIDE. RULE 8 stays intact: placement is still
Blueprint data.

`TurretSocketName` (default `turret`) and `TurretMountedSeatComponents` (Gunner + Commander, the
Driver deliberately absent) are `EditDefaultsOnly`. The vendor meshes share the `turret` bone name
— verified on both the VK1602 and the T90.

Measured with the turret at 135°: Driver moved 0.6uu (stayed in the hull), Gunner 65uu and
Commander 69uu (swung with the turret).

### Interior crew compartment is a SEPARATE skeletal mesh with its own skeleton
On the VK1602 it is the `Tank_SkeletalMesh` component (asset `Tank_New`, skeleton
`Tank_New_Skeleton`) — **not** the hull's `SK_VK1602Leopard`. It had **no AnimClass at all**, so
nothing drove its bones and the interior turret basket never moved.

`ABP_VK1602Leopard_Interior` now drives bone `b_Upper` from `GetInteriorTurretRotation()`, which
reads `TurretsRot[0].Yaw` — the same array the exterior turret uses, so the two cannot drift apart.

**Transform (Modify) Bone must be Additive in COMPONENT space.** Bone space put the yaw onto the
bone's *pitch* (its local frame is rolled ~90°), and Replace mode wipes the bind orientation
entirely. Verified: turret +135.145 -> bone +135.11 from bind, turret -44.870 -> bone -44.90, with
pitch ~0 and roll preserved.

`USkeletalMeshComponent` has **no** `SetBoneRotationByName` — that is on `UPoseableMeshComponent`.
An AnimBP is the only route for a skeletal mesh.

Other bones on that skeleton, unused so far: `b_Lower`, `b_Brake`, `b_Brake_001`, `b_Gas`,
`b_L_Lever`, `b_R_Lever` (driver controls).

### ⚠ Interior animation lagged a frame behind the gun — it was TICK ORDER
`TurretsAndGunsRotCalculation` writes `TurretsRot` in the pawn's Event Tick, but the interior mesh
could evaluate its AnimBP *before* that ran, drawing last frame's angle while the exterior gun drew
this frame's. Only the interior shows it, because `VehicleMesh` is the root and does not have the
problem. `SyncInteriorMeshTickToPawn()` calls `AddTickPrerequisiteActor(this)` on every non-root
skeletal mesh. The root is deliberately skipped — it carries the vehicle physics.

Logs prove ordering, never smoothness. **Whether the lag is visually gone still needs a human.**

## 🎯 Team spawn points

`ATSGameMode::GetSpawnTransformForTeam` takes any actor tagged `TSTeamSpawn_TeamA`..`TeamD`, or a
`PlayerStart` with that `PlayerStartTag`. WarZone had none, so tanks fell in at a world-origin
offset of z=200.

Two `TargetPoint`s are now placed and tagged, on ground chosen by a slope survey (sample a 600uu
footprint, take the spots with the smallest height spread):
```
TSTeamSpawn_TeamA  (-4000, -1000, -277.8)  yaw  90   ground spread 11.7uu
TSTeamSpawn_TeamB  (-4000,  1800, -389.5)  yaw -90   ground spread 20.3uu
```
**Flat ground is not cosmetic.** The first attempt put TeamA on a slope where the tank slid
backwards, and `ThrottleControl` then correctly applied FULL BRAKE (`Select(Throttle, Throttle*-1,
bPickA = Throttle>0 AND ForwardSpeedMPH < -1)`) — so pressing forward locked the tank. It read as
"driving is broken" and was not. On the flat spawn the tank reports `speed=0.0` at rest, which also
makes speed a usable signal again.

### ⚠ `unreal.Rotator(a, b, c)` is (ROLL, PITCH, YAW)
Passing a yaw into the second slot pitches the spawn point 90°, and the tank arrives **upside
down**. That is what happened on the first attempt.

### ⚠ WarZone.umap is GITIGNORED
`.gitignore:86` excludes `/Content/TankSimulation/Maps`, so these spawn points do NOT survive a
fresh clone. Un-ignore that folder, or re-place them per checkout.

### ⚠ Python `Vector` will not convert to `Vector_NetQuantize`
`pc.server_aim_turret(unreal.Vector(...))` throws `NativizeStructInstance: Cannot nativize
'Vector' as 'Vector_NetQuantize'`. Two test runs were misread as "the turret does not respond"
before the type error was spotted. Use `unreal.Vector_NetQuantize(x, y, z)`.

## 🔫 Firing

The tank's real firing is `StartShooting` / `StopShooting` on `BP_TankWeapon` — a hold-to-fire
pair, while `ITSTankInterface`'s fire events are single discrete requests. `BP_TankWeapon_C` is a
Blueprint-generated type C++ cannot name, so the master Blueprint implements two one-node events,
`BP_WeaponStartShooting` / `BP_WeaponStopShooting`, and C++ keeps the timing:
- `BP_FireMainCannon` holds the trigger `MainCannonTriggerHoldSeconds` (0.15) then releases.
- `BP_FireMachineGun` starts on the first request and pushes the release out
  `MachineGunReleaseDelaySeconds` (0.25) each frame, so the gun stops when requests stop arriving.
  Must comfortably exceed one frame — `ServerFireMachineGun` is Unreliable.

Verified reaching `StartShooting -> FireWeapon -> UpdateWeaponAmmo -> ReloadWeapon`.

### `WeaponReloadUI` was an infinite per-tick loop without a HUD
It is `Switch on Int -> Reload Weapon UI macro -> NotReloaded -> Delay Until Next Tick -> retry`.
The macro reads `HUD`, which no tank has under the crew model (nobody possesses the tank), so it
never completed and span every tick on every server tank after the first shot. Now guarded with
`Branch(IsValid(HUD))` at the event entry. This is the exception to the earlier note that
`ReloadWeaponUI` must not be guarded — that warning was about the *function's* two exec outputs;
this event's only consumer is its own retry loop.

## ✅ VR mode — headset when present, flat when not (2026-09-07)

Feature work, like the multiplayer fixes: own commits, own manual test. One build runs both ways;
there is no VR build, no VR map and no VR toggle. `ATSVRPawn::ApplyVRMode` decides on possession,
per client, from "is a headset connected" and "is this player allowed one".

C++: `Source/Tank_Sim_V2/Player/TSVRModeLibrary.{h,cpp}` (the one place that answers both
questions), plus VR handling on `ATSVRPawn`, host exclusion on `ATSHostCameraPawn`.

### ⚠ UE 5.7 has NO generic `MotionController_*` keys
This is the VR analogue of the `DefaultKeyMappings` trap, and it fails exactly as silently.

XR keys are **per controller profile**, declared in
`Engine/Source/Runtime/InputCore/Classes/InputCoreTypes.h`:
```
OculusTouch_Left_Trigger_Click   ValveIndex_Right_Thumbstick_2D   Vive_Left_Trackpad_2D  ...
```
`grep MotionController InputCoreTypes.cpp` returns **0 hits**. Binding `MotionController_Left_*`
compiles, saves, exports and does nothing at all.

**`FKey` import does not validate.** Proven: `Key.import_text("TotallyFakeKey123")` round-trips
verbatim. There is no Python-side validity check either — `KismetInputLibrary` is not exposed and
`get_all_keys` does not exist. So a key name cannot be verified after the fact from script.
**Read the name out of `InputCoreTypes.h` before authoring it**, or pick it in the editor's key
picker, which only offers real keys.

Real asymmetries in that header — these look like typos and are not:
- only `OculusTouch_LEFT_Menu_Click` exists; there is no Right equivalent
- Index has `Grip_Axis` / `Grip_Force`, **no** `Grip_Click`
- Vive has a **trackpad**, no thumbstick
- Touch left is X/Y, right is A/B

**A thumbstick needs no swizzle.** `IA_Drive` is Axis2D; WSAD needed Swizzle/Negate because a 1D
key only writes X, but `*_Thumbstick_2D` is already Axis2D and maps straight through.

### ⚠ In VR the Gunner's aim fires NO input action
The aim trace hung off `IA_AimTurret`. In a headset the player aims by turning their head, which
triggers no action, so the trace would never have run — the turret would have sat frozen for the
whole session with nothing in the log, on a code path that works perfectly on a desktop.

The trace now lives in `UpdateGunnerAim()`, called from the input action on a desktop **and from
`Tick` in VR**. Tick is enabled only for a local VR Gunner (`UpdateAimTickEnabled`), so no other
crew pawn pays for it. `IMC_Gunner` deliberately has **no** motion-controller binding for
`IA_AimTurret` — head aim is the mechanism.

Generalise: **any input-driven feature has to be re-checked for VR, because the HMD generates pose,
not events.** Anything that only runs on an action callback is dead in a headset.

### The host is never VR, and stereo alone is not enough to make that true
Two separate things keep the host flat, and killing only one leaves a broken half-state:
1. stereo rendering off (`UTSVRModeLibrary::SetVRModeEnabled(false)`), and
2. `Camera->bLockToHmd = false`.

Head tracking can be live while stereo is off, so a host with (2) still set gets a flat screen that
swings around with a headset sitting on the desk. `ATSHostCameraPawn::NotifyControllerChanged`
does both, guarded on `IsLocalController` — doing it for a remote copy would switch VR off on
somebody else's machine.

`ApplyVRMode` also re-checks `PS->IsHost()` rather than relying on pawn choice alone, so the rule
survives a future spectate mode that hands a host a crew pawn.

### Tracking origin: `Local`, not floor or stage
A crew member is strapped into a chair, and the seat scene component already marks where their head
goes. `EHMDTrackingOrigin::Local` centres tracking on the headset's start pose, so the head lands at
the seat. `LocalFloor`/`Stage` would put the player's head on the tank's floor.

### Build wiring
`UHeadMountedDisplayFunctionLibrary` lives in the **XRBase plugin** in UE5; the types
(`EHMDTrackingOrigin`) stayed in the **HeadMountedDisplay module**. Both are needed. XRBase also has
to be listed in `Tank_Sim_V2.uproject` — UBT warns
`does not list plugin 'XRBase' as a dependency` and that would bite at packaging time, not here.

### Widget interaction is scaffolding, not a feature
No crew widgets exist yet. What is in place: a **deactivated** `UWidgetInteractionComponent` on the
right hand, `IMC_VR_Widget` (IA_Primary on the right trigger), and
`ATSVRPawn::SetVRWidgetInteractionEnabled(bool)`, which points the laser and adds that context at
**priority 3** — above the role context — so the trigger clicks the widget instead of firing the
gun. Call it when a widget is shown/hidden; nothing guesses.

### 🔻 TEMPORARY — `bVRTestAutoAssign` must be reverted
`ATSTeamMatchGameMode` has a VR bring-up shortcut, off by default:
```
bVRTestAutoAssign   bool         suppresses host designation, force-assigns the joining player
VRTestTeam          ETSTeamId    default TeamA
VRTestRole          ETSCrewRole  change between runs to test each seat
```
Normal flow needs a host plus crew, which makes "put the headset on and check the Driver's stick" a
two-person job. With this on, one Play-In-Editor run drops you straight into a seat.

It **bypasses the host-admin rule on purpose**, which is exactly why it must not ship.
`ShouldDesignateAsHost` was made `virtual` so the flag can suppress host designation *before*
`GetDefaultPawnClassForController` reads `bIsHost` — designating afterwards would spawn the wrong
pawn. Every assignment logs a `Warning` naming the flag.

**Revert `bVRTestAutoAssign` (and the override that supports it) once VR is verified.**

### WarZone team spawn points
`TSTeamSpawn_TeamA..D` are now four `TargetPoint`s in `WarZone`, ground-traced and placed as two
opposing lines (A/C face +X at -2600, B/D face back at +1400). Without them
`GetSpawnTransformForTeam` falls back to a world-origin offset and logs a warning.
**`Content/TankSimulation/Maps` is git-ignored, so WarZone.umap is local-only** — these actors are
not in the repo and will not reach another clone.

### ⚠ Never toggle stereo during possession — it crashes in SetupPlayerInputComponent
First run in a real headset crashed with this stack and nothing in the log:
```
ATSVRPawn::SetupPlayerInputComponent   TSVRPawn.cpp:360
ATSGameMode::PostLogin                 TSGameMode.cpp:207   <- Super::PostLogin
ATSTeamMatchGameMode::PostLogin        TSTeamMatchGameMode.cpp:28
```
The reported line is a red herring — it is a null-guarded `BindAction`. The real cause is the
frame below it. `ApplyVRMode` ran from `PossessedBy`, which is inside
`AGameModeBase::RestartPlayer` inside `PostLogin`: **possession is still in progress and
`SetupPlayerInputComponent` has not run yet.** `EnableHMD(true)` rebuilds the viewport and its
render target, so flipping stereo there pulls the ground out from under the input setup that runs
immediately afterwards.

**Anything that rebuilds the viewport must be deferred out of the possession/restart call stack.**
`ApplyVRMode` now does `SetTimerForNextTick(... ApplyVRModeDeferred)`. The role mapping context is
still applied synchronously — that is plain Enhanced Input bookkeeping and is safe there.

Make the toggle idempotent too. Under **Play > VR Preview stereo is ALREADY on**, so an
unconditional `EnableHMD(true)` re-initialised the stereo device on every possession for no reason.
`SetVRModeEnabled` now early-outs when `IsVRModeActive() == bEnable`, which removes the churn
entirely in the normal case.

### ⚠ OpenXR never sees your Enhanced Input bindings unless they are in the PROJECT settings
The log said it outright, and it is easy to scroll past:
```
LogHMD: Warning: No mapping context provided in the OpenXR Input project settings, action
bindings will not be visible to the OpenXR runtime.
```
`OpenXRInput.cpp` builds its action set **at session start** from
`UEnhancedInputDeveloperSettings::DefaultMappingContexts` (Project Settings > Engine > Enhanced
Input). Empty list means it calls `BuildLegacyActions` instead and **no motion-controller binding
reaches the runtime, however correct the IMC assets are.** Adding a context at runtime from the
pawn is too late and does not count.

Registered in `Config/DefaultInput.ini`:
```ini
[/Script/EnhancedInput.EnhancedInputDeveloperSettings]
bEnableDefaultMappingContexts=True
+DefaultMappingContexts=(InputMappingContext="/Game/.../IMC_Driver.IMC_Driver",Priority=0,bAddImmediately=False,...)
```
**`bAddImmediately=False` is load-bearing.** It exposes the context to OpenXR without Enhanced Input
auto-applying it to every local player — applying them all would hand the Driver the Gunner's
bindings and destroy the role gating. `ATSVRPawn` still decides who gets which context.

(Also note the header's own caveat: these contexts must live in the game's root Content directory,
not a plugin.)

### Recentre needs a live head pose
`Could not retrieve a valid head pose for recentering` on every run: the auto-recentre fired before
the XR session produced a pose, so it silently did nothing. Guarded on
`UTSVRModeLibrary::IsHeadTrackingActive()` now; if tracking is not up yet the player still has the
Recenter button.

### Still owed a human test
Nothing here has run in an actual headset. The pawn changes add `UPROPERTY`s, so Live Coding cannot
carry them: close the editor and rebuild the **editor** target first.

### ⚠ `ETriggerEvent::Triggered` fires ONLY while actuated — releasing sends nothing
Reported as "the tank keeps moving forward even after releasing the input", in VR and on desktop.

`IA_Drive` was bound to `Triggered` only. Enhanced Input raises `Triggered` every frame the axis is
actuated and **nothing at all** when it returns to zero, so the release never reached the server,
`UTSTankControlComponent::CurrentDriveInput` kept its last non-zero value, and `BP_SetDriveInput`
went on feeding that throttle for ever.

Verified in the engine before fixing rather than assumed —
`UEnhancedPlayerInput::GetTriggerStateChangeEvent`: `Triggered -> None` yields
`ETriggerEventInternal::Completed`, `Ongoing -> None` yields `Canceled`. So for an action with no
explicit triggers, `Completed` fires exactly once on release. Both now bind to
`ATSVRPawn::Input_DriveReleased`, which sends an explicit `(0,0)`.

**Generalise: any state LATCHED from a `Triggered` binding needs a `Completed`/`Canceled` binding to
clear it.** A one-shot action (fire, reload) is fine on `Triggered` alone; a continuous one the
receiver keeps applying is not.

### ⚠ An Unreliable RPC is the wrong channel for a TERMINAL command
`ServerSetDriveInput` is `Server, Unreliable` — right for a per-frame stream, where a dropped packet
is superseded by the next one. But the **release is a single terminal packet**, and if that one
drops the tank drives away for ever with nothing to recover it.

Fixed with a server-side dead-man switch rather than by making the RPC reliable (a reliable stop can
still be overtaken by a late unreliable non-zero packet and re-latch the throttle).
`UTSTankControlComponent` ticks **only while a non-zero input is latched** and zeroes it after
`DriveInputTimeoutSeconds` (0.5s) without fresh input, logging which throttle it released. A held
input refreshes the timer every frame, so it never fires while someone is driving.

### ⚠ A merge once took the .h from one side and the .cpp from the other
`36813ab` (merge of `origin/SessionCreation`) kept `TSVRPawn.h` / `TSHostCameraPawn.h` from the VR
branch and took both `.cpp` files from the remote, which predated that work. The result **declared
nine functions that nothing defined** and the branch would not link:
```
error LNK2001: unresolved external symbol ATSVRPawn::Tick / ApplyVRMode / IsVRCrewMode /
               UpdateGunnerAim / SetVRWidgetInteractionEnabled /
               ATSHostCameraPawn::NotifyControllerChanged
```
It was invisible for a while because the running editor still held a DLL built *before* the merge.

Repaired by redoing the merge per-file with `git merge-file` against the real base
(`git merge-base`), which reduced it to three genuinely additive conflicts — VR code on one side,
diagnostics on the other — all resolved by keeping both. **If a link error names a function you
know you wrote, suspect a merge that split a header from its implementation**, and check
`git show <merge>^1:<file>` against `^2` before assuming the code was deleted deliberately.


---

## 6. Test Procedure (run after every phase)

1. Close editor fully. Rebuild C++ (Rule 5). Relaunch.
2. Editor reaches full load (not hung — check CPU Time is rising).
3. PIE; drive forward; confirm **real displacement**, not jitter:
   ```
   pie_call_function  K2_GetActorLocation   (before)
   hold/inject IA_MoveForwardBack ~3s
   pie_call_function  K2_GetActorLocation   (after)
   ```
   Baseline is ~3372 units in 3s. Anything near zero is a failure.
4. Check log for `Accessed None`, `CreateWidget called with a null class`, physics errors.
5. Green → commit. Red → revert this phase only; do not stack changes on a failure.

### The drive test is necessary but NOT sufficient
It only exercises what feeds physics. A variable that drives tracks, antennas, camera,
HUD or turret can be completely broken while the tank still drives its baseline distance.
**For each phase, additionally test the thing that phase actually touched** — for a moved
variable, sample it live in PIE and confirm it still gets written:

```
run_pie_smoke  probe_scripts: [{at_seconds: N,
  python: "... t.get_editor_property('<VarName>') ... unreal.log(...)"}]
```
A moved variable that reads 0 for the whole session while the tank moves means the
rebind silently failed. That is the Phase 4+ analogue of attempt 1's silent regressions.

### Comparing drive numbers requires the same harness
The 7921 / 6310 baselines came from one measurement method. `run_pie_smoke` advances PIE
at a much lower frame rate and yields much smaller absolute numbers for the same healthy
tank — those figures are **not** comparable to the baselines. Either re-measure the
known-good state with the identical harness before comparing, or judge that phase on a
targeted signal instead. Do not report a pass or a regression off mismatched harnesses.

---

## 7. Useful Paths

- Master BP: `/Game/YI_TankCollection/Blueprint/Master/Controller/BP_TankController_Chaos`
- Per-tank BPs: `/Game/YI_TankCollection/Blueprint/<Tank>/Controller/BP_<Tank>_Controller_Chaos`
- AnimBPs: `ABP_Chaos_<Tank>` (main), `ABP_PP_<Tank>` (post-process)
- Wheel BP: `/Game/YI_TankCollection/Blueprint/<Tank>/Controller/BP_TankWheel_Chaos_<Tank>`
- Inputs: `/Game/YI_TankCollection/Inputs/`
- Attempt-1 reference (do not build): `CPP_Port_WIP_DO_NOT_USE_YET/Attempt1_Reference/`
- Pre-reparent BP backups: `CPP_Port_WIP_DO_NOT_USE_YET/PreReparentBackup/`
