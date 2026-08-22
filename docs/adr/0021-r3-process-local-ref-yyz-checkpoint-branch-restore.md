# ADR-0021: R3 process-local REF-YYZ checkpoint and branch restore

- Status: Accepted
- Date: 2026-08-22
- Owner: Repository owner
- Related task: R3-LIF-001
- Architecture references: 06 §3, §7, §14, §19; 14 §8–§13
- Extends: ADR-0017 in-process run lifecycle outcomes; ADR-0018 completed-run reset and explicit dispose
- Related: ADR-0022 plan-derived held sampled outputs

## Context

The route-free REF-YYZ Session already executes from a frozen `ExecutionPlanImage`, commits state/history/seals at explicit boundaries, and retains typed process-local values through Image-linked materializers. R3 now needs one executable branch point without introducing persistence, source recompilation, a checkpoint manager, or command identity semantics.

The checkpoint must remain usable after its source Session continues, resets, disposes, or is destroyed. Restore must start in a separate `Created` Session, rebuild preparation and Runtime Cell objects through the normal Image lifecycle, and publish no partial child run when any compatibility, materialization, clone, or final-precommit check fails.

## Decision

1. `Session::checkpoint()` supports only an `Initialized` route-free Session at a closed committed boundary. A CycleFrame, active StepTransaction, ModelCommit staging, missing committed run identity, command route, or event delivery rejects the request before snapshot publication.
2. A checkpoint is an immutable process-local object. Its identity consists of parent RunId, parent run sequence, committed epoch, committed tick, and committed step count. It also retains the exact RunBinding, Image fingerprint, and the same in-memory Image object used by the source Session. No new hash, codec, registry key, or serialized identifier is introduced.
3. Snapshot ownership includes deep clones of every committed state block, all committed evaluator-history samples, every plan-declared committed `HeldLatest` sample, the current sealed boundary and its typed output values, plus CycleFrame generation/sequence coordinates needed for deterministic continuation. Each value retains exact size, alignment, layout, codec, role, type identity, original sequence and sample timing, quality, hold authority, and invariant evidence.
4. Candidate state, open frame values, `IntegrationHeld`, unsealed drafts, StepJournal staging, command stores, Runtime Cell objects, Mission source, Catalog, Compiler, source documents, and cancellation state are excluded. In particular, a checkpoint contains no pending, accepted, or sampled cancellation request. The checkpoint retains the materialization provider only as a process-local lifetime witness for typed snapshot destruction; it retains no Runtime Cell address.
5. Capture clones and validates the complete snapshot in private storage. Publication occurs only after store shape, Image identity, layout, codec, C++ type identity, and object invariants all pass. Failure returns a structured `CheckpointOutcome` and leaves source state, epoch, tick, history, seal, RunOutcome, and active run unchanged.
6. `Session::restore()` accepts a new nonempty child RunId, exact RunBinding, and immutable checkpoint on a new `Created` Session. Reusing the parent RunId is rejected. The target must share the exact in-memory Image and must expose materializer operations matching every checkpoint value's implementation identity.
7. Restore validates the checkpoint before materialization, then constructs preparations, Runtime Cells, frame storage, and initial state through the ordinary Image lifecycle. Committed/candidate replacements, histories, held samples, and the sealed boundary are deep-cloned into private staging. A final no-fail precommit check closes every shape and swap requirement.
8. `RestoreCommit` publishes all restored stores, boundary coordinates, child RunId, exact binding, and lineage in one commit region. The child enters `Initialized` with `run_sequence=0`, the checkpoint epoch/tick/step count, `RunStartKind::RestoreBranch`, and clean cancellation state. Its next `execute_step()` continues from the captured boundary.
9. Restore failure releases all target materialization in reverse order, publishes no active run or partial mutable store, and freezes one minimal `RestoreBranch + run_start_committed=false + Failed + Unknown` RunOutcome. The target Image, attempted checkpoint, structured RestoreOutcome, and diagnostic remain queryable.
10. Images with command routes or event deliveries return the stable unsupported-capability result before checkpoint or restore state changes. Pending-command branch RunId rebinding has no contract in this slice.

## Consequences

- A source Session and any number of restored child Sessions share immutable Image/checkpoint ownership while keeping all mutable state, histories, seals, candidates, frames, preparations, and Runtime Cells independent.
- Cancellation requests remain bound to the exact RunId that accepted them. Cancelling a parent after capture cannot change the checkpoint or any restored child; a child cancels only after accepting a request bound to its own child RunId.
- Exact Image object identity and process-local C++ type identity deliberately prevent cross-process restore and compatibility migration.
- Restore recreates Runtime Cells from the Image. Continuation therefore depends only on committed checkpoint stores for the current REF-YYZ implementation.
- Checkpoint lifetime can exceed source Session and adapter facade lifetime because the checkpoint owns its typed values and retains their destruction implementation.

## Executable evidence

- `r3.kernel-step-transaction.probe` captures REF-YYZ after the first Continue at `(epoch=1,tick=1)`, completes the parent suffix, destroys the source Session, restores the same checkpoint twice with distinct child RunIds, and completes both child suffixes.
- The probe compares rigid/mass state, typed tick 0/1/2 seals, concrete evaluator-history samples, terminal decision, mission result, and completion facts bit for bit, excluding declared run and lineage identity differences. Both children continue to the existing mission oracle and advancing one child leaves the other unchanged.
- Direct negatives cover state/history/seal capture failures, open frame/transaction barriers, wrong Image and binding, empty/duplicate child RunId, layout/codec/type/invariant corruption, final RestoreCommit precheck failure, and absence of partial target stores.
- `r3.kernel-session-materialization.probe` injects allocation failure into checkpoint and restore, verifies structured diagnostics and zero post-failure allocation, and proves reverse cleanup. `r3.kernel-command-event.probe` proves both operations fail closed for the ModeOwner command/event Image without changing either Session.
- `r3.kernel-multirate-held-output.probe` captures a committed tick 0 guidance sample, completes and destroys the parent, restores two independent children, and reproduces the held tick 1 and fresh tick 2 suffix. It rejects held layout, codec, type, quality, authority, missing-store, extra-store, clone, and final-precommit faults without partial publication. A separate path captures the checkpoint, cancels the parent, restores a clean child that completes, rejects the parent RunId on that child, and accepts only a new request bound to the child RunId.

## Deferred scope

- command/event checkpoint participation and pending-command branch identity;
- active-run truncating reset and pause/resume;
- complete RunResource open/finalize hooks;
- durable checkpoint serialization, file or Artifact representation, manager/registry, cross-process transport, and compatibility migration;
- R4 evidence publication, Workflow, and frontend integration.

## Supersession rule

Any command-bearing checkpoint, durable representation, process boundary, compatibility policy, Runtime Cell state participation, or additional restore lifecycle requires a successor decision backed by an executable consumer.
