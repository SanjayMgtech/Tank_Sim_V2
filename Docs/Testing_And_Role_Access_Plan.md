# Testing Approach & Role-Restricted Access — Plan

Replaces the ad-hoc PIE probe testing used so far. Written after the `SessionCreation` merge
brought in host-as-admin, the UI subsystem and the VR/UI content.

---

## 1. Why the old approach has to change

Everything verified so far was driven by Python probe scripts inside `run_pie_smoke`. That was
right for a fast answer during bring-up and is wrong as the project's testing strategy. Four
concrete reasons, each one already demonstrated rather than hypothetical:

**It silently went stale.** Every crew test used **player 0** as the Driver. `f824b26` made
player 0 the host, and both `TryAssignTeam` and `TryAssignRole` now refuse a host outright. The
tests kept passing only because the built DLL was hours older than the merged source. A test
that cannot tell you it is testing the wrong build is not a safety net.

**It is blind to ownership, which is the main networking failure mode here.** Calling a Server
RPC through `call_method` on a local PlayerController executes it locally regardless of who owns
the actor. Real clients do not get that: an RPC from a non-owning client is **silently dropped
with no error at the call site**. So the probe approach structurally cannot catch the single
trap this codebase most needs to catch — the one already documented against
`ShouldSendAimToServer`.

**It tests no replication at all.** `CreatePlayer` adds *local* players to one world. Nothing
crosses a network boundary, so `OnRep_` paths, `DOREPLIFETIME` registration and relevancy are
all unexercised.

**It is not repeatable.** The probes lived in tool calls, not in the repo. Nobody else can run
them, CI cannot run them, and a regression next month reproduces nothing.

One more, worth keeping as a lesson: a probe reported Driver-drive and Gunner-aim as **broken**
when they were fine. Unreal's Python bindings return a *live view* of a struct property, so
reading `CurrentDriveInput` before and after aliased the same memory. Int-typed reads compared
correctly, struct-typed reads never did. **Capture scalars, not struct references, when diffing
state across a call.**

---

## 2. Bug classes, and what actually catches each

The point of tiering is that each tier catches something the others cannot. Do not build one
tier and assume coverage.

| Bug class | Example in this project | Caught by |
|---|---|---|
| Pure logic | permission matrix cell wrong | **T1** automation |
| State machine | seat exclusivity, host gating, disconnect release | **T1** automation |
| Privilege escalation | non-host calling `ServerHostAssignPlayerToRole` | **T1** automation |
| Gameplay integration | crew seated, tank spawned per team, drive reaches the Blueprint | **T2** functional map |
| **Ownership** | Server RPC dropped because the caller does not own the actor | **T3** two-window only |
| **Replication** | `OnRep_AimPoint` never fires on a remote client | **T3** two-window only |
| Rendering / comfort | VR frame budget, seat jitter | **T4** headset only |

T3 and T4 are irreducibly manual. Everything above them should stop being manual.

---

## 3. Tier 1 — automated C++ tests (build first)

`Source/Tank_Sim_V2/Tests/`, using Unreal's automation framework. Run headless, no editor
interaction, no PIE. Already reachable through the existing `run_automation_tests` action.

Start with the tests that need no world at all — `FTSPermissions` is a pure static function, so
the whole matrix is a table-driven test with zero setup:

1. **Permission matrix** — all `ETSCrewRole` × `ETSCapability` pairs against the Section 8 table,
   including the single `Limited` cell (Gunner + RadarIntel) that a boolean check flattens.
2. **Crew occupancy** — occupy, reject a second occupant, release, re-occupy; `HasAccess` true
   only for the actual occupant.
3. **Host gating** — a PlayerState with `bIsHost` is refused a team and a seat.
4. **Privilege escalation** — a non-host `ServerHostAssignPlayerToRole` changes nothing.

These four cover the logic that today has no protection at all.

---

## 4. Tier 2 — functional tests in a map

`AFunctionalTest` actors in a dedicated map, for flows that need a world, a tank and components.

- Team assignment spawns exactly one tank per team.
- Three crew occupy three distinct seats on the same tank.
- Crew pawns attach to the correct seat components.
- Drive input reaches `BP_SetDriveInput` and the tank moves **on real ground**.
- Cross-tank denial (see §6) — needs two teams, so it belongs here rather than T1.

**The test map needs ground with traction.** `M_FrameworkTest` currently has a scaled cube
floor, and the tank gets almost no grip on it — a drive test there moved ~4 units where the
demo map's terrain gives ~370. Any "does it drive" assertion in that map is meaningless until
this is fixed.

---

## 5. Tier 3 — the two-window listen-server test

Manual, human-only, and the only thing that proves ownership and replication. Keep it to a short
written script run at milestones, not continuously:

1. Host + 2 clients; host stays admin, clients take Gunner and Driver.
2. Gunner aims — turret moves **on the host's screen** (proves the client→server path).
3. Host drives its own camera — clients see the tank turret smoothly (proves server→client).
4. A client attempts a role it does not hold — refused.
5. Disconnect a client — the seat frees and becomes selectable again.

Step 2 is the one that cannot be faked in single-process PIE.

---

## 6. Role-restricted access — current state

| Mechanism | State |
|---|---|
| `FTSPermissions` matrix | ✅ complete, authoritative, matches the documented table |
| Two-factor check (capability + seat on *this* tank) | ✅ in Control / Weapon / Commander |
| Per-capability checks | ✅ fixed — each weapon action names its own capability |
| Host cannot hold team or seat | ✅ enforced in both entry points |
| Host RPCs re-check `IsMatchHost` server-side | ✅ present |
| `UTSPermissionLibrary` for UI | ✅ read-only view for display decisions |
| Role input contexts (`IMC_Driver/Gunner/Commander`) | ✅ exist — **UX, not security** |

### Gaps, ranked by risk

**1. Cross-tank access is untested.** Every permission test so far used one tank. The second
factor — "does this player occupy that seat *on this tank*" — is exactly what stops a Team B
Gunner operating Team A's tank, and nothing has ever exercised it. Highest risk because the code
looks correct and has never been challenged.

**2. Commander has no verified action.** `TryIssueCommand` and the intel refresh have never run.
Commander is the only role with zero proven capability.

**3. The one `Limited` cell is unverified.** Gunner + RadarIntel returns `Limited`, and
`UTSTankCommanderComponent` filters intel on it. Never exercised, and a boolean `HasFullAccess`
check anywhere in that path would silently collapse it to denied.

**4. `Reload` has no capability row.** It is gated on `MainCannon` by convention. Fine, but it
should be a deliberate row in the enum or a comment at the check, not an accident.

**5. `UTSRoleDefinition` is unused.** Three `DA_Role_*` assets exist; no C++ references them. The
role HUDs should consume the display name / colour / icon, or the assets should go.

**6. Input contexts must never be mistaken for access control.** Swapping `IMC_Gunner` off a
Driver stops the *button* existing, not the *request*. Server validation is the security boundary;
the contexts are ergonomics. Worth stating explicitly so nobody later "simplifies" a server check
away on the grounds that the input is already restricted.

---

## 7. Suggested order

1. T1 automation tests (§3) — cheapest, protects logic that has no protection today.
2. Fix the test map's ground (§4), since several T2 assertions depend on it.
3. Cross-tank denial test (gap 1) — highest-risk untested path.
4. Commander path + the `Limited` cell (gaps 2, 3).
5. Write down the T3 script (§5) and run it once against the merged build.
6. Housekeeping: `Reload` capability, wire or delete `UTSRoleDefinition`.
