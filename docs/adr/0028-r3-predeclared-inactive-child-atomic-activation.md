# ADR-0028: R3 predeclared inactive-child atomic activation

- Status: Proposed
- Date: 2026-08-24
- Owner: Validation lead; repository-owner acceptance pending
- Related task: R3-FIX-001
- Architecture references: 04 typed ports and entity selectors; 06 §7, §14 and §19; 13; 14 §5–§10 and §19; 15 §17–§18
- Extends: ADR-0020 in-process command cutoff, receipt and event commit; ADR-0027 static cross-entity selector and atomic scope union

## Context

The final R3-FIX pressure fixture needs a child entity that is already fully described by Source, Plan, Proof and Image but does not execute until an owner-authorized activation commits. Creating the child, its state, its schedule, or its bindings during a Session would bypass the Plan Firewall. Letting one reducer mutate relationship, parent and child state would also violate the single-owner rule.

The current qualification scope has one active parent, one inactive child, one relationship owner and one exact two-hop mapping chain. It does not justify a general dynamic topology manager or event bus.

## Decision

1. The complete static source may declare an entity as `InactiveAtInitialize`. Its identity and lifecycle sources, occurrences, state blocks, resources, schedules and bindings are compiled before Session creation. The narrow legacy compiler continues to reject this extension.
2. Compiler lowering emits explicit entity and known-activation plan elements, proof coverage and numeric Image revision-5 entries. The activation freezes one parent, one child, three owner occurrences, one transaction, one command route, two ordered delivery handles, three candidate slots, all gated callsites and outputs, and a topology revision delta of one.
3. Session initialization materializes every child Runtime Cell, state object, slot and resource. The initial activity bit is false and topology revision is zero. Runtime execution only applies the compiled numeric activity predicate; it performs no package or entity-name lookup and creates no topology.
4. A typed command first reaches the relationship `DecisionAuthority` reducer. One `LaterPhaseSameTick` mapping updates the parent owner and one `OrderedSamePhaseSameTick` mapping updates the child owner. Each adapter reads and replaces only its own committed/candidate state and carries the mapped value through the exact process-local typed event chain.
5. Precommit requires exactly the relationship, parent and child `InstantPatch` candidates plus the expected receipt and two events. One no-fail publication commits all three owner blocks, flips the child activity bit and increments the topology revision. The child remains gated during the activation boundary and begins its frozen cadence at the next Publish Region.
6. Reducer, either mapping consumer and ObservationSeal invocation failures discard all staging, retain the due command and permit one exact retry. Candidate validation, activation precommit and downstream child evaluation failures are fatal `Invalid` failures. Neither class may leak partial state, activity, topology, output, receipt or event evidence.
7. Successful reset restores every Image-declared initial state, sets the child inactive, resets topology revision to zero and clears control evidence without rematerializing the topology. Activation-bearing checkpoint and restore remain unsupported and fail closed without mutation.
8. Entity and activation facts use conditional source, descriptor, proof and Image-fingerprint domains. Images without this extension keep their accepted revision and fingerprints.

## Consequences

- Activation is a committed relationship transition over pre-existing runtime objects, not runtime topology construction.
- The three mutable blocks retain distinct owners while their candidates and the activity/topology metadata become visible at one StepTransaction boundary.
- A child cannot publish or satisfy a selector while inactive; activation-tick output is also absent by construction.
- The public capability remains limited to one predeclared inactive child and an exact two-hop mapping chain. General N-hop routing, deactivation and topology mutation are not implied.
- `R3-FIX-001` has complete technical fixture evidence and remains `review` for repository-owner acceptance.

## Verification

- `r3.kernel-inactive-child-activation.probe` runs Package/Source through Compiler, Plan/Proof, Image revision 5, Session and StepTransaction.
- Compile/link and coherently re-fingerprinted Image negatives cover lifecycle, identity, owner/candidate membership, delivery order/predecessor, gates and topology delta.
- Runtime checks prove full pre-materialization, no opening or activation-tick child output, atomic three-owner activation, next-Publish execution, typed downstream influence and stable topology revision.
- Reducer, parent mapper, child mapper and seal failures retry exactly once; activation precommit, invalid child candidate and downstream consumer failures roll back and freeze the correct fatal outcome.
- Reset, unsupported checkpoint/restore, shared-provider isolation and deterministic independent runs preserve the declared boundary.

## Alternatives considered

- Runtime entity creation or recompilation would establish a second topology authority outside the Image.
- One reducer writing all three state blocks would violate owner isolation.
- Running the child on the activation boundary would make schedule visibility depend on command execution order rather than the next frozen Publish Region.
- A general event graph or topology service would add unsupported capability beyond the single executable consumer.

## Supersession rule

Deactivation, more than one inactive child, more than two mapping hops, runtime creation/destruction, command-bearing checkpoint participation or cross-process activation values require a successor decision and executable evidence.
