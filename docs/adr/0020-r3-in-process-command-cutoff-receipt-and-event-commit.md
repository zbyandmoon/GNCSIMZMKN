# ADR-0020: R3 in-process command cutoff, receipt and event commit

- Status: Accepted
- Date: 2026-08-22
- Owner: Repository owner
- Related tasks: R3-SCH-001, R3-TXN-001, R3-LIF-001
- Architecture references: 06 §3, §7, §14, §19; 14 §5–§8, §19, §22
- Extends: ADR-0017 in-process run lifecycle outcomes; ADR-0018 completed-run reset and explicit dispose; ADR-0019 plan-derived in-process cancellation

## Context

The frozen Image and authoritative StepTransaction already commit model state, history and seals. The first command/event consumer now needs one bounded in-process path from submission through a transaction-start cutoff to an owner replacement, application receipt, same-tick event consumption and ModelCommit. The qualification consumer is a small `ModeOwner`; it carries no REF-YYZ scientific meaning. Existing REF-YYZ Image identities, numerical results, sealed payloads and cancellation behavior must remain unchanged.

## Decision

1. The Compiler exposes one optional command/event lowering pass over an already canonical complete plan. A `CommandRoutePlan` resolves one `ModeOwner` `InstantPatch` state block, one `CommandReduction` callsite, one transaction candidate and one later-phase `EventConsumption` callsite. Link converts those references to numeric Image handles. Every route, queue, authority, cutoff, candidate commit-class and event-delivery fact that affects execution enters the descriptor and Image fingerprints. A plan with no command/event extension retains the established REF-YYZ fingerprints.
2. Kernel execution uses numeric route, component, state, callsite, transaction, writer-token and delivery handles plus exact linked typed adapters. Runtime dispatch has no model-name switch, string-keyed type registry, callback manager or general event bus.
3. `Session::submit_command()` remains a single-execution-owner operation. It accepts calls only from an `Initialized` Session at a committed boundary with no open StepTransaction. Lifecycle and open-transaction refusals are pre-admission outcomes: they return `InvalidLifecycle` or `TransactionOpen` with the current ledger sequence and mutate no command control store. `request_cancel()` remains the sole Session mutation allowed concurrently with execution.
4. An admitted submission validates exact RunId, route, target component, payload schema, DecisionAuthority, timing, supersession key, process-local payload type and route capacity. Its `CommandLedgerCommit` uses copy-and-swap publication and advances the Session-local ledger sequence without changing model epoch or tick. The outcome is `Enqueued` or `SubmissionRejected`. An exact retry of the same CommandId and owned request identity returns the original outcome and sequence; conflicting reuse receives `CommandIdConflict`.
5. Each route owns a bounded `RejectNewest` Session queue. `LatestDuePerKey` deterministically supersedes older due entries. Expiration, supersession and terminal disposition produce maintenance receipts and one ledger-sequence increment each; they add no model-epoch increment beyond the enclosing ModelCommit.
6. StepTransaction preparation snapshots the current ledger sequence as its cutoff and builds a private queue/maintenance snapshot. Only unconsumed route entries at or below the cutoff, effective at the base tick and unexpired can become due. The live queue, maintenance receipts and ledger sequence remain unchanged until ModelCommit.
7. A due command reaches its owner only through the compiled route. The typed reducer receives the exact authority, payload view, committed owner view and candidate writer set. `Applied` requires a complete owner-block replacement and one typed event payload. `Rejected` consumes the command without state or event publication. `Deferred` publishes an application-attempt receipt and retains the command for a later transaction.
8. Precommit validation closes candidate ownership, command-to-receipt correspondence, deterministic event identity, queue consumption and ledger-maintenance accounting. ModelCommit publishes the owner replacement, queue snapshot, application receipts, consumed event records, maintenance receipts and ledger sequence in the same no-fail publication region as epoch/tick, history and seals.
9. An applied event id derives from run sequence, base tick, delivery handle and command ledger sequence. The Image requires a consumer phase after the reducer phase. The consumer receives only the routed typed event payload and emits one process-local typed result retained with the committed event. External observers see events only after successful ModelCommit.
10. A reducer or event-consumer invocation failure, or an ObservationSeal failure, discards command staging and leaves the due command retryable at the latest committed boundary. Boundary evaluation, integration, candidate production/validation and final precommit failures are fatal `Invalid` failures even when a command is due. Precommit cancellation discards the same staging. Cancellation observed after a Continue ModelCommit preserves the newly committed state, receipt, event and StepOutcome. A selected Terminal branch runs no command reducer; it publishes only deterministic Expired, Superseded or Terminated maintenance for remaining commands, so terminal completion creates no application evidence. ADR-0026 records this narrowed classification.
11. Successful reset clears ledger, queue, submission outcomes, maintenance receipts, application receipts and events before activating the new run. Failed reset preserves the prior run control evidence. Each Session owns all mutable command/event stores independently. Explicit dispose clears them with the remaining Session resources.
12. Checkpoint/restore, active-run truncation, cross-process transport, serialization, RBAC, dynamic topology, general command management, general event delivery, pause/resume and R4 artifact/workflow publication remain open.

## Consequences

- Submission acceptance proves queue admission only. A committed application receipt proves an owner decision at one ModelCommit.
- A failed or cancelled precommit attempt cannot leak owner state, queue maintenance, application receipt or event visibility.
- Terminal completion remains authoritative over all pending command application.
- The current command and event payloads are process-local C++ objects with exact type identity. They carry no wire-compatibility promise.
- The new consumer is qualification-only. REF-YYZ continues through the existing route-free Image path.

## Executable evidence

- `r3.kernel-command-event.probe` compiles and links the qualification `ModeOwner`, verifies route and fingerprint participation, rejects invalid target/schema/authority/capacity/order and tampered Image facts, and checks exact typed adapter identities before initialization.
- The same probe covers admission, exact retry, command-id conflict, RunId/target/schema/authority/type/timing/capacity rejection, pre/post-cutoff behavior, `Applied | Rejected | Deferred`, supersession, expiration, terminal maintenance, same-tick later-phase event consumption and atomic publication.
- Reducer, event-consumer and observation-seal failures prove rollback followed by exactly-once retry. Candidate validation, projection and final-precommit failures prove rollback followed by a frozen `Failed + Invalid` run. Precommit and postcommit cancellation, successful and failed reset, two-Session isolation and explicit dispose cover lifecycle ownership.
- Existing Compiler, Session and REF-YYZ probes retain their established fingerprints, trajectories, sealed payloads, terminal result and cancellation outcomes.

## Alternatives considered

- A runtime string registry or model-specific Kernel dispatch would weaken linked-entry identity and introduce a second routing authority.
- A general command manager or event bus would add lifecycle, ordering and extension semantics without another executable consumer.
- Publishing maintenance before ModelCommit would leak transaction-local control changes across reducer, consumer, seal or cancellation rollback.
- Applying due commands on Terminal would create application evidence after termination had already become the selected authoritative branch.

## Supersession rule

A second production command owner, multiple delivery phases, durable command/event evidence, cross-process transport, checkpoint participation or concurrent submission requires a successor decision and executable consumer before this contract expands.
