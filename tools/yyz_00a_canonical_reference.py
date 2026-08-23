#!/usr/bin/env python3
"""Independent Decimal reference for the canonical YYZ 00A opening boundary.

This module reads fixture assets only. It imports no product package, Kernel
adapter, C++ output, or existing YYZ expected trajectory. Mapping, cadence,
air data and every opening formal output available before the aerodynamic
query are independently recomputed. The accepted aero domain stops the
canonical profile before a force closure, RK4 candidate or trajectory exists.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from decimal import Decimal, localcontext
from pathlib import Path
from typing import Any, Dict, Iterable, List, Sequence, Tuple


D = Decimal
PRECISION = 80
PI = D(
    "3.14159265358979323846264338327950288419716939937510582097494459230781640628620899"
)


def load_decimal_json(path: Path) -> Dict[str, Any]:
    return json.loads(
        path.read_text(encoding="utf-8"),
        parse_float=Decimal,
        parse_int=int,
    )


def sha256_path(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def decimal_text(value: Decimal) -> str:
    if value == 0:
        return "0"
    return str(value.normalize())


def vector_text(values: Iterable[Decimal]) -> List[str]:
    return [decimal_text(value) for value in values]


def dot(lhs: Sequence[Decimal], rhs: Sequence[Decimal]) -> Decimal:
    return sum((a * b for a, b in zip(lhs, rhs)), D(0))


def cross(lhs: Sequence[Decimal], rhs: Sequence[Decimal]) -> List[Decimal]:
    return [
        lhs[1] * rhs[2] - lhs[2] * rhs[1],
        lhs[2] * rhs[0] - lhs[0] * rhs[2],
        lhs[0] * rhs[1] - lhs[1] * rhs[0],
    ]


def norm(values: Sequence[Decimal]) -> Decimal:
    return sum((value * value for value in values), D(0)).sqrt()


def symmetric_clamp(value: Decimal, limit: Decimal) -> Decimal:
    return min(max(value, -limit), limit)


def sin_cos_radians(angle: Decimal) -> Tuple[Decimal, Decimal]:
    """Taylor sin/cos with Decimal-only range reduction."""

    with localcontext() as context:
        context.prec = PRECISION + 18
        two_pi = D(2) * PI
        x = angle % two_pi
        if x > PI:
            x -= two_pi
        x_squared = x * x
        sine = x
        sine_term = x
        cosine = D(1)
        cosine_term = D(1)
        threshold = D(10) ** (-(PRECISION + 8))
        index = 1
        while True:
            sine_term *= -x_squared / D((2 * index) * (2 * index + 1))
            cosine_term *= -x_squared / D(
                (2 * index - 1) * (2 * index)
            )
            sine += sine_term
            cosine += cosine_term
            if abs(sine_term) < threshold and abs(cosine_term) < threshold:
                break
            index += 1
            if index > 1000:
                raise RuntimeError("Decimal trigonometric series did not converge")
        context.prec = PRECISION
        return +sine, +cosine


def sin_cos_degrees(angle_degrees: Decimal) -> Tuple[Decimal, Decimal]:
    return sin_cos_radians(angle_degrees * PI / D(180))


def transpose(matrix: Sequence[Sequence[Decimal]]) -> List[List[Decimal]]:
    return [list(row) for row in zip(*matrix)]


def active_matrix_to_quaternion_wxyz(
    matrix: Sequence[Sequence[Decimal]],
) -> List[Decimal]:
    """Standard trace/major-diagonal conversion; no output sign is assumed."""

    m00, m01, m02 = matrix[0]
    m10, m11, m12 = matrix[1]
    m20, m21, m22 = matrix[2]
    trace = m00 + m11 + m22
    if trace > 0:
        scale = (trace + D(1)).sqrt() * D(2)
        quaternion = [
            scale / D(4),
            (m21 - m12) / scale,
            (m02 - m20) / scale,
            (m10 - m01) / scale,
        ]
    elif m00 > m11 and m00 > m22:
        scale = (D(1) + m00 - m11 - m22).sqrt() * D(2)
        quaternion = [
            (m21 - m12) / scale,
            scale / D(4),
            (m01 + m10) / scale,
            (m02 + m20) / scale,
        ]
    elif m11 > m22:
        scale = (D(1) + m11 - m00 - m22).sqrt() * D(2)
        quaternion = [
            (m02 - m20) / scale,
            (m01 + m10) / scale,
            scale / D(4),
            (m12 + m21) / scale,
        ]
    else:
        scale = (D(1) + m22 - m00 - m11).sqrt() * D(2)
        quaternion = [
            (m10 - m01) / scale,
            (m02 + m20) / scale,
            (m12 + m21) / scale,
            scale / D(4),
        ]
    length = norm(quaternion)
    if length == 0:
        raise RuntimeError("attitude basis produced a zero quaternion")
    return [value / length for value in quaternion]


def passive_matrix_from_quaternion_wxyz(
    quaternion: Sequence[Decimal],
) -> List[List[Decimal]]:
    w, x, y, z = quaternion
    inverse_squared_norm = D(1) / dot(quaternion, quaternion)
    return [
        [
            (w * w + x * x - y * y - z * z) * inverse_squared_norm,
            D(2) * (x * y + w * z) * inverse_squared_norm,
            D(2) * (x * z - w * y) * inverse_squared_norm,
        ],
        [
            D(2) * (x * y - w * z) * inverse_squared_norm,
            (w * w - x * x + y * y - z * z) * inverse_squared_norm,
            D(2) * (y * z + w * x) * inverse_squared_norm,
        ],
        [
            D(2) * (x * z + w * y) * inverse_squared_norm,
            D(2) * (y * z - w * x) * inverse_squared_norm,
            (w * w - x * x - y * y + z * z) * inverse_squared_norm,
        ],
    ]


def matrix_max_error(
    lhs: Sequence[Sequence[Decimal]], rhs: Sequence[Sequence[Decimal]]
) -> Decimal:
    return max(
        abs(lhs[row][column] - rhs[row][column])
        for row in range(3)
        for column in range(3)
    )


def find_selected_asset(
    asset_index: Dict[str, Any], role: str
) -> Dict[str, Any]:
    matches = [
        asset
        for asset in asset_index["selected_assets"]
        if asset["role"] == role
    ]
    if len(matches) != 1:
        raise RuntimeError(f"asset role {role!r} did not resolve exactly once")
    return matches[0]


def verify_asset_locks(
    source: Dict[str, Any], repository_root: Path
) -> Dict[str, Dict[str, Any]]:
    index_lock = source["asset_index_lock"]
    index_path = repository_root / index_lock["path"]
    if sha256_path(index_path) != index_lock["sha256"]:
        raise RuntimeError("canonical asset-index SHA-256 mismatch")
    asset_index = load_decimal_json(index_path)
    if asset_index["asset_index_id"] != index_lock["identity"]:
        raise RuntimeError("canonical asset-index identity mismatch")

    selected: Dict[str, Dict[str, Any]] = {}
    for promoted in source["promoted_assets"]:
        role = promoted["role"]
        if role in selected:
            raise RuntimeError(f"duplicate promoted asset role {role!r}")
        if promoted["revision"] != 1:
            raise RuntimeError(
                f"promoted {role} has an unsupported revision"
            )
        source_path = repository_root / promoted["source_path"]
        if sha256_path(source_path) != promoted["source_sha256"]:
            raise RuntimeError(
                f"promoted {role} source SHA-256 mismatch"
            )
        indexed = find_selected_asset(asset_index, promoted["index_role"])
        if indexed["asset_id"] != promoted["asset_id"]:
            raise RuntimeError(
                f"promoted {role} asset identity mismatch"
            )
        expected_source = (
            promoted["source_path"] + "#" + promoted["source_pointer"]
        )
        if indexed["source_case_path"] != expected_source:
            raise RuntimeError(
                f"promoted {role} source pointer mismatch"
            )
        selected[role] = indexed
    expected_roles = {
        "environment",
        "mass_properties",
        "aerodynamics",
        "propulsion",
        "guidance_control",
        "numerical_policy",
        "termination_observation",
    }
    if set(selected) != expected_roles:
        raise RuntimeError("canonical promoted asset roles are incomplete")
    return selected


def static_due_ticks(interval: int, terminal: int = 10) -> List[int]:
    return [tick for tick in range(terminal + 1) if tick % interval == 0]


def held_source_ticks(provider_interval: int, consumer_ticks: Iterable[int]) -> List[int]:
    return [
        (consumer_tick // provider_interval) * provider_interval
        for consumer_tick in consumer_ticks
    ]


def build_reference_document(
    source_path: Path, repository_root: Path
) -> Dict[str, Any]:
    with localcontext() as context:
        context.prec = PRECISION
        source = load_decimal_json(source_path)
        assets = verify_asset_locks(source, repository_root)
        author = source["author_input"]
        latitude = D(author["latitude_deg"])
        longitude = D(author["longitude_deg"])
        anchor = source["mapping"]["launch_anchor"]
        if latitude != D(anchor["latitude_deg"]) or longitude != D(
            anchor["longitude_deg"]
        ):
            raise RuntimeError("independent mapping rejects a non-anchor input")

        altitude = D(author["altitude_m"])
        datum_altitude = D(anchor["datum_altitude_m"])
        speed = D(author["speed_mps"])
        heading = D(author["heading_deg"])
        gamma = D(author["flight_path_angle_deg"])
        bank = D(author["bank_deg"])
        sin_heading, cos_heading = sin_cos_degrees(heading)
        sin_gamma, cos_gamma = sin_cos_degrees(gamma)
        sin_bank, cos_bank = sin_cos_degrees(bank)

        forward = [
            cos_gamma * sin_heading,
            cos_gamma * cos_heading,
            sin_gamma,
        ]
        zero_bank_right = [cos_heading, -sin_heading, D(0)]
        zero_bank_down = cross(forward, zero_bank_right)
        right = [
            cos_bank * zero_bank_right[index]
            + sin_bank * zero_bank_down[index]
            for index in range(3)
        ]
        down = [
            -sin_bank * zero_bank_right[index]
            + cos_bank * zero_bank_down[index]
            for index in range(3)
        ]
        body_to_enu = [
            [forward[row], right[row], down[row]] for row in range(3)
        ]
        quaternion = active_matrix_to_quaternion_wxyz(
            transpose(body_to_enu)
        )
        recovered = passive_matrix_from_quaternion_wxyz(quaternion)
        basis_residual = matrix_max_error(recovered, body_to_enu)
        negative_quaternion = [-value for value in quaternion]
        double_cover_residual = matrix_max_error(
            passive_matrix_from_quaternion_wxyz(negative_quaternion),
            recovered,
        )

        position = [D(0), D(0), altitude - datum_altitude]
        velocity = [speed * component for component in forward]
        environment = assets["environment"]["payload"]
        wind = [D(value) for value in environment["velocity_airmass_I_mps"]]
        relative_inertial = [
            velocity[index] - wind[index] for index in range(3)
        ]
        relative_body = [
            dot(body_axis, relative_inertial)
            for body_axis in (forward, right, down)
        ]
        airspeed = norm(relative_body)
        speed_of_sound = D(environment["speed_of_sound_mps"])
        mach = airspeed / speed_of_sound
        density = D(environment["density_kgpm3"])
        dynamic_pressure = D("0.5") * density * airspeed * airspeed
        aerodynamic = assets["aerodynamics"]["payload"]
        propulsion = assets["propulsion"]["payload"]
        guidance_control = assets["guidance_control"]["payload"]
        mass_properties = assets["mass_properties"]["payload"]

        measured_pitch = -gamma * PI / D(180)
        measured_pitch_rate = D(0)
        target_altitude = D(author["altitude_command_m"])
        altitude_error = target_altitude - position[2]
        altitude_feedback = (
            D(guidance_control["altitude_error_gain_rad_per_m"])
            * altitude_error
        )
        vertical_speed_feedback = -D(
            guidance_control["vertical_speed_gain_rad_s_per_m"]
        ) * velocity[2]
        raw_pitch_command = altitude_feedback + vertical_speed_feedback
        pitch_command = symmetric_clamp(
            raw_pitch_command,
            D(guidance_control["pitch_command_limit_rad"]),
        )
        pitch_error = pitch_command - measured_pitch
        proportional_moment = D(
            guidance_control["pitch_error_gain_Nm_per_rad"]
        ) * pitch_error
        damping_moment = -D(
            guidance_control["pitch_rate_gain_Nm_s_per_rad"]
        ) * measured_pitch_rate
        raw_moment = proportional_moment + damping_moment
        controller_moment = symmetric_clamp(
            raw_moment,
            D(guidance_control["moment_command_limit_Nm"]),
        )
        actuator_moment = [
            D(0),
            D(guidance_control["realization_gain"])
            * controller_moment,
            D(0),
        ]
        mach_axis = [D(value) for value in aerodynamic["mach_axis"]]
        alpha_axis = [D(value) for value in aerodynamic["alpha_axis_rad"]]
        beta_axis = [D(value) for value in aerodynamic["beta_axis_rad"]]
        # The canonical body components have zero vertical/side velocity in
        # exact arithmetic. Tiny Decimal series residuals remain far inside
        # the angle domain and do not affect the Mach rejection.
        alpha = D(0)
        beta = D(0)
        domain_inside = (
            mach_axis[0] <= mach <= mach_axis[-1]
            and alpha_axis[0] <= alpha <= alpha_axis[-1]
            and beta_axis[0] <= beta <= beta_axis[-1]
        )
        if domain_inside:
            raise RuntimeError(
                "canonical independent query unexpectedly entered aero domain"
            )

        base_rate = int(author["base_rate_hz"])
        rates = author["component_rates_hz"]
        intervals = {
            name: base_rate // int(rate) for name, rate in rates.items()
        }
        if any(
            base_rate % int(rate) != 0 for rate in rates.values()
        ):
            raise RuntimeError("canonical cadence is not integer-divisible")
        duration = D(author["duration_s"])
        terminal_tick_decimal = duration * D(base_rate)
        terminal_tick = int(terminal_tick_decimal)
        if D(terminal_tick) != terminal_tick_decimal:
            raise RuntimeError("duration does not end on the base grid")

        controller_ticks = static_due_ticks(intervals["controller"])
        actuator_ticks = static_due_ticks(intervals["actuator"])
        source_hash = sha256_path(source_path)
        return {
            "schema_version": "gnczmkn.yyz-00a-independent-reference/1",
            "reference_id": "REFERENCE-YYZ-00A-CANONICAL-001",
            "implementation": {
                "identity": "tools.yyz-00a-canonical-decimal-reference@1",
                "decimal_precision_digits": PRECISION,
                "independence": "fixture-only Decimal formulas; no product kernel, adapter, C++ trace, or prior expected trajectory is imported",
                "source_sha256": source_hash,
                "asset_hashes_verified": True,
            },
            "mapping": {
                "inertial_frame_id": source["mapping"]["inertial_frame_id"],
                "body_frame_id": source["mapping"]["body_frame_id"],
                "position_enu_m": vector_text(position),
                "velocity_enu_mps": vector_text(velocity),
                "body_forward_enu": vector_text(forward),
                "body_right_enu": vector_text(right),
                "body_down_enu": vector_text(down),
                "q_i_b_wxyz": vector_text(quaternion),
                "passive_basis_roundtrip_max_abs": decimal_text(
                    basis_residual
                ),
                "double_cover_equivalent": double_cover_residual == 0,
                "double_cover_max_abs": decimal_text(double_cover_residual),
                "nonunit_strategy": {
                    "strict": "DomainError/non-unit-quaternion",
                    "normalize_with_flag": "accepted-with-Normalized-flag",
                },
            },
            "cadence_and_hold": {
                "base_dt_s": decimal_text(D(1) / D(base_rate)),
                "terminal_tick": terminal_tick,
                "interval_ticks": intervals,
                "due_ticks_0_through_10": {
                    name: static_due_ticks(interval)
                    for name, interval in intervals.items()
                },
                "guidance_source_for_controller_ticks_0_through_10": held_source_ticks(
                    intervals["guidance"], controller_ticks
                ),
                "controller_source_for_actuator_ticks_0_through_10": held_source_ticks(
                    intervals["controller"], actuator_ticks
                ),
            },
            "opening_air_data": {
                "tick": 0,
                "gravity_enu_mps2": vector_text(
                    [D(value) for value in environment["gravity_I_mps2"]]
                ),
                "wind_enu_mps": vector_text(wind),
                "density_kgpm3": decimal_text(density),
                "speed_of_sound_mps": decimal_text(speed_of_sound),
                "relative_velocity_enu_mps": vector_text(
                    relative_inertial
                ),
                "relative_velocity_body_mps": vector_text(relative_body),
                "airspeed_mps": decimal_text(airspeed),
                "alpha_rad": decimal_text(alpha),
                "beta_rad": decimal_text(beta),
                "dynamic_pressure_pa": decimal_text(dynamic_pressure),
                "mach": decimal_text(mach),
            },
            "opening_formal_outputs": {
                "navigation_position_enu_m": vector_text(position),
                "navigation_velocity_enu_mps": vector_text(velocity),
                "opening_angular_rate_body_radps": ["0", "0", "0"],
                "guidance_measured_pitch_rad": decimal_text(
                    measured_pitch
                ),
                "guidance_measured_pitch_rate_radps": decimal_text(
                    measured_pitch_rate
                ),
                "guidance_altitude_error_m": decimal_text(altitude_error),
                "guidance_altitude_feedback_rad": decimal_text(
                    altitude_feedback
                ),
                "guidance_vertical_speed_feedback_rad": decimal_text(
                    vertical_speed_feedback
                ),
                "guidance_raw_command_rad": decimal_text(
                    raw_pitch_command
                ),
                "guidance_command_rad": decimal_text(pitch_command),
                "controller_pitch_error_rad": decimal_text(pitch_error),
                "controller_proportional_moment_nm": decimal_text(
                    proportional_moment
                ),
                "controller_damping_moment_nm": decimal_text(
                    damping_moment
                ),
                "controller_raw_moment_nm": decimal_text(raw_moment),
                "controller_moment_nm": decimal_text(controller_moment),
                "actuator_moment_nm": vector_text(actuator_moment),
                "propulsion_force_body_n": vector_text(
                    [
                        D(propulsion["thrust_magnitude_N"])
                        * D(component)
                        for component in propulsion[
                            "thrust_direction_B_unit"
                        ]
                    ]
                ),
                "mass_flow_kgps": decimal_text(
                    D(propulsion["fuel_consumption_rate_kgps"])
                ),
                "opening_mass_kg": decimal_text(D(author["mass_kg"])),
                "center_of_mass_body_m": vector_text(
                    [
                        D(value)
                        for value in mass_properties[
                            "r_body_origin_to_CoM_B_m"
                        ]
                    ]
                ),
                "guidance_control_asset_values": {
                    key: decimal_text(D(value))
                    for key, value in guidance_control.items()
                    if isinstance(value, (Decimal, int))
                },
                "aerodynamic_coefficients": "unavailable-OutOfRange",
                "closure_wrench": "unavailable-OutOfRange",
            },
            "aerodynamic_domain": {
                "model_id": "gnc.package.yyz.aerodynamic-table.multiaffine.experimental@1",
                "asset_id": assets["aerodynamics"]["asset_id"],
                "mach_axis": vector_text(mach_axis),
                "alpha_axis_rad": vector_text(alpha_axis),
                "beta_axis_rad": vector_text(beta_axis),
                "inside": False,
                "status": "OutOfRange",
                "detail": "table-query",
                "failure_tick": 0,
            },
            "downstream_model_reexpression": {
                "propulsion_mass_guidance_control_actuator": "guidance, controller, ideal actuator, propulsion and opening mass outputs are independently evaluated; mass candidate awaits a successful force closure",
                "frozen_interval_rk4": "classical RK4/FrozenInterval retained; no derivative stage is evaluated after the opening failure",
                "metrics_and_terminal": "committed-boundary formulas retained; no terminal committed boundary exists",
            },
            "trajectory": {
                "requested_duration_s": decimal_text(duration),
                "requested_terminal_tick": terminal_tick,
                "opening": "mapped-and-compared",
                "intermediate": "unavailable-domain-failure-before-first-interval",
                "terminal": "unavailable-domain-failure-before-first-interval",
                "committed_intervals": 0,
                "status": "domain-locked-at-opening",
            },
            "dt_ladder": {
                "steps_s": ["0.01", "0.005", "0.0025"],
                "committed_intervals": [0, 0, 0],
                "status": "not-evaluated-domain-failure-before-first-derivative",
            },
            "verdict": {
                "status": "domain-blocked",
                "candidate_terminal_science_verdict": False,
                "claim_bounds": "mapping, source identity, deterministic fail-closed execution, and independent opening-domain agreement only",
            },
        }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--repository-root", required=True, type=Path)
    args = parser.parse_args()
    document = build_reference_document(
        args.source.resolve(), args.repository_root.resolve()
    )
    print(json.dumps(document, indent=2, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
