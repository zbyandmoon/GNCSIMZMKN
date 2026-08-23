# ADR-0026: R3 boundary-derived termination, fault fragments and retry classification

- Status: Proposed
- Date: 2026-08-23
- Owner: Validation lead; repository-owner acceptance pending
- Related tasks: R3-FIX-001, R3-DIA-001
- Architecture references: 06 §7, §14, §19; 13 §3; 14 §5–§8, §19
- Extends: ADR-0017 in-process run lifecycle outcomes; ADR-0020 in-process command cutoff, receipt and event commit

## Context

The first scheduled stuck-actuator fixture exposed three gaps in the initial pressure slice. The terminal branch was selected only from a fixed clock tick, so an earlier committed ground contact was observed but could not end the run. The actuator owner stored both fault facts and a healthy-command-derived actual position, creating a stale second authority. Finally, the presence of a due command made unrelated boundary, physics, candidate and precommit failures retryable with `Unknown` validity.

The correction must remain Image-driven and package-typed. Kernel cannot interpret `ContactImpactResult`, actuator model identities or diagnostic strings, and the established route-free REF-YYZ Image and fingerprint must remain unchanged.

## Decision

1. An evaluator may declare one optional branch-decision formal output. The contract is the existing typed `TransactionBranch`; its descriptor, implementation history witness, compiled history plan and Image history entry freeze the exact port and numeric slot. The optional facts enter only the extension fingerprint domain, so terminal-only legacy Images retain their existing bytes.
2. A branch-decision evaluator runs at every committed boundary. After boundary callsites and staged event consumption, but before interval candidate production, Session reads only the plan-declared typed slot. `Continue` keeps the interval path, `Terminal` discards the interval path and commits boundary evidence plus terminal maintenance, and `Failure` or an invalid/missing typed value fails closed. The clock terminal remains a maximum-boundary fallback.
3. The stuck-actuator owner stores only `FaultStateFragment { mode, locked_position_radians, revision }`. Healthy actual position is derived each boundary from the current demand; the locked position becomes authoritative only in stuck mode. The reducer replaces the complete fault fragment and no cross-owner write is introduced.
4. Command presence is not a general retry token. Only named command reducer/event-consumer invocation failures and ObservationSeal failures may return `RetryStep + Unknown` while retaining the due command. Boundary evaluation, integration, candidate production or validation, identity/materialization/allocation/internal errors and final precommit failures return `FailOperation + Invalid`, freeze the failed run and reject same-Session retry.
5. A validator that performs allocation cannot promise `noexcept`. Allocation fault injection must be converted by the initialization boundary into the existing structured `AllocationFailure` result and must not terminate the process.

## Consequences

- Ground contact terminates at the first committed boundary that reports it; no post-contact interval candidate is generated.
- Fault rollback is identical to owner rollback because the fault fragment is the complete actuator state authority.
- Retry evidence now distinguishes a deliberately replayable command-side failure from a corrupted or incomplete scientific transaction.
- The Image still contains no package-specific termination interpretation and no runtime topology mutation.
- R3-FIX-001 remains `review`: the two-entity and inactive-child fixtures are separate required slices.

## Alternatives considered

- Reading `ContactImpactResult` or a reason string in Kernel would introduce package dispatch and a second termination authority.
- Treating the fixed clock terminal as the only branch decision allowed a known earlier impact to continue integrating.
- Keeping healthy actual position in the fault owner retained a stale value with no independent state evolution.
- Retrying every failure whenever a command was due made scientific and invariant failures look recoverable.

## Verification

- `r3.kernel-stuck-actuator.probe` proves healthy completion at tick 20, first committed stuck-path impact at tick 10, positive pre-impact altitude, no terminal interval candidates, threshold-sensitive tick movement, and exactly one receipt/event.
- The same probe rejects missing, mistyped and `Failure` branch-decision values; covers reducer/event/seal retry and evaluation/precommit fatal failure; and verifies rollback, deterministic rerun, isolation, reset and checkpoint rejection.
- `r3.kernel-command-event.probe` reverses the former broad-retry expectations for candidate validation and projection failures.
- `r3.kernel-session-materialization.probe` injects allocation failures through Image validation and proves structured unwind rather than process termination.
- The R2 runtime Catalog and complete-plan probes retain their established legacy-path identities and pass alongside the new optional branch contract.

## Supersession rule

A second domain termination consumer, multiple simultaneous branch authorities, a recoverable candidate/precommit protocol, or cross-process branch serialization requires a successor decision and executable evidence before this contract expands.
