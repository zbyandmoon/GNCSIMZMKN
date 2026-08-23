# ADR-0025: R3 00A passive attitude and exact qualification profile

- Status: Accepted
- Date: 2026-08-23
- Owner: Repository owner
- Related task: R3-YYZ-001
- Architecture references: 00A; 03 §8; 15 §2–§9; YYZ reference bundle DEC-001 and DEC-004–DEC-011
- Extends: ADR-0007 passive Hamilton quaternion convention; ADR-0023 target-rate source overrides; ADR-0024 launch-local ENU initial mapping

## Context

The canonical 00A source needs an attitude that relates launch-local ENU axes to body forward/right/down axes. It also needs one traceable scientific profile built from the exact R0 qualification assets already accepted by the repository owner. The owner-authorized 00A values may replace initial state, duration and cadence. Gains, inertia, aerodynamic rows, propulsion and numerical formulas remain byte-for-byte fixed. A true asset-domain failure must stay visible.

## Decision

1. Attitude uses the accepted passive Hamilton `q_I_B = [w,x,y,z]` convention. Body axes are `+x forward`, `+y right`, `+z down`; positive bank is right-wing-down. Angular rate remains the accepted body-expressed `omega_BI_B` convention and opens at `[0,0,0] rad/s`.
2. With heading `chi`, flight-path angle `gamma` and bank `phi`, the ENU basis vectors are constructed as

   ```text
   f  = [cos(gamma) sin(chi), cos(gamma) cos(chi), sin(gamma)]
   r0 = [cos(chi), -sin(chi), 0]
   d0 = f cross r0
   r  = cos(phi) r0 + sin(phi) d0
   d  = -sin(phi) r0 + cos(phi) d0
   ```

   The body-to-ENU matrix has columns `[f,r,d]`. Quaternion conversion starts from the transposed Eigen active matrix, normalizes the finite nonzero result, and round-trips through the repository passive rotation function. No quaternion coefficient sign is selected from a memorized special case.
3. For heading `90 deg`, flight-path angle `0 deg` and bank `0 deg`, forward maps to East, right maps to South and down maps to Down. The canonical double-precision quaternion is `[0, 1, 3.0615158845559431e-17, 0]`; `[0,-1,-3.0615158845559431e-17,0]` is the same rotation. The independent Decimal reference converges to the analytic representative `[0,1,0,0]` within the declared field tolerance.
4. Strict consumers reject a non-unit quaternion. `NormalizeWithFlag` consumers normalize a finite nonzero input and report the correction. Zero norm remains a domain error. Direct tests cover both policies and `q/-q` equivalence.
5. `AltitudePitchGuidanceKernel` obtains its signed pitch from the body-forward axis for a general valid attitude. The existing pure-`B-y` arithmetic is preserved exactly for the frozen fixture path; the canonical level east-heading attitude yields zero measured pitch.
6. The canonical mission is `mission.yyz.00a.abstract-engineering-baseline@1` under plan `plan.yyz.00a.abstract-engineering-baseline`, subject `vehicle.yyz.00a.abstract-engineering@1`, clock `clock.yyz.00a.100hz@1` and profile `profile.yyz.00a.abstract-engineering-baseline@1`. Its author facts are the exact values recorded in `fixtures/ref-yyz-00a-canonical/source.json`. The frozen source has SHA-256 `b4e045237a87d85592b19d62863df95065ecf956146c4a131d7c0d890c41ab98`.
7. DEC-005 through DEC-010 select the seven existing R0 qualification roles from `asset-index.fixture.yyz.r0-qualification@1`, revision 1: `environment.fixture.yyz.uniform@1`, `mass-properties.fixture.yyz.constant-geometry@1`, `aero-table.fixture.yyz.multiaffine@1`, `propulsion-response.fixture.yyz.main@1`, `guidance-control.fixture.yyz.altitude-pitch@1`, `numerical.fixture.yyz.rk4-frozen-interval@1` and `termination-observation.fixture.yyz.two-interval@1`. The index SHA-256 is `570750a2d54d0a2ba67689b3da553ff100dfdf07bae4bddf7897de1f55ae122e`; the selected frozen-interval and mission-composition payload files have SHA-256 `17609b48b02c43bfe9e103218cba04210bcb93938fbfbc332935225461cf0bc6` and `c506a2e5c3e193de40f0cd3815df077f6a1dcd48e592671949d8eacf453a5a2d`.
8. The only profile overrides are the approved opening state, `680 kg` opening mass, `1000 m` altitude command, `30 s` duration, `0.01 s` base step and cadence intervals `1/5/2/1/4`. Inertia, gains, aerodynamic table, propulsion values, closure and RK4 equations retain their accepted bytes and applicability domains.
9. DEC-011 uses `tools.yyz-00a-canonical-decimal-reference@1`: an 80-digit Decimal implementation that reads only the frozen source and asset bytes, independently re-expresses mapping, cadence/held timing and the available opening scientific chain, and uses exact identity/status comparison plus per-field absolute and relative tolerance `2e-12`. The frozen expected and daily verifier are separate from the generators.
10. The canonical Image fingerprint is `e981118b136b3e6872da40a00c296153b9ad26e144c7882341b64b30b219574c`. The interval-1 fingerprint remains `7d1fbe1fa09ca555420ed3cc14a05d1f2994c6501b17cd3991355520fc8e6f14`.
11. The exact opening relative airspeed is `210 m/s`; with the accepted `340 m/s` sound speed, Mach is `0.6176470588235294`. The selected aerodynamic asset declares the closed Mach domain `[0.2,0.6]`, so the first Session step fails closed at tick 0 with `OutOfRange / table-query`, commits zero intervals and retains tick/epoch 0. No asset value is adjusted to obtain a trajectory.
12. A candidate terminal science verdict requires `unresolved_count == 0`. The current report has one unresolved item: the requested tick 1–3000 trajectory is unavailable after the opening aerodynamic-domain failure. The accepted claim is limited to canonical mapping/source identity, deterministic fail-closed product execution and independent opening/domain agreement. It excludes real-aircraft accuracy, stability, pitch overshoot and a completed 30-second trajectory.

## Consequences

- DEC-001 and DEC-004–DEC-011 have concrete source, product and oracle selections for this abstract engineering profile.
- Exact asset provenance is closed while the selected aerodynamic applicability domain exposes one owner-level scientific incompatibility.
- Product, independent reference and old target-rate qualification retain separate identities and evidence.
- The terminal science verdict remains withheld until an approved source/asset combination can execute the requested horizon and the independent difference report reaches zero unresolved items.

## Executable evidence

- `r3.yyz-00a-initial-mapping.probe` covers the ENU/body basis, passive round-trip, `q/-q`, strict non-unit rejection and `NormalizeWithFlag`.
- `r2.yyz-static-product-contracts.probe` covers the general-attitude guidance consumer and the strengthened constant-space committed-accumulator invariants.
- `r3.kernel-yyz-00a-canonical.probe` compiles the distinct canonical Source/Plan/Proof/Image, initializes a real Session and reproduces the exact tick-0 aerodynamic-domain failure.
- `r3.yyz-00a-canonical.oracle` runs the independent frozen reference and validates `reports/r3-yyz-001-00a-difference.json`. The available opening fields pass; maximum absolute error is `1.347066989204615e-14` at tick 0 on the near-zero North velocity, and `unresolved_count` is 1.

## Alternatives considered

- Hard-coding `[0,1,0,0]` would hide the general basis conversion and quaternion double-cover behavior.
- Scaling inertia, modifying gains, reshaping the aerodynamic table or changing propulsion would break the exact accepted asset promotion.
- Widening the comparator tolerance would not resolve the missing trajectory.
- Treating the target-rate engineering run as the canonical scientific trajectory would merge distinct initial states, masses and Image identities.

## Supersession rule

Changing the passive attitude direction, body axes, bank sign, angular-rate convention, canonical author facts, selected asset bytes, model applicability domain, numerical method, tolerance policy or terminal claim requires a successor ADR and independent executable evidence.
