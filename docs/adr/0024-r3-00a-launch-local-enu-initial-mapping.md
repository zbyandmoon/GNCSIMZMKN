# ADR-0024: R3 00A launch-local ENU initial mapping

- Status: Accepted
- Date: 2026-08-23
- Owner: Repository owner
- Related task: R3-YYZ-001
- Architecture references: 00A; 03 §8; 15 §2–§4; YYZ reference bundle DEC-003
- Extends: ADR-0006 SI, frame and simulation-time conventions; ADR-0023 00A target-rate source overrides

## Context

The 00A author input supplies geodetic launch coordinates, altitude, speed, heading and flight-path angle, while the current YYZ product consumes Cartesian state in one inertial frame. A canonical R3 source therefore needs one explicit mapping with a stable frame identity and a declared applicability domain. The current consumer only needs the approved 00A launch anchor. A general ellipsoid, Earth-rotation or moving-tangent-plane service would add unused authority and runtime surface.

## Decision

1. The canonical inertial frame is `frame.yyz.00a.launch-local-enu@1`. Its origin is the launch anchor at latitude `31.2304 deg`, longitude `121.4737 deg` and datum altitude `0 m`. Its axes are `+x East`, `+y North`, `+z Up`.
2. The immutable package definition accepts only that exact latitude and longitude. The author input also carries altitude, speed, heading, flight-path angle, bank, mass, altitude command, duration and all component rates. Every value is validated before a profile is returned.
3. Position is `[0, 0, altitude - datum_altitude]`. The canonical opening position is therefore `[0, 0, 1000] m`.
4. Heading is positive clockwise from North and flight-path angle is positive toward Up. With speed `V`, heading `chi` and flight-path angle `gamma`, velocity is

   ```text
   v_E = V cos(gamma) sin(chi)
   v_N = V cos(gamma) cos(chi)
   v_U = V sin(gamma)
   ```

   The canonical opening velocity is `[220, 0, 0] m/s`, up to the bounded trigonometric round-off checked by the direct product and independent Decimal references.
5. The mapping is implemented by the package-owned immutable `Canonical00AInitialMappingDefinition`, author `Canonical00AAuthorInput`, product `Canonical00AProductProfile` and pure `Canonical00AInitialMappingQuery`. The query identity is `gnc.yyz.qualification-00a.launch-local-enu-mapping@1`, version `1.0.0`.
6. The returned profile contains the two frame identities, Cartesian rigid state, opening mass, altitude command, fixed step, terminal tick and all interval ticks. The canonical programmatic MissionSource consumes this output directly.
7. The applicability domain ignores Earth rotation, Earth curvature, ellipsoid conversion and changes to the local tangent plane during the 30-second qualification horizon. Any other latitude or longitude fails closed. A general geodetic conversion remains outside this decision.

## Consequences

- Author geodetic facts participate in executable validation and provenance while Kernel code continues to consume only Image numeric facts.
- The frame has an unambiguous axis order and a distinct identity from the fixture-local REF-YYZ frame.
- The exact-anchor restriction prevents accidental reuse as a general Earth model.
- Changing the launch anchor, datum, axes, heading direction or Earth approximation changes source semantics and requires a successor decision.

## Executable evidence

- `r3.yyz-00a-initial-mapping.probe` checks the canonical author facts, position and velocity formulas, frame identities, duration/rate lowering and fail-closed latitude, longitude and cadence cases.
- `r3.kernel-yyz-00a-canonical.probe` proves that the mapped position, velocity, attitude and `680 kg` mass survive MissionSource → Plan → Proof → Image → Session initialization.
- `tools.yyz-00a-canonical-decimal-reference@1` independently recomputes the launch mapping at 80-digit Decimal precision without importing the package query or C++ run output.

## Alternatives considered

- Reusing the fixture-local inertial frame would merge two mission identities with different author facts and applicability domains.
- Adding a general geodesy framework would create unconsumed ellipsoid, registry and serialization choices.
- Treating latitude and longitude as descriptive metadata would leave the product path unable to reject an unsupported anchor.

## Supersession rule

A different launch anchor, datum, axis order, heading or flight-path convention, Earth rotation/curvature, a changing tangent plane or general geodetic conversion requires a successor ADR and a direct product consumer.
