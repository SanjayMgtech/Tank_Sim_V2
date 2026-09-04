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
| 8+ — Move more variables | ⬜ next | |

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
