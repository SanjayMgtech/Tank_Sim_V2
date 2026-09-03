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

### RULE 5 — Close the editor before rebuilding C++
Live Coding cannot safely patch constructors or class layout. It caused repeated
`EXCEPTION_ACCESS_VIOLATION` crashes in the destructor during attempt 1.
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
| 4+ — Move variables | ⬜ next | |

**Proof the architecture is right:** every value that silently regressed in attempt 1
(section 4 table) is correct here automatically, because it was never moved out of the
Blueprint in the first place.

Current C++ surface: `Source/Tank_Sim_V2/Tank/TSTankControllerBase.{h,cpp}` — empty by design.

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

---

## 7. Useful Paths

- Master BP: `/Game/YI_TankCollection/Blueprint/Master/Controller/BP_TankController_Chaos`
- Per-tank BPs: `/Game/YI_TankCollection/Blueprint/<Tank>/Controller/BP_<Tank>_Controller_Chaos`
- AnimBPs: `ABP_Chaos_<Tank>` (main), `ABP_PP_<Tank>` (post-process)
- Wheel BP: `/Game/YI_TankCollection/Blueprint/<Tank>/Controller/BP_TankWheel_Chaos_<Tank>`
- Inputs: `/Game/YI_TankCollection/Inputs/`
- Attempt-1 reference (do not build): `CPP_Port_WIP_DO_NOT_USE_YET/Attempt1_Reference/`
- Pre-reparent BP backups: `CPP_Port_WIP_DO_NOT_USE_YET/PreReparentBackup/`
