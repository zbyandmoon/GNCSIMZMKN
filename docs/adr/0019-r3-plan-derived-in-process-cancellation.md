# ADR-0019: R3 plan-derived in-process cancellation and safe-point sampling

- Status: Accepted
- Date: 2026-08-22
- Owner: Repository owner
- Related tasks: R3-SCH-001, R3-TXN-001, R3-LIF-001, R3-DIA-001
- Architecture references: 06 §3, §7, §14, §19; 07 §3, §6, §7; 14 §5–§13
- Extends: ADR-0017 in-process run lifecycle outcomes; ADR-0018 completed-run reset and explicit dispose

## Context

The frozen REF-YYZ Image already drives one authoritative Continue/Terminal transaction path. A caller now needs to cancel that work from another thread without exposing asynchronous interruption, a Kernel callback, or a general command/event platform. Cancellation must preserve the transaction boundary, exact RunId identity and already committed evidence.

This decision covers one narrow in-process control operation. CommandLedger, command application receipts, event queues, pause/resume, checkpoint/restore, cross-process transport and executor management remain outside this slice.

## Decision

1. `Session::request_cancel(CancellationRequest)` is the only Session mutation entry allowed to run concurrently with the single execution owner. Initialization, reset, execute, drive, dispose and destruction retain their existing single-owner calling rule.
2. A Session-local mutex protects a dedicated cancellation mirror: lifecycle, exact current RunId, committed epoch/tick snapshot and the single accepted request identity. The execution thread remains the sole owner of model state, frame/candidate/history/seal stores, journals, outcomes and the primary Session lifecycle state.
3. An accepted request is monotonic for its current run. The request carries a caller-owned opaque request id and exact RunId. Repeating the same pair returns `AlreadyRequested`; a competing later request returns `Superseded`; empty identities, a wrong active RunId and requests after Completed return `Rejected`. Request handling only records intent and never unwinds an executing component stack.
4. The Compiler derives `TransactionStart`, `AfterBoundaryCallsite`, `AfterCandidateProducer`, `BeforeModelCommit` and `AfterModelCommit` facts from numeric transaction/component/callsite/candidate links. The policy is frozen in the Image and encoded by the existing Image fingerprint. Kernel validation rejects unknown kinds, zero or duplicate handles, duplicate tuples, unknown references, incompatible subjects and incomplete membership before materialization.
5. The execution owner cooperatively samples only an Image-declared safe point. Sampling performs no callback and launches no work. A request observed before ModelCommit closes the frame and discards candidate, history, seal and journal staging while leaving committed state, epoch, tick, history, seal and result at the latest successful boundary.
6. Precommit observation forms `StepStatus::Cancelled`, `SessionState::Cancelled` and one `RunFinalStatus::Cancelled` outcome with `EvidenceValidity::Valid` and `RunFinalizationStatus::Succeeded`. It emits no RuntimeDiagnostic. Observation after a Continue ModelCommit preserves that `Committed` StepOutcome and the newly committed epoch/tick/history/seal, then freezes the run as Cancelled.
7. A committed Terminal branch freezes Completed before any later cancellation can decide the run. An execution failure that freezes first remains authoritative. Later cancellation cannot replace the Completed or Failed RunOutcome or its primary failure.
8. `run_to_terminal()` returns a narrow `Completed | Cancelled | Failed` drive outcome. A Cancelled Session rejects further execute/initialize/reset operations, retains identity/outcome queries, and permits the same one-time explicit dispose path as other terminal states.
9. Session lifetime must cover every concurrent `request_cancel()` call. The caller must join or otherwise finish concurrent requests before explicit dispose or destruction. The current slice provides no lifetime manager or shared executor ownership.

## Consequences

- Cancellation latency is bounded by the next declared safe point and never depends on asynchronous thread termination.
- Before-commit cancellation preserves bit-exact committed evidence; after-commit cancellation preserves the completed transaction boundary.
- Two Sessions sharing one immutable Image/provider retain independent cancellation intent and outcomes.
- The standard C++ thread runtime is an explicit build dependency of `kernel_session`; no third-party concurrency library is introduced.

## Executable evidence

- `r2.compiler-complete-yyz-plan.probe` verifies exact policy derivation, handle uniqueness and fingerprint participation.
- `r3.kernel-session-materialization.probe` rejects unknown, duplicate, out-of-range, incompatible and incomplete safe-point Images before placement.
- `r3.kernel-step-transaction.probe` uses test-adapter-only rendezvous points to cover cancellation before step one, between boundary callsites, between candidate producers, during final precommit, after ModelCommit, after one committed Continue, after Terminal, after failure, repeated request ids, explicit dispose and two-Session isolation. The existing sequence 0/1/2 replay continues to prove bit-exact non-cancelled science, sealed payload and terminal result.

## Alternatives considered

- Asynchronous thread cancellation: it can cut through package and Kernel stack frames and violates transaction cleanup guarantees.
- Kernel test callbacks at safe points: they would add a production extension surface solely for test coordination.
- A command/event queue reused for cancellation: the current consumer needs one monotonic control bit; queue ordering, receipts and payload routing remain unimplemented R3-SCH scope.
- `SessionError::Cancelled`: normal caller cancellation carries no error diagnostic and requires a direct drive status.

## Supersession rule

A future command/event consumer may extend scheduling around these safe points. Cross-process cancellation, active-run truncation, lifetime management or a new synchronization owner requires a successor ADR before changing this contract.
