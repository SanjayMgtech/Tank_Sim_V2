# C++ Tank Controller Port — Status (branch: cpp-tank-controller-port)

## Confirmed root cause of the last attempt's data loss

Reparenting a Blueprint from a BP-SCS parent (`BP_TankController_Chaos_C`)
to a native C++ parent (`ATSTankControllerChaos`) is **inherently lossy**
for per-child Inherited Component Handle (ICH) overrides — even without any
manual "cleanup" deleting them. BP-SCS components are keyed by an SCS node
GUID; native `CreateDefaultSubobject` components are keyed by name. When
the parent class changes from one identity scheme to the other, UE cannot
carry forward the old override *values* — each child's overridden
components reset to whatever the new native CDO declares as default.

## Master's own baseline (what the C++ constructor should default to)

These are `BP_TankController_Chaos`'s own SCS defaults (`source: "scs"` —
i.e. not itself an override of anything, this is the true engine-facing
default every tank starts from unless it overrides):

- `Light_R`: RelativeLocation=(368, -78.292870, 115)
- `P_Exhaust`: RelativeLocation=(-375, 15, 110)
- `SlideBackRight`: RelativeLocation=(-250, 130, 0)
- `Brake_L`: RelativeLocation=(-365.576111, -145.002548, 147)
- `TankSplineAnim`: empty (always a per-tank-only array, never a master default)
- `TrackPath_R`: not queried on master directly (irrelevant — master has no
  demo map / no meaningful default track shape; every tank overrides it)

Mirror values for `Light_L`/`SlideBackLeft`/`Brake_R` follow the same Y-sign
mirroring pattern seen in every tank below (not independently queried on
master, but confirmed by the pattern in all 6 tanks).

## Per-tank data — ALL 6 TANKS CAPTURED

For each tank: `ich_override` = this tank has its own value; `inherited_scs`
= this tank uses the master baseline above as-is (no override needed, C++
constructor default already correct for it).

### T90 (`/Game/YI_TankCollection/Blueprint/Tank_T90/Controller/BP_T90_Controller_Chaos`)

- `TrackPath_R` (ich_override): RelativeLocation=(0, 141.685136, 0),
  RelativeRotation=(0,-180,0). **19-point** SplineCurves — full curve
  captured in this session's transcript (search "Tank_T90" + "InVal=18.000000").
- `TankSplineAnim` (9 entries, all bInteractWithWheel=false):
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
- Other components (Light_R/L, Brake_L/R, P_Exhaust*, SlideBack*, Tank_Destroyed):
  NOT individually pulled for T90 — not yet confirmed override vs inherited.
  (Lower priority: T90 already has a working demo map and was the tank
  being actively tested this session; visually it looked correct.)

### Leopard2A7 (`/Game/YI_TankCollection/Blueprint/Tank_Leopard2A7/Controller/BP_Leopard2A7_Controller_Chaos`)

- `TrackPath_R` (ich_override): RelativeLocation=(0, 120.974007, 0.000001),
  RelativeRotation=(0,-180,0). **18-point** SplineCurves — full curve
  captured in this session's transcript (search "Tank_Leopard2A7" +
  "InVal=17.000000").
- `TankSplineAnim` (5 entries):
  ```
  (AnimPointIndex=7,  VibrationMaxAmplitude=2.5, VibrationPhase=45,   SaggingBack=-7)
  (AnimPointIndex=10, VibrationMaxAmplitude=5,   VibrationPhase=0,    SaggingForward=1)
  (AnimPointIndex=12, VibrationMaxAmplitude=5,   VibrationPhase=-180, SaggingForward=1)
  (AnimPointIndex=14, VibrationMaxAmplitude=5,   VibrationPhase=0,    SaggingForward=1)
  (AnimPointIndex=17, VibrationMaxAmplitude=2.5, VibrationPhase=-45,  SaggingForward=-9)
  ```
- `Light_R` (ich_override): (326.878826, -74.649862, 105.927688)
- `Light_L` (ich_override): (326.878826, 74.033021, 105.927689)
- `P_Exhaust` (ich_override): (-338.493248, 15, 132.703789)
- `P_Exhaust1` (ich_override): (-338.493248, 65, 132.703789)
- `P_Exhaust2` (ich_override): (-338.493248, -55, 132.703789)
- `SlideBackRight` (ich_override): (-162.281062, 130, 3)
- `SlideBackLeft` (ich_override): (-162.281062, -130, 3)
- `Brake_L` (ich_override): (-395.576111, -145.002548, 127)
- `Brake_R` (ich_override): (-395.576111, 145.997452, 127)
- `Tank_Destroyed`: (0,0,0) — no override needed (matches default)

### M1A2 (`/Game/YI_TankCollection/Blueprint/Tank_M1A2/Controller/BP_M1A2_Controller_Chaos`)

- `TrackPath_R` (ich_override): RelativeLocation=(0, 133.6, 0.000000337),
  RelativeRotation=(0,-180,0). **14-point** SplineCurves — full curve
  captured in this session's transcript (search "Tank_M1A2" + "InVal=13.000000").
- `TankSplineAnim` (3 entries):
  ```
  (AnimPointIndex=7,  VibrationMaxAmplitude=2.5, VibrationPhase=45,   SaggingBack=-9)
  (AnimPointIndex=10, VibrationMaxAmplitude=5,   VibrationPhase=0,    SaggingForward=5, SaggingBack=-5)
  (AnimPointIndex=13, VibrationMaxAmplitude=2.5, VibrationPhase=-45,  SaggingForward=-7)
  ```
- `Light_R` (inherited_scs, = master default): (368, -78.292870, 115)
- `Light_L` (inherited_scs, = master default): (368, 78.707130, 115)
- `P_Exhaust` (ich_override): (-370.475498, 15, 110)
- `P_Exhaust1` (ich_override): (-370.475498, 65, 110)
- `P_Exhaust2` (ich_override): (-370.475498, -55, 110)
- `SlideBackRight` (ich_override): (-200.585632, 130, 3)
- `SlideBackLeft` (ich_override): (-200.585632, -130, 3)
- `Brake_L` (inherited_scs, = master default but Z differs from master's
  147 → wait, matches: 147): (-365.576111, -145.002548, 147)
- `Brake_R` (inherited_scs): (-365.576111, 145.997452, 147)
- `Tank_Destroyed`: (0,0,0)

### Merkava (`/Game/YI_TankCollection/Blueprint/Tank_Merkava/Controller/BP_Merkava_Controller_Chaos`)

- `TrackPath_R` (ich_override): RelativeLocation=(0, 124.318214, 0.0000021),
  RelativeRotation=(0,-180,0). **13-point** SplineCurves — full curve
  captured in this session's transcript (search "Tank_Merkava" + "InVal=12.000000").
- `TankSplineAnim` (3 entries):
  ```
  (AnimPointIndex=6,  VibrationMaxAmplitude=2.5, VibrationPhase=45,  SaggingBack=-8)
  (AnimPointIndex=9,  VibrationMaxAmplitude=5,   VibrationPhase=0,   SaggingBack=10)
  (AnimPointIndex=12, VibrationMaxAmplitude=2.5, VibrationPhase=-45, SaggingForward=-5)
  ```
- `Light_R` (ich_override): (230.131038, -120.724774, 110.971655)
- `Light_L` (ich_override): (230.131038, 122.213260, 110.971655)
- `P_Exhaust` (ich_override): (85.421377, 146.172524, 118.862943)
- `P_Exhaust1` (ich_override): (54.647268, 146.172524, 118.862943)
- `P_Exhaust2` (ich_override): (114.964522, 146.172524, 118.862943)
- `SlideBackRight` (ich_override): (-188.615692, 130, 3)
- `SlideBackLeft` (ich_override): (-188.615692, -130, 3)
- `Brake_L` (inherited_scs): (-365.576111, -145.002548, 147)
- `Brake_R` (inherited_scs): (-365.576111, 145.997452, 147)
- `Tank_Destroyed`: (0,0,0)

### Proxy (`/Game/YI_TankCollection/Blueprint/Tank_Proxy/Controller/BP_Proxy_Controller_Chaos`)

- `TrackPath_R` (ich_override): RelativeLocation=(0, 141.212441, -0.0000019),
  RelativeRotation=(0,-180,0). **13-point** SplineCurves — full curve
  captured in this session's transcript (search "Tank_Proxy" + "InVal=12.000000").
- `TankSplineAnim` (3 entries):
  ```
  (AnimPointIndex=6,  VibrationMaxAmplitude=2.5, VibrationPhase=45,  SaggingBack=-10)
  (AnimPointIndex=9,  VibrationMaxAmplitude=5,   VibrationPhase=0,   SaggingForward=10)
  (AnimPointIndex=12, VibrationMaxAmplitude=2.5, VibrationPhase=-45, SaggingForward=-10)
  ```
- `Light_R` (ich_override): (220.087087, 84.466691, 128.286699)
- `Light_L` (ich_override): (220.087087, -84.466691, 127.762927)
- `P_Exhaust` (ich_override): (-375, 15, 110) — matches master default exactly
  but still stored as an override
- `P_Exhaust1` (ich_override): (-375, 65, 110)
- `P_Exhaust2` (ich_override): (-282.737138, -180.498176, 128.801946) —
  note this one is NOT mirrored/symmetric with P_Exhaust1, genuinely unique placement
- `SlideBackRight` (inherited_scs, = master default): (-250, 130, 0)
- `SlideBackLeft` (inherited_scs): (-250, -130, 0)
- `Brake_L` (ich_override): (-365.576111, -144.830963, 129.650953)
- `Brake_R` (ich_override): (-365.576111, 132.764190, 129.650953)
- `Tank_Destroyed`: (0,0,0)

### VK1602Leopard (`/Game/YI_TankCollection/Blueprint/WW2_VK1602Leopard/Controller/BP_VK1602Leopard_Controller_Chaos`)

- `TrackPath_R` (ich_override): RelativeLocation=(0, 114.072921, 0),
  RelativeRotation=(0,-180,0). **16-point** SplineCurves — full curve
  captured in this session's transcript (search "VK1602Leopard" +
  "InVal=15.000000"). Note: this tank's spline geometry is visibly
  different in shape from the other 5 (WW2-era tank, different silhouette).
- `TankSplineAnim` (7 entries — the only tank where `bInteractWithWheel`
  is ever `true`, on 4 of the 7):
  ```
  (AnimPointIndex=5,  VibrationMaxAmplitude=2, VibrationPhase=-300, SaggingForward=3,    SaggingBack=-3, InteractWithWheel=false)
  (AnimPointIndex=8,  VibrationMaxAmplitude=5, VibrationPhase=-240, SaggingForward=3,    SaggingBack=7,  InteractWithWheel=true)
  (AnimPointIndex=9,  VibrationMaxAmplitude=5, VibrationPhase=-180, SaggingForward=2,    SaggingBack=6,  InteractWithWheel=true)
  (AnimPointIndex=10, VibrationMaxAmplitude=5, VibrationPhase=-120, SaggingForward=2,    SaggingBack=6,  InteractWithWheel=true)
  (AnimPointIndex=11, VibrationMaxAmplitude=5, VibrationPhase=-60,  SaggingForward=0,    SaggingBack=4,  InteractWithWheel=true)
  (AnimPointIndex=12, VibrationMaxAmplitude=5, VibrationPhase=0,    SaggingForward=-2,   SaggingBack=2,  InteractWithWheel=false)
  (AnimPointIndex=15, VibrationMaxAmplitude=2.5, VibrationPhase=60, SaggingForward=-5.5, SaggingBack=0,  InteractWithWheel=false)
  ```
- `Light_R` (ich_override): (247.620128, -113.442916, 186.146679)
- `Light_L` (ich_override): (247.620129, 113.442916, 186.146753)
- `P_Exhaust` (ich_override): (-366.711871, 15, 206.741509)
- `P_Exhaust1` (ich_override): (-256.867045, 34.418773, 165.919317)
- `P_Exhaust2` (ich_override): (-256.901549, -34.533330, 165.978924)
- `SlideBackRight` (inherited_scs): (-250, 130, 0)
- `SlideBackLeft` (inherited_scs): (-250, -130, 0)
- `Brake_L` (ich_override): (-365.576111, -145.002548, 147) — matches
  master default exactly but still stored as override
- `Brake_R` (ich_override): (-365.576111, 145.997452, 147)
- `Tank_Destroyed`: (0,0,0)

## Still not extracted / lower priority

- T90's own Light/Brake/Exhaust/SlideBack overrides (T90 has a working demo
  map and visually looked correct this session — worth a quick pass before
  final reparent, but not blocking).
- `Fire` (AudioComponent) and `BP_TankWeapon` (non-scene) — no transform to
  extract; likely fine to leave at native CDO defaults for all tanks, but
  worth a spot-check of `BP_TankWeapon`'s class reference per tank (each
  tank probably has its own weapon Blueprint assigned).
- Rotation on lights/particles — only checked `RelativeLocation` throughout
  (cheaper query). All spot-checked rotations were (0,0,0) on the two
  components where I pulled the full dump (Leopard2A7's Light_R/L), so
  likely safe to assume 0 rotation everywhere, but not confirmed for the
  other 4 tanks' lights.
- Any tank-specific graph logic beyond the "thin" 4-node EventGraph / 2-node
  UserConstructionScript pattern confirmed on T90 — not spot-checked on the
  other 5 tanks. Worth one `list_graphs` call each before reparenting
  (cheap) to make sure none of them have real per-tank logic beyond T90's.

## Fixed in the parked C++ (`../Source/Tank_Sim_V2/Tank/TSTankControllerChaos.cpp`)

- `VibrationOffset_R/L` sizing off-by-one — fixed in `OnConstruction`, now
  matches the original `UserConstructionScript`'s `ForLoop(0,
  TrackPath_R->GetNumberOfSplinePoints()-1)` + `Array_Set(...,
  bSizeToFit=true)` pattern exactly (sizes to `NumPoints`, not `NumPoints-1`).

## Still TODO in the C++ before reparenting again

- `TrackPath_R` must stop being one hardcoded spline in the constructor —
  every tank needs a DIFFERENT point count/shape (13 to 19 points, all
  different geometry). Correct approach: keep the constructor's default
  minimal (or match master's own baseline, which has none meaningful since
  master has no demo map), then apply each tank's real `SplineCurves` via
  `set_component_property` on the child BP **after** reparenting.
- Same pattern for every `ich_override` component listed above: after
  reparenting each tank child, re-apply its captured `RelativeLocation`
  (and any non-default `TankSplineAnim` array) via
  `set_component_property`/`set_cdo_property`.
- Components where a tank shows `inherited_scs` need NO action — the native
  C++ constructor's default (matching master's baseline above) is already
  correct for that tank on that component.

## Recommended process from here (repeat per tank, or batch once C++ constructor is right)

1. Reparent master BP → `ATSTankControllerChaos`.
2. Reparent each of the 6 tank children → (now automatically parents through
   the native-backed master).
3. For each tank, for each component listed above as `ich_override`: call
   `set_component_property` with that tank's captured value. For
   `TrackPath_R` specifically, this means setting `SplineCurves` to the
   full captured curve string (large payload, one call per tank).
4. For each tank with a non-empty `TankSplineAnim` above: `set_cdo_property`
   with the ImportText array-of-struct syntax (same pattern already proven
   working for T90 earlier this session:
   `((AnimPointIndex=6,bInteractWithWheel=False,VibrationMaxAmplitude=2,...),(...))`).
5. Strip only duplicate GRAPH logic (functions/events now in C++) from each
   child — never touch SCS/ICH component entries during that cleanup.
6. Compile, save, PIE-test each tank individually before moving to the next.
7. Only after all 6 tanks verified working should this branch's BPs ever
   replace what's on `main`.
