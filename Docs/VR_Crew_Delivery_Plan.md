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
├─ Tank/        TSTank, TSTankInterface, Crew/Control/Weapon/Commander components
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
| Blueprint→C++ port | 🟡 phases 0–17 green, phase 18+ open |

---

## 1. Two blockers to settle before anyone writes parallel code

### Blocker A — `ATSTank` is the wrong base class, and nothing uses it

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

The genuinely new build. Nothing below exists today.

### 3.1 Enable and prove the VR runtime — do this first, alone

OpenXR is enabled as a plugin but has no project configuration. Before designing anything,
establish that a headset renders this project at an acceptable frame rate.

**This is a gate, not a task.** The startup map is 170 MB and the tanks are high-poly assets
built for flat-screen rendering. VR is roughly 2× the pixels at a hard 90 Hz deadline, and a
miss is nausea rather than a dropped frame. If the existing content cannot hit frame budget, the
answer is a scope conversation about a VR-specific map and LODs — and it is far cheaper to have
that conversation in week 1 than in month 2.

Deliverable: a bare VR test map, `ATSVRPawn` subclass spawned, headset tracking, a frame-time
number from `stat unit` on the real target hardware.

### 3.2 Crew seating

Three VR pawns must ride a physics-simulated tank. Attaching a camera to a fast-moving Chaos
vehicle is a known source of jitter and motion sickness, and it is the highest-risk unknown in
the VR layer.

Run a **spike** before committing to an approach: attach a VR pawn to the tank mesh at a socket,
drive over rough terrain, and judge comfort. Compare against the alternative of a smoothed
follow rather than a rigid attach. Decide on evidence.

Each role needs a seat transform and a sensible default view:
- **Driver** — forward, low, periscope or hatch view.
- **Gunner** — turret-mounted so the view rotates with the turret; sight/optics view.
- **Commander** — cupola, all-round view, plus the radar/intel surface.

The interior meshes currently being authored under
`Content/YI_TankCollection/Mesh/WW2_VK1602Leopard_Interior/` are on this critical path. Until
they land, seat positions are provisional — build against sockets, not hardcoded offsets.

### 3.3 VR input

All 27 existing `IA_*` actions are desktop/gamepad. VR needs its own.

- Shared actions: Interact, Grab, Primary, Secondary, Menu, Recenter (`ATSVRPawn` already
  declares the slots and `51d4dc2` added Primary/Secondary).
- One Input Mapping Context per role, swapped by the existing
  `ATSVRPawn::ApplyRoleMappingContext`, which is already wired to fire on PlayerState role
  changes.
- **Input never reaches the tank directly.** It becomes a request on the PlayerController. The
  tank's own Enhanced Input bindings stay for desktop/debug play but are not the VR path.

### 3.4 VR UI

Screen-space UMG does not work in VR. Every widget the crew sees in-headset must be a
`WidgetComponent` in world space — a physical panel in the cockpit — with laser-pointer or
touch interaction. The C++ widget bases are done; the WBPs and their VR placement are not.

Menus (login, session browser, team, role) can be a floating panel in a neutral pre-game space
rather than cockpit-mounted.

### 3.5 Voice

`UTSVoiceSubsystem` exists as an abstraction with no backend. First milestone is one crew
channel. Note `OnlineSubsystemNull` is LAN-only with no voice backend — a real provider
(EOS being the usual choice) has to be enabled before voice can be tested at all.

---

## 4. Re-cut developer split

The PDF's split assumed all three workstreams start from nothing. Developer 1's C++ is largely
written, so their package is re-cut toward wiring, proving and hardening rather than authoring.

| | Owns | First deliverable |
|---|---|---|
| **Dev 1 — Multiplayer** | Wiring the framework in: GameMode/GameInstance config, session flow, team/role assignment, tank spawn/assignment, disconnect handling. Hardening the existing C++ against real multi-client tests. | Host + 2 clients join, land in one team, occupy three distinct roles, verified in `UTSTankCrewComponent`'s seats |
| **Dev 2 — Tank gameplay** | Blocker A and Blocker B. `ITSTankInterface` on `ATSTankControllerBase`, crew/control/weapon/commander components added to the Blueprint, the turret aim path rebuilt onto the Gunner. **Sole owner of every tank Blueprint.** Also owns the remaining port phases. | Driver drives and Gunner aims through the validated path, with the permission matrix rejecting the wrong role |
| **Dev 3 — VR/UI/Voice** | §3 in full. | §3.1's frame-time gate, then seating spike |

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
