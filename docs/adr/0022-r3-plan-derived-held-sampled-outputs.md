# ADR-0022: R3 plan-derived held sampled outputs

- Status: Accepted
- Date: 2026-08-22
- Owner: Repository owner
- Related tasks: R3-SCH-001, R3-TXN-001, R3-LIF-001
- Architecture references: 04 temporal contracts; 06 store ownership; 14 §4, §5, §7–§10
- Extends: ADR-0021 process-local checkpoint and branch restore

## Context

The existing REF-YYZ Image and Session support current-cycle sampled values and transaction-local `IntegrationHeld` values. A real multi-rate consumer also needs an explicit plan-owned zero-order hold when its producer is skipped on a consumer tick. The first executable consumer is a qualification-only profile of the package-owned guidance-to-controller edge.

## Decision

1. `HeldLatest` is an explicit sampled-signal temporal relation. A `CurrentCycle` edge whose consumer can run while its producer is skipped fails compilation. This slice supports zero-order hold only.
2. The Compiler derives one committed-output slot for each `HeldLatest` binding. The Image freezes its source slot, typed layout and codec, coordinator writer, exact reader callsites, producer cadence, and consumer maximum age. These facts enter existing proof, descriptor-hash, and Image-fingerprint mechanisms. An Image without a held edge retains the established interval-1 encoding and fingerprint.
3. Ordinary current-cycle outputs remain in the bounded `CycleFrame`. `IntegrationHeld`, terminal results, telemetry, and observation copies do not enter the committed-output store.
4. On a producer tick, the fresh source value flows through the current frame and a validated deep clone is staged for publication. On a skipped producer tick, Session injects the latest committed typed sample into the new frame before the authorized consumer runs. Original sequence, sample tick/time, interval, quality, and payload remain unchanged; freshness and age are derived from the current tick.
5. Missing history, invalid quality or identity, and age beyond the compiled limit fail before product-consumer invocation. Session performs no default construction, interpolation, extrapolation, or runtime name lookup.
6. Held replacements publish with state, epoch/tick, history, and seals at the same prevalidated no-fail `ModelCommit`. Execution failure, rollback, or precommit cancellation discards staging; postcommit cancellation retains the published sample. Continue and Terminal use the same rule.
7. Each Session owns its typed held values. Successful completed-run reset clears them before the new run begins; failed reset preserves the prior committed store; dispose destroys each value once. Route-free checkpoint and RestoreCommit deep-clone and validate every held sample and its exact authority under ADR-0021.
8. The qualification Image schedules package-owned guidance every two base ticks at offset zero and the package-owned controller every tick. This profile proves framework behavior only. It does not select the 10 Hz or 20 Hz guidance rate for the complete 30-second YYZ product profile.

## Executable evidence

- `r3.kernel-multirate-held-output.probe` compiles and links the qualification profile through the ordinary product-definition path. It locates schedule, hold, slot, codec, reader/writer, maximum-age, proof, descriptor-hash, and fingerprint facts, while rejecting a cross-rate `CurrentCycle` edge.
- The probe observes fresh tick 0, held tick 1 with preserved tick/sequence/quality and age one, then fresh tick 2 replacement. The real controller consumes both forms and produces a checked formal moment command.
- Direct failures cover missing and expired samples, store/injection clone, validation, later-call and final-precommit rollback, pre/postcommit cancellation, shared-provider Session isolation, reset, dispose, checkpoint/restore corruption, two-child continuation, and parent-cancel/child-continue isolation.
- The interval-1 Image fingerprint remains `7d1fbe1fa09ca555420ed3cc14a05d1f2994c6501b17cd3991355520fc8e6f14`; existing REF-YYZ terminal, oracle, cancellation, reset, and branch-restore probes remain required regressions.

## Deferred scope

- first-order hold, interpolation, extrapolation, unavailable-value policy, external snapshots, and asynchronous clock domains;
- a general rate-adapter framework, runtime registry, manager, serializer, wire schema, and compatibility migration;
- command/event checkpoint participation and durable checkpoint representation;
- the complete 30-second YYZ rate selection, oracle, and difference report.

## Supersession rule

Any additional hold mode, cross-clock transport, durable representation, or complete YYZ rate choice requires a successor decision backed by its executable consumer.
