# ADR-0023: R3 00A target-rate source overrides

- Status: Accepted
- Date: 2026-08-22
- Owner: Repository owner
- Related task: R3-YYZ-001
- Architecture references: 00A; 15 §4; YYZ reference bundle DEC-002
- Extends: ADR-0015 stateless RuntimeComponent Catalog boundary; ADR-0022 plan-derived held sampled outputs

## Context

The interval-1 REF-YYZ graph and the 2:1 held-output qualification establish the generic Compiler, Image and Session path, but they do not carry the complete 00A cadence. The 00A walkthrough explicitly specifies a 100 Hz base, 100 Hz navigation, 20 Hz guidance, 50 Hz controller, 100 Hz actuator and 25 Hz observation schedule. These frequencies lower to base/navigation/guidance/controller/actuator/observation intervals `1/1/5/2/1/4`, all at zero offset. Architecture volume 15 §4 uses a 10 Hz guidance schedule only as an illustrative example. This ADR freezes the authoritative 00A target-conformance cadence, HeldLatest freshness and programmatic source-lowering scope without changing stable package descriptors or introducing a source parser, scheduler service, observation sink or science verdict.

## Decision

1. The 00A target-conformance profile uses a `0.01 s` base step. Navigation runs every step, guidance every 5 steps, controller every 2 steps, ideal actuator every step and observation scheduling every 4 steps. Every offset is zero.
2. The navigation-to-guidance edge remains `CurrentCycle`. Guidance-to-controller uses `HeldLatest` with a maximum age of 4 steps. Controller-to-actuator uses `HeldLatest` with a maximum age of 1 step.
3. The 20 Hz guidance rate belongs only to this 00A target-conformance profile. The 10 Hz schedule in architecture volume 15 §4 remains an illustrative example.
4. Stable package descriptors retain their package-owned default schedule. An optional programmatic `CompleteStaticCompositionSource` extension identifies exact real occurrences and bindings, supplies occurrence schedule and binding temporal overrides, and carries direct `SourceRef` provenance. The Compiler validates exact endpoint, port, contract, relation, offset and reachable-age consistency before publishing canonical IR.
5. The optional extension has a dedicated conditional semantic-encoding domain. Source semantic hash, descriptor hash, proof identity and Image fingerprint therefore change when target-rate facts change. Sources without the extension preserve the established semantic bytes and interval-1 Image fingerprint.
6. Observation cadence is a static source/plan/proof/Image fact for this R3 target-conformance slice. It does not create an observation runtime sink, R4 Field, Artifact or Dataset.
7. The executable slice uses a production `TruthPassthroughNavigation` RuntimeComponent, the existing real YYZ guidance, controller and ideal actuator, a short run of at least 10 ticks, and a 3000-tick endurance run. Determinism and Session isolation are mandatory evidence.
8. This decision freezes rate shape and temporal freshness only. Canonical scientific inputs, geodetic mapping, real asset selection, the difference report and the final science verdict remain open in `R3-YYZ-001`.

## Consequences

- Product schedule choice is authored in the mission source and remains visible through proof and Image facts.
- Kernel execution continues to dispatch exclusively through numeric Image handles and exact linked entries.
- A narrower maximum input age may deliberately exercise runtime expiration; source authoring cannot widen freshness beyond the oldest sample reachable from the declared cadences.
- The target-conformance label does not claim scientific equivalence or complete Reference A acceptance.

## Executable evidence

- `r3.kernel-multirate-held-output.probe` verifies the optional source override path on the real guidance-to-controller binding, including changed source/descriptor/proof/Image identities, exact validation failures and preservation of the interval-1 fingerprint.
- `r3.kernel-yyz-target-rate.probe` compiles the production navigation→guidance→controller→actuator chain with exact 1/5/2/1 intervals, an observation interval of 4, zero offsets and HeldLatest ages 4/1. It verifies Source→Plan→Proof→Image observation identity, a tick-31 short run with complete cadence/provenance sequences, controlled-versus-zero-controller distinction, bit-deterministic replay and two-Session isolation.
- Two independent 3000-tick executions must both return `Completed`, leave both Sessions in `Completed`, reach terminal tick 3000 and freeze completed RunOutcomes. Any earlier `Failed` is a conformance failure with error, committed tick and detail. All comparable state, committed aggregate, runwide result, rolling-window diagnostic and RunOutcome fields are identical; the caller-owned RunIds deliberately differ.
- The package evaluator retains its accepted earliest-committed-boundary rule. A direct product regression triggers `downrange-goal` at tick 1, then makes tick 2 miss that predicate while meeting a later higher-priority mass condition; the result remains bound to tick 1.
- The target Image retains the package-declared rolling history depth of three as an explicitly named terminal-window diagnostic. That `CommittedMissionResultOutput` covers ticks 2998–3000: approximately 0.02 s and 0.01 kg consumed, with displacement and extrema scoped to the window. A product-owned, per-Session constant-space state owner accumulates committed samples through tick 2999 inside the ordinary model transaction; a depth-one terminal fold adds tick 3000 and emits the formal runwide result. The runwide result covers ticks 0–3000, evaluates 3001 samples and first meets the existing `duration-complete` predicate at 30.0 s. Target mass and downrange predicates remain in finite legal ranges that cannot trigger first, and the former finite-grid-derived mass sentinel is removed. Direct small-sequence equivalence plus rollback, precommit cancellation, reset, checkpoint/restore, dispose and shared-provider isolation checks cover the new state.
- The probe emits `target_conformance science_verdict_pending`. It does not establish scientific equivalence, the 680 kg canonical 00A scenario or an R3 gate verdict.

## Alternatives considered

- A second public YYZ schedule profile would make qualification cadence part of the stable product surface and duplicate mission-composition authority.
- Kernel dispatch on a package, model or profile name would violate the numeric Image boundary.
- A general parser schema or scheduler service would add unused runtime and compatibility obligations.
- Treating the illustrative 10 Hz schedule as the 00A choice would leave the requested 20 Hz target profile unresolved.

## Supersession rule

A different 00A cadence, non-zero offsets, interpolation/extrapolation, asynchronous clocks, a runtime observation sink or a public source-file schema requires a successor decision with an executable consumer.
