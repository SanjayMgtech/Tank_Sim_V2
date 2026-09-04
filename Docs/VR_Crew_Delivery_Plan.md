# VR + Three-Crew Delivery Plan

Plan for the two things the PDFs ask us to build: **the VR layer** and **the three-developer
parallel workstream**. Written against the actual repository state on branch
`cpp-tank-controller-port` at commit `e5f25bf`, not against the PDFs in isolation.

Read `CLAUDE.md` first — it is the operating manual for the Blueprint→C++ port and several of
its hard rules constrain this plan.

---

## 0. Start here: most of the C++ framework already exists

This is the single most important fact for planning, and it is not obvious from the PDFs.

Commit `c47e904` ("Implement Tank Simulation multiplayer VR framework in C++") plus `51d4dc2`
and `1e8808b` already landed **~3,900 lines** implementing essentially all of Framework Doc §4:

```
Source/Tank_Sim_V2/
├─ Core/        TSGameInstance, TSGameMode (273 lines), TSGameState, TSTypes
├─ Networking/  TSSessionSubsystem (188 lines)
├─ Player/      TSTankPlayerController, TSTankPlayerState, TSVRPawn (216 lines)
├─ Tank/        TSTankInterface, Crew/Control/Weapon/Commander components
├─ UI/          Login, SessionBrowser, Team, Role, Crew/Driver/Gunner/Commander HUD bases
├─ Voice/       TSVoiceSubsystem
└─ Data/        TSRoleDefinition, TSTankDefinition
```

It compiles (`Binaries/Win64/UnrealEditor-Tank_Sim_V2.dll` is current) and ships three docs in
`Docs/`, including a step-by-step `Tank_Simulation_Implementation_Walkthrough.md`.

**So the job is not "write the framework". It is "wire it up, fix two wrong assumptions, and
build the VR layer that genuinely does not exist yet."** That is a substantially different — and
smaller — piece of work than the PDFs imply, and the three-developer split has to be re-cut
accordingly.

### What is actually missing

| Area | State |
|---|---|
| C++ framework classes | ✅ written, compiles |
| Framework wired to the game | ❌ `GlobalDefaultGameMode` still points at the old `Tank_GameMode_C` |
| Tank Blueprint ↔ framework | ❌ `ATSTankControllerBase` does not implement `ITSTankInterface` |
| VR runtime config | ❌ no OpenXR settings in `DefaultEngine.ini` at all |
| VR input assets | ❌ all 27 `IA_*` are desktop/gamepad; zero VR actions or role contexts |
| VR pawn Blueprint | ❌ C++ class exists, no BP subclass, never spawned |
| Widget Blueprints | ❌ C++ bases exist, no WBP built on them |
| Crew seating / interiors | 🟡 interior meshes are being authored (untracked in `Content/.../Interior/`) |
| Blueprint→C++ port | 🟡 phases 0–18 green; only `SelfCollisionCheck` left as portable |

---

## 1. Two blockers to settle before anyone writes parallel code

### Blocker A — `ATSTank` is the wrong base class, and nothing uses it — ✅ **RESOLVED**

> Done during the dead-code sweep. `TSTank.{h,cpp}` deleted, `GetTankForTeam` widened to `APawn*`,
> and the stale Path A wording corrected across the headers and the two scaffold docs. What remains
> of this blocker is the positive half: actually adding `ITSTankInterface` to
> `ATSTankControllerBase` and its five `BP_*` implementations.

`Source/Tank_Sim_V2/Tank/TSTank.h` declares `ATSTank : public APawn`. Our real tank is
`ATSTankControllerBase : public AWheeledVehiclePawn`, which the master Blueprint has been
reparented onto across seventeen verified port phases. `AWheeledVehiclePawn` is not negotiable —
it is what carries the Chaos vehicle movement component the whole tank depends on.

A grep of `Content/` confirms **no Blueprint references `ATSTank`**. It is a dead class.

**Decision: take Path B.** The scaffold's author anticipated exactly this and documented the
alternative in `TSTankInterface.h`: implement `ITSTankInterface` directly on the existing pawn.

```cpp
class ATSTankControllerBase : public AWheeledVehiclePawn, public ITSTankInterface
```

The five `BP_*` events then become the stable contract, and the Blueprint keeps executing the
tank-specific work exactly as CLAUDE.md's core principle requires. The crew/control/weapon/
commander components get added to the Blueprint's Components panel — **not** created in the C++
constructor, per RULE 1.

`ATSGameState::FindTankForTeam` already returns an untyped pawn, so only the convenience helper
`ATSGameMode::GetTankForTeam` needs its return type widened. Delete `TSTank.{h,cpp}` once that
is done, so nobody integrates against the dead path by mistake.

### Blocker B — three players cannot possess one Pawn, and this breaks work we just shipped

A Pawn has exactly one Controller. "All three players operate the same tank actor" cannot mean
three possessions. The scaffold handles the RPC half of this correctly — every Server RPC is
declared on `ATSTankPlayerController`, which does own a NetConnection — but the consequence for
the tank itself is not yet faced.

**Decision: nobody possesses the tank.** All three crew possess their own `ATSVRPawn`; the tank
is an unpossessed replicated actor driven entirely by validated input arriving through its
components. This is the only arrangement where all three players get an HMD and hands.

This is the right design, and it has a cost we should book now rather than discover in testing:

> **It invalidates the multiplayer turret aiming fix from commits `fb79bd0` / `5dec71a`.**
> `IsTurretSimulatedLocally()` and `ShouldSendAimToServer()` both call `IsLocallyControlled()`
> on the tank. With the tank unpossessed that is **always false on every machine**, so the
> turret gate closes permanently and the aim RPC never fires. The turret would stop working in
> multiplayer — silently, and in the exact direction we just spent a session fixing.

The replacement is a straight substitution, not a redesign: the Gunner's `ATSVRPawn` computes
the aim point locally and calls `ATSTankPlayerController::ServerAimTurret`, which already exists
with validation. Server-side, `UTSTankWeaponComponent` writes the aim point and the existing
`TurretsAndGunsRotCalculation` consumes it unchanged. The gate becomes "am I the server, or am I
the Gunner of this tank" instead of "am I locally controlling this pawn".

Everything else about the turret port survives — the maths, `TurretsRot`/`GunsRot` replication,
and the `ReceivedAimPoint` mechanism all stay. Only the two predicates change.

Related cleanup: the tank Blueprint has its own occupancy notion (`PawnSwitching`,
`Is Vehicle taken?`) and its own `HUD`/`Crosshair` widget references. These overlap
`UTSTankCrewComponent` and the `UTS*HUDWidget` bases. Decide per item whether the framework
supersedes it or the Blueprint keeps it; do not leave two competing owners of "who is in this
tank".

---

## 2. The process risk that will cost the most if ignored

**`.uasset` files are binary. Git cannot merge them.** If two developers on two branches both
edit `BP_TankController_Chaos`, whoever merges second loses their work entirely — no conflict
markers, no partial recovery, just a silent overwrite of one side.

The PDF's git strategy (three long-lived parallel branches) is written as if everything were
text. For this project it is actively dangerous: the master tank Blueprint is the single most
contended asset in the repo and every workstream has a reason to touch it.

**Hard rule for the whole team:**

- **Only Developer 2 edits existing tank Blueprints.** Full stop.
- Developers 1 and 3 create **new** assets only (widgets, VR pawn BP, input assets, maps).
- Anything either of them needs inside a tank Blueprint is a **request to Developer 2**, in the
  same hand-off format RULE 7 already defines: why, where, what, how we verify.
- Keep branches short-lived and merge to the integration branch daily. A week-old branch holding
  Blueprint edits is a week of work waiting to be destroyed.

CLAUDE.md already warns never to `git add -A` here because one checkout serves several sessions.
The same discipline applies with three humans: **always stage with an explicit pathspec.**

---

## 3. The VR workstream

Staged so each stage produces something testable and de-risks the next. Stage order matters:
V0 can invalidate art scope, and V1 must be proved on a flat screen before a headset is involved.

### What already exists (do not rebuild it)
`ATSVRPawn` (216 lines) is further along than it looks. It creates `VROrigin`, `Camera`,
`LeftHand`, `RightHand`; declares 16 Input Action slots; swaps role mapping contexts via
`ApplyRoleMappingContext`, already hooked to PlayerState role changes; and **binds 12 actions and
routes them through the PlayerController RPCs** — `Input_Drive` → `ServerSetDriveInput`,
`Input_FireMainCannon` → `ServerFireMainCannon`, and so on.

So the command path is written. What is missing is everything around it: runtime config, the
Input Action assets, a Blueprint subclass, seating, world-space UI, and voice.

---

### V0 — Prove the VR runtime. This is a GATE, not a task.
**Nothing about VR is configured.** The only rendering line in `DefaultEngine.ini` is
`r.DefaultFeature.MotionBlur=False`. Everything else sits at UE5 engine defaults, and four of
those defaults are actively hostile to VR:

| Default | Problem in VR |
|---|---|
| Lumen GI + Lumen Reflections | Far too expensive for a 90 Hz stereo budget |
| Virtual Shadow Maps | Very costly at VR resolutions |
| TSR / TAA | Ghosts and smears badly in stereo, especially on a moving vehicle |
| Deferred renderer | Blocks MSAA, which VR benefits from more than any post-AA |

Add the startup map at **170 MB** with high-poly tanks authored for flat-screen rendering, and
the honest position is: **we do not yet know this project can hit VR frame budget at all.**

Deliverable: a bare test map, a `BP_TSVRPawn` spawned in it, headset tracking, and a real
`stat unit` figure on the actual target headset and GPU.

Then decide from measurement, not preference: forward shading + MSAA vs deferred + TSR; Lumen off
with baked/simpler GI; conventional shadow maps; instanced stereo on; a starting `vr.PixelDensity`.

It is a gate because a bad answer changes art scope — possibly a VR-specific map and an LOD pass.
That conversation costs days in week 1 and months in month 3.

---

### V1 — One seat, proved on a flat screen first
Settle **Blocker B** before writing VR code: nobody possesses the tank; all three crew possess
their own `ATSVRPawn`.

1. Create `BP_TSVRPawn` from `ATSVRPawn`.
2. Author the VR Input Actions and `IMC_Shared` + `IMC_Driver` / `IMC_Gunner` / `IMC_Commander`.
   All 27 existing `IA_*` are desktop/gamepad and are **not** the VR set — leave them alone.
3. Assign them to the pawn's `EditDefaultsOnly` slots (per RULE 2, C++ never loads them).
4. Implement `BP_SetDriveInput` on the tank so it calls the **existing** `ThrottleControl` /
   `TurningControl`. Do not reimplement driving; those functions are the proven path.

Prove the whole chain with a gamepad, no headset: input → `ServerSetDriveInput` → permission
check → `BP_SetDriveInput` → tank moves, and a Gunner pressing drive is rejected server-side.
Debugging authority bugs and VR bugs at the same time is how weeks vanish.

---

### V2 — Crew seating
Three VR pawns riding a physics-simulated Chaos vehicle. **Highest comfort risk in the project**:
a camera rigidly attached to a vehicle over rough terrain is a classic nausea source.

Run a spike before committing: rigid attach to a socket vs a smoothed follow. Drive rough
terrain and judge comfort. Decide on evidence, not theory.

Seat viewpoints: **Driver** forward and low (periscope/hatch); **Gunner** turret-mounted so the
view rotates with the turret; **Commander** cupola with all-round view plus the intel surface.

Build against **sockets on the interior mesh**, never hardcoded offsets. The interior meshes
under `Content/YI_TankCollection/Mesh/WW2_VK1602Leopard_Interior/` are on this critical path, and
they are currently untracked in git — they need committing before anyone depends on them.

---

### V3 — Fix the aim path. This is a DESIGN DECISION, not wiring.
The single genuine conflict in the VR layer. Three components disagree about what "aim" is:

| Where | Representation | Evidence |
|---|---|---|
| `ATSVRPawn::Input_AimTurret` | 2D stick axis packed into a vector | `ServerAimTurret(FVector_NetQuantize(Axis.X, Axis.Y, 0.f))` |
| `UTSTankWeaponComponent` | a **direction** | `CurrentAimDirection`, defaults to `(1,0,0)` |
| The actual tank turret | a **world-space point** | `ReceivedAimPoint` → `TargetPoint` in `TurretsAndGunsRotCalculation` |

None of these are compatible, and `BP_AimTurret` is not implemented on the tank at all yet, so
nothing currently fails loudly — it would simply never aim.

**Recommendation: standardise on the world-space POINT.** Reasons: it is what the turret actually
consumes; it is already proven working over the network (the `ServerSetAimPoint` fix, verified in
a two-window listen-server test); and a point survives the tank rotating underneath the gunner,
whereas a direction has to be re-based every frame. Rename the contract `AimPoint`, not
`AimDirection`, so the type mismatch cannot silently reappear.

Then, in VR: the Gunner aims with head or controller ray → line trace → world point →
`ServerAimTurret(Point)` → `TryAimTurret` validates Gunner access → replicated → `BP_AimTurret`
feeds the existing turret maths.

**Retire the tank's own `ServerSetAimPoint`** in favour of the PlayerController RPC. This is not
optional: a Server RPC is silently dropped if the calling client does not own the actor, and once
the Gunner possesses a VR pawn rather than the tank, they no longer own the tank. Keeping the
current call would fail with no error at the call site — the exact trap documented in the
networking section.

---

### V4 — World-space UI
Screen-space UMG does not render in VR. Every in-headset widget must be a `UWidgetComponent` in
world space — a physical panel in the cockpit — driven by laser pointer or touch.

The C++ widget bases are written (`UTSDriverHUDWidget`, `UTSGunnerHUDWidget`,
`UTSCommanderHUDWidget`, ...); the WBPs and their cockpit placement are not. Role HUDs read
replicated state only and never mutate it — every server-affecting action goes through
`ATSTankPlayerController`.

Menus (login, session browser, team, role) can be a floating panel in a neutral pre-game space
rather than cockpit-mounted, since they run before anyone is seated.

Keep comfort settings and recentre **local** to the VR layer — they are per-player preferences
and must never touch authoritative state.

---

### V5 — Voice
`UTSVoiceSubsystem` is an abstraction with no backend. **`OnlineSubsystemNull` has no voice
support at all**, so a real provider (EOS is the usual choice) has to be enabled before crew
voice can even be tested. First milestone is one crew channel; push-to-talk, radio channels and
commander broadcast come later.

---

### VR risk register

| Risk | Severity | Mitigation |
|---|---|---|
| Project cannot hit 90 Hz with current art | **High** | V0 gate in week 1, before any VR design work |
| Seat jitter on the physics vehicle causes nausea | **High** | V2 spike; compare rigid attach vs smoothed follow |
| Aim path mismatch shipped as-is | **High** | V3 decision before any Gunner VR work |
| Server RPC silently dropped once the Gunner stops owning the tank | Medium | Route all aim through `ATSTankPlayerController` |
| Interior meshes untracked in git | Medium | Commit them before seating work depends on them |
| No voice backend | Medium | Enable EOS early; it is a project-settings change, not code |

---

## 4. Re-cut developer split

The PDF's split assumed all three workstreams start from nothing. Developer 1's C++ is largely
written, so their package is re-cut toward wiring, proving and hardening rather than authoring.

| | Owns | First deliverable |
|---|---|---|
| **Dev 1 — Multiplayer** | Wiring the framework in: GameMode/GameInstance config, session flow, team/role assignment, tank spawn/assignment, disconnect handling. Hardening the existing C++ against real multi-client tests. | Host + 2 clients join, land in one team, occupy three distinct roles, verified in `UTSTankCrewComponent`'s seats |
| **Dev 2 — Tank gameplay** | Blocker A and Blocker B. `ITSTankInterface` on `ATSTankControllerBase`, crew/control/weapon/commander components added to the Blueprint, the turret aim path rebuilt onto the Gunner. **Sole owner of every tank Blueprint.** Also owns the remaining port phases. | Driver drives and Gunner aims through the validated path, with the permission matrix rejecting the wrong role |
| **Dev 3 — VR/UI/Voice** | §3 (V0–V5) in full. | The **V0 frame-time gate**, then the V2 seating spike |

Dev 2 is the critical path and the contention point — both blockers and exclusive Blueprint
ownership sit there. Expect to load-balance toward them.

---

## 5. Sequencing

**Phase A — unblock (nobody parallelises yet).** Settle Blockers A and B, delete `TSTank`,
point `GlobalDefaultGameMode` at `ATSGameMode`, and get a two-client PIE session reaching the
role-selection screen. Until the contract compiles and runs end-to-end once, parallel work is
speculative. In parallel and independently, Dev 3 runs the §3.1 frame-time gate, since it
touches no shared asset and can invalidate scope.

**Phase B — one working crew.** One team, one tank, three roles, desktop-only, no VR. Prove the
permission matrix and the command path with the existing flat-screen input. Debugging authority
bugs and VR bugs simultaneously is how weeks disappear.

**Phase C — VR on top.** Swap the proven command path onto VR pawns and world-space UI. Because
Phase B fixed the path, a VR bug in Phase C is a VR bug.

**Phase D — voice, multiple teams, polish.**

Milestone gate at each phase: run the Framework Doc §16 Definition-of-Done checklist, which
`Docs/Tank_Simulation_Setup_Guide.md` §10 already expands into concrete verification steps.

---

## 6. Open question: what happens to the Blueprint→C++ port

Phases 0–17 are green; phase 18+ is open and blocked on a design decision about SCS component
access (see CLAUDE.md).

**Recommendation: pause the port at phase 17.** Not abandon — pause. The reasoning:

- The port's whole value is *behaviour preservation*. It is verified by tight numeric equality
  against the current Blueprint. Once three developers are actively changing tank behaviour,
  "identical to before" stops being a meaningful test.
- Every further phase edits `BP_TankController_Chaos`, the exact asset that cannot be merged and
  that Dev 2 needs free for framework integration.
- The port is already delivering its benefit: 60 properties and 5 functions in C++, the class
  reparented, and a stable native base to hang `ITSTankInterface` on.

Resume it after Phase B, when tank behaviour has settled again. Phase 18's SCS-component
question is worth answering then anyway, because the crew components will have established the
pattern for C++ reaching Blueprint-owned components.

This is a judgement call about sequencing, not a technical constraint — if the port's completion
matters more than parallel velocity, it can continue with Dev 2 serialising the two streams.

---

## 7. Where the PDFs need correcting

Recorded so nobody re-derives these from the source documents:

| PDF says | Reality |
|---|---|
| Build the C++ framework from scratch | It exists and compiles (`c47e904`) |
| Tank Blueprint should derive from `ATSTank` | Must not — it needs `AWheeledVehiclePawn`. Use `ITSTankInterface` (Path B) |
| "All three players operate the same tank actor" | True at the actor level; none of them *possess* it |
| Three parallel long-lived git branches | Unsafe with binary `.uasset`. Short branches + exclusive Blueprint ownership |
| Module path `Source/TankSimulation/` | Actual module is `Source/Tank_Sim_V2/` |
| Voice via the configured OSS | `OnlineSubsystemNull` has no voice backend; needs EOS or similar first |

The PDFs also contain "Master Claude Prompt" sections. Those are prompt scaffolding for
generating this work, not requirements — the framework they describe has already been generated.
Treat §17 of the Framework doc and §10 of the Developer doc as historical.
