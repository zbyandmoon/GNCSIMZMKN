# ADR-0025: R3 00A passive attitude and exact qualification profile

- Status: Accepted
- Date: 2026-08-23
- Owner: Repository owner
- Related task: R3-YYZ-001
- Architecture references: 00A; 03 §8; 15 §2–§9; YYZ reference bundle DEC-001 and DEC-004–DEC-011
- Extends: ADR-0007 passive Hamilton quaternion convention; ADR-0023 target-rate source overrides; ADR-0024 launch-local ENU initial mapping

## Context

The canonical 00A source needs an attitude that relates launch-local ENU axes to body forward/right/down axes. It also needs one traceable scientific profile built from the R0 qualification assets already accepted by the repository owner. The owner-authorized 00A values preserve initial state, duration and cadence and select one synthetic aerodynamic fixture successor. Gains, inertia, propulsion and numerical formulas remain byte-for-byte fixed. The frozen aerodynamic asset remains unchanged and every true asset-domain failure stays visible.

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
5. `AltitudePitchGuidanceKernel` obtains its signed pitch from the body-forward axis for a general valid attitude. The existing pure-`B-y` arithmetic is preserved exactly for the frozen fixture path; the canonical level east-heading attitude yields zero measured pitch. Direct cases cover heading `37 deg` with flight-path angle `12 deg`, the same forward direction with bank `43 deg`, the expected pitch sign and `q/-q` equivalence.
6. The canonical mission is `mission.yyz.00a.abstract-engineering-baseline@1` under plan `plan.yyz.00a.abstract-engineering-baseline`, subject `vehicle.yyz.00a.abstract-engineering@1`, clock `clock.yyz.00a.100hz@1` and profile `profile.yyz.00a.abstract-engineering-baseline@1`. Its author facts are the exact values recorded in `fixtures/ref-yyz-00a-canonical/source.json`. After correcting the embedded Image evidence value, the frozen source has SHA-256 `d3de4d75b6904dcc4dfea50636b2a2861fb981486a6ab4c64fa5b6819f77684d`; science inputs and indexed asset payloads are unchanged.
7. DEC-005 through DEC-010 continue to select six unchanged revision-1 R0 roles from `asset-index.fixture.yyz.r0-qualification@1`: `environment.fixture.yyz.uniform@1`, `mass-properties.fixture.yyz.constant-geometry@1`, `propulsion-response.fixture.yyz.main@1`, `guidance-control.fixture.yyz.altitude-pitch@1`, `numerical.fixture.yyz.rk4-frozen-interval@1` and `termination-observation.fixture.yyz.two-interval@1`. The canonical aerodynamic role selects `aero-table.fixture.yyz.00a-synthetic-multiaffine@2`, SHA-256 `59673c1505fc6d73648c546e3ac78d80317532e9d9799771a0ca780fcc3f855f`, while `aero-table.fixture.yyz.multiaffine@1` remains frozen. The successor copies every original corner exactly and deterministically evaluates the single multiaffine relation fixed by those eight corners at each added node.
8. The approved profile keeps the opening state, `680 kg` opening mass, `1000 m` altitude command, `30 s` duration, `0.01 s` base step and cadence intervals `1/5/2/1/4`. Its initial successor used Mach `[0.2,0.8]` with the old alpha/beta bounds. A real Session reached tick 9 and exposed the finite query `(Mach,alpha,beta)=(0.617102,0.113627,0)`. The single authorized adjustment retains Mach `[0.2,0.8]` and adds the complete finite `atan2` coordinate ranges alpha `[-pi,pi]` and beta `[-pi/2,pi/2]`. Every query still uses the generic strict trilinear kernel; clamping, runtime extrapolation and automatic domain growth remain excluded.
9. DEC-011 uses `tools.yyz-00a-canonical-decimal-reference@1`: an 80-digit Decimal implementation that reads only the frozen source and asset bytes, verifies successor generation and old-domain equivalence, independently re-expresses mapping, cadence/held timing, opening air data and the opening coefficient lookup, and uses exact identity/status comparison plus per-field absolute and relative tolerance `2e-12`. It does not implement a second 3000-step Session or RK4 trajectory.
10. The canonical Image fingerprint is `65e00515234efcabfae14399e9fe6838d65248af2d9a598f50b09a3470131078`. This replaces the stale documentation value `4faccb3be9c6b760591b286c8996322069c7e0c1ba1668dcdf8946fb36c263ff`; clean MSVC and GCC builds produce the corrected value from the same accepted source and asset facts. The prior domain-locked canonical fingerprint was `e981118b136b3e6872da40a00c296153b9ad26e144c7882341b64b30b219574c`; the interval-1 fingerprint remains `7d1fbe1fa09ca555420ed3cc14a05d1f2994c6501b17cd3991355520fc8e6f14`.
11. The exact opening relative airspeed is `210 m/s`; with the accepted `340 m/s` sound speed, Mach is `0.6176470588235294`. The frozen asset still returns `OutOfRange`. The successor returns `Success` with independent coefficients `(CA,CY,CN,Cl,Cm,Cn)=(0.04470588235294118,0,0,0,-0.03764705882352941,0)` within tolerance, and tick 1 commits through four RK4 derivative evaluations.
12. Two real canonical Sessions reach tick 3000 with `3000` committed intervals and a `Completed / duration-complete` terminal result. Cadence counts are navigation `3001`, guidance `601`, controller `1501` and actuator `3001`; maximum `HeldLatest` ages remain `4/1`, and nonzero actuator output reaches an RK4 held form. The representative MSVC capture has query envelope Mach `[0.30237994030302495,0.61764705882352944]`, alpha `[-3.1412239572488261,3.1397707600310416]` and beta `[-1.4925695311254403,1.4441212960610592]`. State, aggregate, terminal window, RunOutcome, envelope and cadence are bit-deterministic across both runs in one build/process. Cross-tool qualification does not claim bit-identical long-horizon state: a one-ULP attitude difference first observed between MSVC and GCC accumulates in this nonlinear synthetic trajectory. Difference-report revision 3 therefore retains one rich build-local capture while its frozen cross-tool projection requires exact identity/status/cadence/held/terminal facts, independently tolerated opening and terminal mass, a finite ordered in-domain envelope containing the opening query, and build-local bit determinism. The compact report has `unresolved_count == 0` and emits `abstract_engineering_target_conformance` within the declared abstract-engineering claim. Real-aircraft accuracy, cross-tool long-horizon state equality, stability, handling quality and certification remain outside that claim.

## Consequences

- DEC-001 and DEC-004–DEC-011 have concrete source, product and oracle selections for this abstract engineering profile.
- Exact asset provenance includes the frozen R0 base and its explicit synthetic successor; the selected applicability domain contains every observed canonical query.
- Product, independent reference and old target-rate qualification retain separate identities and evidence.
- The terminal product evidence supports the bounded `abstract_engineering_target_conformance` verdict. The report retains the remaining science gaps without widening this claim.

## Executable evidence

- `r3.yyz-00a-initial-mapping.probe` covers the ENU/body basis, passive round-trip, `q/-q`, strict non-unit rejection and `NormalizeWithFlag`.
- `r2.yyz-static-product-contracts.probe` covers both general-attitude guidance cases, pitch sign, `q/-q` and the strengthened constant-space committed-accumulator invariants.
- `r3.kernel-yyz-00a-canonical.probe` compiles the distinct canonical Source/Plan/Proof/Image, proves old/new asset behavior, rejects malformed asset/Image inputs, commits tick 1 and completes two deterministic tick-3000 Sessions.
- `r3.yyz-00a-canonical.oracle` runs the independent frozen opening reference and validates the revision-3 portable projection of `reports/r3-yyz-001-00a-difference.json`. Both MSVC and GCC retain their build-local raw observation while matching the same exact/domain/tolerance contract; maximum portable absolute error is `1.364e-11` on terminal mass within the combined tolerance, and `unresolved_count` is 0.

## Alternatives considered

- Hard-coding `[0,1,0,0]` would hide the general basis conversion and quaternion double-cover behavior.
- Scaling inertia, modifying gains, changing propulsion or mutating the frozen aerodynamic asset would break the accepted profile boundary.
- Clamping or runtime extrapolation would hide applicability and remove the strict query evidence.
- Treating the target-rate engineering run as the canonical scientific trajectory would merge distinct initial states, masses and Image identities.

## Supersession rule

Changing the passive attitude direction, body axes, bank sign, angular-rate convention, canonical author facts, selected asset bytes, model applicability domain, numerical method, tolerance policy or terminal claim requires a successor ADR and independent executable evidence.
