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

Full rebuild with the editor closed:
```bash
"C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" Tank_Sim_V2Editor Win64 Development -Project="C:\Projects\Tank_Sim_V2\Tank_Sim_V2.uproject" -WaitMutex -FromMsBuild
```
If Live Coding was used, delete stale patches before relaunching, or the editor may hang/crash:
```bash
rm -f Binaries/Win64/UnrealEditor-Tank_Sim_V2.patch_*
```

### RULE 6 — Never leave stray `.uasset` files in `Content/`
The editor scans and compiles everything under `Content/` at startup. A stray backup
Blueprint there gets loaded and compiled. Keep backups in
`CPP_Port_WIP_DO_NOT_USE_YET/`, never in `Content/`.

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
  then_3: UpdateHUD → UpdateSound → ReplicateControlRotation →
          UpdateCrosshairPositionAndSize → TracksDecal → HullAccelerationDefinition →
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

**Some variables are referenced only from dead `_Old` code paths.** `UpdateTurretRotation_Old`
and `UpdateMachineGunRotation_Old` are not called from Tick, so `TurretRotation`, `MGRotation`,
`TurretYaw` and `TurretPitch` legitimately stay 0 for a whole PIE session. **Do not read that
as a failed rebind, and do not accept it as a pass either.** Prove them by invoking the owning
function directly and diffing the values across the call:
```
t.call_method('UpdateTurretRotation_Old')   # both take no arguments
```
Before moving a variable, check *which graph* its references live in. A `_Old` suffix or an
`Old` category means the normal drive test will never touch it.

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
`Is Vehicle taken?`, `Turret Rotation Speed`, `MG Yaw`, `Clipping Range Min/Max`,
`Turret Height Range Clip`, `MGRotation Speed`, `Debug Camera`. A C++ identifier cannot
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
| 6 — Vars: turret / MG scratch (**first structs**) | ✅ PASS | `MainTurretAndGunRotation`, `TurretRotation`, `MGRotation` (FRotator), `TurretYaw`, `TurretPitch` (double), `TurretBlocking`, `IsTurretRotating` (bool). All 18 native props present; moved names gone from the BP. `TurretRotation` still 10 refs / same node IDs. BP `UpToDate`, 0 errors/warnings, 0 errored BPs. PIE clean. |
| 7 — Vars: antenna / UI / misc (**FVector, float, int32**) | ✅ PASS | 14 moved: `HullSpeedWorld`, `HullAccelerationWorldInverted`, `TurretSpeedLocalInverted` (FVector), `CrosshairTraceClamp`, `AimPointCorrectionUI`, `CurrentAmplitudeMultiplierR/L`, `FilletsCompensation`, `HullDeltaXLocation`, `DeltaSeconds`, `CurentRPMRatio`, `TrackSpeedModifier` (double), `ForwardSpeedMPH` (float), `DamageCausedUI` (int32). 32 native props total. All 6 tanks match master on every default. BP `UpToDate`, 0 errors/warnings, 0 errored BPs. PIE clean. **13/14 proven at runtime; `DamageCausedUI` structural only** (see below). |
| 8 — Vars: scratch **ARRAYS** | ✅ PASS | 11 moved: `VibrationOffset_R/L`, `FinalScattering` (TArray&lt;double&gt;), `SplinePointLocation`, `SplinePointPerpendicularVectors`, `AntennaCurrentSpeed` (TArray&lt;FVector&gt;), `CopyPointIndices` (TArray&lt;int32&gt;), `TurretsRotUnstabilized`, `TurretsRotPrevFrame`, `GunsRotUnstabilized`, `GunsRotPrevFrame` (TArray&lt;FRotator&gt;). 43 native props. All lengths match across all 6 tanks. BP `UpToDate`, 0 errors/warnings, 0 errored BPs. |
| 9 — Vars: **REPLICATED** | ✅ PASS (with a stated gap) | `Rep_ControlRotation` (FRotator), `TurretsRot`, `GunsRot` (TArray&lt;FRotator&gt;, len 10). 46 native props. `UPROPERTY(Replicated)` + `GetLifetimeReplicatedProps`/`DOREPLIFETIME`. Lengths correct on all 6 tanks. BP `UpToDate`, 0 errors/warnings, 0 errored BPs, **0 LogNet warnings**. PIE clean. **Client-server replication NOT exercised — see gap below.** |
| 10 — Vars: object / component **references** | ✅ PASS | 7 moved: `BaseTrackMaterial`, `RightTrackMID`, `LeftTrackMID`, `TracksInstances_R/L`, `TrackPath_L`, `VehicleMovement`. 53 native props. **All 21 SCS components still intact.** BP `UpToDate`, 0 errors/warnings, 0 errored BPs. PIE clean. 6/7 runtime-proven. |
| 11 — Vars: first real tuning values | ❌ **FAILED, REVERTED** | Moving the four `WheelRadius*` lost **19 of 24 per-tank overrides** — every tank fell back to the master default. Reverted; all overrides restored, nothing lost. See below. |
| 11 (retry) — vibration/sagging tuning, **middle path** | ✅ PASS | 9 moved: `SpeedInfluence`, `MaxSpeedInfluence`, `AccelerationInfluence`, `MaxAccelerationInfluence`, `TrackFrequency`, `DecayRate`, `InteractionAmplitudeMultiplier`, `SaggingMaxDistance`, `ProportionalCoefficient`. 62 native props. All 9 verified correct on master **and all 6 tanks** (`problems=0`). BP `UpToDate`, 0 errors/warnings, 0 errored BPs. PIE clean. |
| 12 — **First function move**: `VibrationCalculation` | ✅ PASS | Ported 1:1 to C++. Call site in `PointLocationCalculation` rebound with **every pin intact, 0 orphaned**, node retargeted to `TSTankControllerBase`. Math verified **bit-exact** (delta 0.000e+00) on 3 input pairs. BP `UpToDate`, 0 errors/warnings, PIE clean. |
| 13 — `HullAccelerationDefinition` | ✅ PASS | Ported 1:1. Call site retargeted to `TSTankControllerBase`, all pins intact, 0 orphaned. `HullSpeedWorld` matches the mesh's physics velocity **exactly** (delta 0.0000) at two sample points; inverted acceleration non-zero. BP `UpToDate`, PIE clean. |
| 13a — `SaggingCalculation` | ⛔ BLOCKED, later UNBLOCKED | Parameter shadowed a moved member. Resolved in Phase 15/16. |
| 14 — Tuning w/ **override re-application** + `UpdateTracksMID` | ✅ PASS | Moved `TilingSegmentLength`, `InvertTrackDirection` (both differ per tank), re-applied all overrides, then ported `UpdateTracksMID`. Overrides **survived an editor restart** (`problems=0`). Function math matches to float precision. |
| 15 — **Rename** `SaggingCalculation` param (manual) | ✅ PASS | `HullDeltaXLocation` -> `InHullDeltaXLocation`, done by hand in the editor. Both call sites kept their wiring; signature clean; 0 errored BPs. |
| 16 — `SaggingCalculation` | ✅ PASS | Ported 1:1 as **BlueprintPure**. Call site retargeted, no exec pins added, 0 orphaned. Math **bit-exact** (delta 0.000e+00) across both branches and both clamp boundaries. |
| 17+ — SCS component access pattern, then more functions | ⬜ next | |

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
| `WheelRotationDefinition` | reads `WheelRadius*` (per-tank) and its params would shadow `TrackThickness`/`WheelSpeedCorrectionUV` |

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
| `WheelRotationDefinition` | `TrackThickness` | still a BP variable | **would block if moved** |
| `WheelRotationDefinition` | `WheelSpeedCorrectionUV` | still a BP variable | **would block if moved** |

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

### KNOWN GAP — replication itself was not tested
Everything above was verified in **standalone** PIE, which does not run client-server
replication at all. What is proven: the properties exist, carry the specifier, are registered,
compile, hold their array lengths, are written during play, and produce no `LogNet` warnings.
What is **not** proven: that values actually reach a client.

The available tooling (`run_pie_smoke`, `get_game_world`) only reaches one PIE world, so it
cannot compare a server value against a client's. To close this properly, run PIE manually as
**Play As Listen Server with 2 players**, rotate the turret on the server, and confirm the
client's tank turret follows. Until someone does that, treat multiplayer turret/gun sync as
untested — not as working.

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
the Phase 6 `_Old` functions they cannot be invoked via `call_method`, and reaching it needs a
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
proven by *calling the `_Old` functions directly* rather than accepting a static check:
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
