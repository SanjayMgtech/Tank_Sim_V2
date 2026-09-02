# C++ Tank Controller Port — Status (branch: cpp-tank-controller-port)

## Confirmed root cause of the last attempt's data loss

Reparenting a Blueprint from a BP-SCS parent (`BP_TankController_Chaos_C`) to a
native C++ parent (`ATSTankControllerChaos`) is **inherently lossy** for
per-child Inherited Component Handle (ICH) overrides — even without any
manual "cleanup" deleting them. BP-SCS components are keyed by an SCS node
GUID; native `CreateDefaultSubobject` components are keyed by name. When the
parent class changes from one identity scheme to the other, UE cannot carry
forward the old override *values* — each child's overridden components reset
to whatever the new native CDO declares as default.

This is why T90's `TrackPath_R` (a real, unique 19-point spline) came back
as the generic 12-point spline after the reparent last time — not something
the cleanup step broke.

## What this means for a true 1:1 port

Every per-tank component override must be **extracted from the original
vendor BP *before* reparenting**, then **re-applied after reparenting** via
`set_component_property`/`set_cdo_property`. There is no shortcut.

## Components with real per-tank ICH overrides (confirmed via `get_components`, `has_override: true`)

Per tank (checked on Leopard2A7 as a representative sample — all 6 tanks
need this checked individually, counts may vary slightly per tank):

- `Light_R`, `Light_L` (SpotLightComponent — transform + likely intensity/color)
- `P_Exhaust`, `P_Exhaust1`, `P_Exhaust2` (ParticleSystemComponent — transform)
- `SlideBackRight`, `SlideBackLeft` (ParticleSystemComponent — transform)
- `Brake_L`, `Brake_R` (PointLightComponent — transform)
- `Fire` (AudioComponent)
- `Tank_Destroyed` (StaticMeshComponent — likely mesh + transform)
- `BP_TankWeapon` (component instance — non-scene)
- `TrackPath_R` (SplineComponent — full spline curve, ~19 points w/ tangents, **the big one**)

`TrackPath_L` is NOT overridden (it's derived at runtime via `SplineMirrorCopy`
from `TrackPath_R`, confirmed from the original `UserConstructionScript`).

Also per-tank: the `TankSplineAnim` array variable (not a component, a plain
BP variable default) — 9 struct entries per tank with real tuned values
(AnimPointIndex, VibrationMaxAmplitude, VibrationPhase, SaggingForward/Back).

## Data already extracted (safe to reuse, don't re-pull)

**T90** (`/Game/YI_TankCollection/Blueprint/Tank_T90/Controller/BP_T90_Controller_Chaos`):
- `TrackPath_R`: full 19-point `SplineCurves` — captured in this session's
  transcript (search for "InVal=18.000000" near the T90 component dump).
  RelativeLocation=(0, 141.685136, 0), RelativeRotation=(0,-180,0).
- `TankSplineAnim`: 9 entries, real values:
  ```
  (AnimPointIndex=6,  VibrationMaxAmplitude=2, VibrationPhase=45,   SaggingBack=-10)
  (AnimPointIndex=9,  VibrationMaxAmplitude=3, VibrationPhase=0,    SaggingForward=4)
  (AnimPointIndex=10, VibrationMaxAmplitude=2, VibrationPhase=-45)
  (AnimPointIndex=11, VibrationMaxAmplitude=5, VibrationPhase=-90,  SaggingForward=6)
  (AnimPointIndex=12, VibrationMaxAmplitude=2, VibrationPhase=-135)
  (AnimPointIndex=13, VibrationMaxAmplitude=5, VibrationPhase=-180, SaggingForward=6)
  (AnimPointIndex=14, VibrationMaxAmplitude=2, VibrationPhase=-225)
  (AnimPointIndex=15, VibrationMaxAmplitude=3, VibrationPhase=-270, SaggingForward=3)
  (AnimPointIndex=18, SaggingForward=-10)
  ```
  (bInteractWithWheel=false for all 9 entries)

**Leopard2A7** (`/Game/YI_TankCollection/Blueprint/Tank_Leopard2A7/Controller/BP_Leopard2A7_Controller_Chaos`):
- `TrackPath_R`: full 18-point `SplineCurves` captured this session
  (search "InVal=17.000000" near the Leopard2A7 dump).
  RelativeLocation=(0, 120.974007, 0.000001), RelativeRotation=(0,-180,0).
- `Light_R`: RelativeLocation=(326.878826, -74.649862, 105.927688).
  (Light_L, other components NOT yet pulled.)

## NOT yet extracted (real work remaining)

- Leopard2A7: Light_L, P_Exhaust×3, SlideBack×2, Brake_L/R, Fire, Tank_Destroyed, TankSplineAnim
- M1A2: everything
- Merkava: everything
- Proxy: everything
- VK1602Leopard: everything
- Master `BP_TankController_Chaos`: confirm which of these components it
  declares defaults for vs. leaves at engine defaults (needed so we know
  what the native C++ constructor's baseline should be, vs. what must be a
  per-tank override applied after reparenting).

## Also fix in the parked C++ (`../Source/Tank_Sim_V2/Tank/TSTankControllerChaos.cpp`)

- `VibrationOffset_R/L` sizing off-by-one — **already fixed** this session
  (`OnConstruction`, matches the original `UserConstructionScript`'s
  `ForLoop(0, TrackPath_R->GetNumberOfSplinePoints()-1)` + `Array_Set(...,
  bSizeToFit=true)` pattern exactly).
- `TrackPath_R` must stop being a single hardcoded 12-point spline in the
  constructor (that was the master's own default, wrongly applied to every
  tank). It should be created with a minimal/empty default in the
  constructor, then each tank child gets its real spline re-applied via
  `set_component_property` on `SplineCurves` after reparenting.

## Recommended process per tank (repeat for all 6)

1. With the ORIGINAL vendor BP live (current state on `main`/this branch
   right now), pull `get_components` (cheap) to confirm the override list,
   then `get_component_details` for each overridden component — but request
   only what's needed (transform is in every response already; for
   SplineComponent the `SplineCurves` property is the only one that matters).
2. Save the transform/spline/relevant-property values to this file (compact
   form, not raw JSON) before moving to the next tank.
3. Only once ALL 6 tanks + master are captured: reparent master, reparent
   each child, strip only duplicate GRAPH logic (functions/events now in
   C++) — never touch SCS/ICH component entries during cleanup.
4. Re-apply each tank's captured override data via `set_component_property`
   / `set_cdo_property`.
5. Compile, save, PIE-test each tank individually before moving on.
6. Only after all 6 tanks verified working should this branch's BPs replace
   what's on `main`.

## Why this session stopped here

Pulling one component's full property dump (e.g. `Light_R` on Leopard2A7)
costs ~15-20K tokens even though only 2-3 fields actually differ from
default. Extracting ~9 components × 6 tanks at that rate is not viable in a
single session's budget. Next session should either continue this
compact-capture approach a few components at a time, or use a dedicated
Workflow/subagent pass specifically for the extraction (not the reparenting
or C++ decisions, which should stay with the main thread) to parallelize
the read-only data pulls.
