# ADR-0027: R3 static cross-entity selector and atomic scope union

- Status: Proposed
- Date: 2026-08-23
- Owner: Validation lead; repository-owner acceptance pending
- Related task: R3-FIX-001
- Architecture references: 04 typed ports and scope authority; 06 §7 and §19; 13; 14 §5–§10; 15 §17–§18
- Extends: ADR-0014 canonical graph semantics; ADR-0017 in-process run lifecycle outcomes
- Extended by: ADR-0028 predeclared inactive-child atomic activation

## Context

The two-entity R3 fixture needs one entity's committed formal output to affect a second entity without introducing global truth, state-store access outside the owner, or Kernel knowledge of package and entity identities. Existing scope validation accepts only same-scope edges and unscoped Environment providers. Existing transactions also name exactly one scope, although the Session candidate and no-fail commit machinery can already commit several owners together.

The executable slice needs two narrowly scoped static facts: an explicit read-only selection of a provider entity for a cross-Vehicle sampled edge, and one commit group whose member scopes are frozen before runtime. Neither fact creates topology during a Session.

## Decision

1. A cross-Vehicle binding may carry exactly one package/source-authored entity selector. The selected entity must be the provider occurrence's declared subject and Vehicle scope. This slice accepts only stored `SampledSignal + CurrentCycle` output values with an exactly-one consumer input.
2. Same-scope bindings and the established unscoped Environment-provider exception reject selectors. Missing, duplicated, reversed, unknown, or contract-incompatible selectors fail compilation.
3. Compiler lowering freezes each selector's source reference, binding, provider occurrence/port/slot, consumer occurrence/port, and authorized reader callsites. A dedicated proof record covers that structured authorization. Image revision 4 stores the same facts as numeric handles; Session validates them once and the execution hot path continues to read the ordinary compiled slot.
4. A transaction may declare a sorted set of two or more active Vehicle scopes. Its legacy `scope` remains the canonical first member. Owners, candidate writers, held slots, and observable output sets are derived from the scope union. The Image freezes the complete occurrence membership for that union.
5. Every mutable block retains one occurrence owner. Candidate production uses each owner's existing callsite and writer token. The transaction coordinator only validates and swaps all candidates at the existing no-fail model commit.
6. Selector and multi-scope facts use conditional source, descriptor, proof, and Image-fingerprint encoding. Sources without either extension retain revision 3 and their accepted fingerprints.

## Consequences

- Entity A can influence entity B through an inspectable typed edge while B remains unable to read A's state block or candidate storage.
- A failure after A has produced a candidate can roll back A, a one-tick link owner, and B under one transaction.
- The one-tick relation is represented by a B-owned committed link state: tick `k` stores A's current formal output and publishes it at tick `k+1` with its original source tick.
- Image revision 4 remains an opt-in static extension. It does not add dynamic entity creation, an ECS, a graph database, a package dispatch branch, or a second state authority.
- The inactive-child successor slice is delivered by ADR-0028. `R3-FIX-001` remains `review` for repository-owner acceptance; its technical fixture matrix is complete.

## Alternatives considered

- Placing both entities in one scope would erase the authority boundary under test.
- A global or static truth object would bypass Source, binding, proof, and Session isolation.
- Two independent Sessions would not provide one atomic transaction across both owners.
- Letting one owner store both entities' truth would violate the single-owner requirement.
- Runtime lookup by entity or package name would move a frozen Compiler decision into Kernel execution.

## Verification

- `r3.kernel-two-entity-causal.probe` runs Package/Source through Compiler, Plan/Proof, Image revision 4, Session, and StepTransaction.
- The probe observes A's tick-`k` formal output at B in the same tick and the B-owned link's tick-`k-1` committed payload concurrently.
- It injects a B candidate failure after A and link candidates exist, then proves all three committed blocks, tick, epoch, and committed outputs remain unchanged with a fatal `CandidateProduction` diagnostic.
- Direct negatives cover missing, wrong, duplicate, and same-scope selectors; incomplete transaction scope membership; coherently rehashed Plan mutations; and numeric Image selector/member mutations.
- Shared-provider Sessions remain isolated, and independent runs produce identical scientific state and final evidence apart from caller-owned `RunId`.
- Existing revision-3 complete-plan, Session materialization, transaction, and stuck-actuator probes remain required regressions.

## Supersession rule

Deactivation, multiple inactive children, asynchronous clocks, additional non-current temporal relations, multiple concurrent transactions, runtime topology edits, or cross-process selector representation require another successor decision with an executable consumer.
