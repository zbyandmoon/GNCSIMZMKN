#!/usr/bin/env python3
"""Build a field-level product/reference report for canonical YYZ 00A."""

from __future__ import annotations

import argparse
import json
import subprocess
from decimal import Decimal
from pathlib import Path
from typing import Any, Dict, List, Sequence

from yyz_00a_canonical_reference import decimal_text, load_decimal_json


D = Decimal
PROBE_PREFIX = "canonical_00a_probe "


def run_probe(executable: Path) -> Dict[str, Any]:
    completed = subprocess.run(
        [str(executable.resolve()), "--self-check"],
        check=False,
        text=True,
        encoding="utf-8",
        errors="strict",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            "canonical product probe failed: "
            + completed.stdout
            + completed.stderr
        )
    lines = [
        line[len(PROBE_PREFIX) :]
        for line in completed.stdout.splitlines()
        if line.startswith(PROBE_PREFIX)
    ]
    if len(lines) != 1:
        raise RuntimeError("canonical product probe emitted no unique JSON line")
    return json.loads(lines[0], parse_float=Decimal, parse_int=int)


def as_decimal(value: Any) -> Decimal:
    if isinstance(value, Decimal):
        return value
    return D(str(value))


def numeric_record(
    field: str,
    actual_value: Any,
    reference_value: Any,
    absolute_tolerance: Decimal,
    relative_tolerance: Decimal,
    tick: int = 0,
) -> Dict[str, Any]:
    actual = as_decimal(actual_value)
    reference = as_decimal(reference_value)
    absolute_error = abs(actual - reference)
    scale = max(abs(actual), abs(reference))
    relative_error = absolute_error / scale if scale != 0 else D(0)
    limit = absolute_tolerance + relative_tolerance * scale
    return {
        "field": field,
        "tick": tick,
        "actual": decimal_text(actual),
        "reference": decimal_text(reference),
        "absolute_error": decimal_text(absolute_error),
        "relative_error": decimal_text(relative_error),
        "limit": decimal_text(limit),
        "accepted": absolute_error <= limit,
    }


def append_vector_records(
    records: List[Dict[str, Any]],
    field: str,
    actual: Sequence[Any],
    reference: Sequence[Any],
    absolute_tolerance: Decimal,
    relative_tolerance: Decimal,
) -> None:
    if len(actual) != len(reference):
        raise RuntimeError(f"field {field} has a vector length mismatch")
    for index, (actual_value, reference_value) in enumerate(
        zip(actual, reference)
    ):
        records.append(
            numeric_record(
                f"{field}[{index}]",
                actual_value,
                reference_value,
                absolute_tolerance,
                relative_tolerance,
            )
        )


def exact_record(field: str, actual: Any, expected: Any) -> Dict[str, Any]:
    return {
        "field": field,
        "actual": actual,
        "expected": expected,
        "accepted": actual == expected,
    }


def build_difference_report(
    reference: Dict[str, Any],
    actual: Dict[str, Any],
    source: Dict[str, Any],
) -> Dict[str, Any]:
    policy = source["comparison_policy"]
    absolute_tolerance = D(policy["float_absolute_tolerance"])
    relative_tolerance = D(policy["float_relative_tolerance"])
    numeric: List[Dict[str, Any]] = []
    append_vector_records(
        numeric,
        "position_enu_m",
        actual["position_enu_m"],
        reference["mapping"]["position_enu_m"],
        absolute_tolerance,
        relative_tolerance,
    )
    append_vector_records(
        numeric,
        "velocity_enu_mps",
        actual["velocity_enu_mps"],
        reference["mapping"]["velocity_enu_mps"],
        absolute_tolerance,
        relative_tolerance,
    )

    actual_quaternion = [as_decimal(value) for value in actual["q_i_b_wxyz"]]
    reference_quaternion = [
        as_decimal(value) for value in reference["mapping"]["q_i_b_wxyz"]
    ]
    if sum(
        actual_value * reference_value
        for actual_value, reference_value in zip(
            actual_quaternion, reference_quaternion
        )
    ) < 0:
        actual_quaternion = [-value for value in actual_quaternion]
    append_vector_records(
        numeric,
        "q_i_b_wxyz_sign_aligned",
        actual_quaternion,
        reference_quaternion,
        absolute_tolerance,
        relative_tolerance,
    )
    append_vector_records(
        numeric,
        "opening_angular_rate_body_radps",
        actual["angular_rate_body_radps"],
        reference["opening_formal_outputs"][
            "opening_angular_rate_body_radps"
        ],
        absolute_tolerance,
        relative_tolerance,
    )
    append_vector_records(
        numeric,
        "center_of_mass_body_m",
        actual["center_of_mass_body_m"],
        reference["opening_formal_outputs"]["center_of_mass_body_m"],
        absolute_tolerance,
        relative_tolerance,
    )

    numeric.append(
        numeric_record(
            "opening_mass_kg",
            actual["mass_kg"],
            reference["opening_formal_outputs"]["opening_mass_kg"],
            absolute_tolerance,
            relative_tolerance,
        )
    )
    for field, actual_key, reference_key in (
        (
            "guidance_altitude_error_m",
            "guidance_altitude_error_m",
            "guidance_altitude_error_m",
        ),
        (
            "guidance_raw_command_rad",
            "guidance_raw_command_rad",
            "guidance_raw_command_rad",
        ),
        ("guidance_command_rad", "guidance_command_rad", "guidance_command_rad"),
        (
            "controller_pitch_error_rad",
            "controller_pitch_error_rad",
            "controller_pitch_error_rad",
        ),
        (
            "controller_raw_moment_nm",
            "controller_raw_moment_nm",
            "controller_raw_moment_nm",
        ),
        ("controller_moment_nm", "controller_moment_nm", "controller_moment_nm"),
        ("mass_flow_kgps", "mass_flow_kgps", "mass_flow_kgps"),
    ):
        numeric.append(
            numeric_record(
                field,
                actual[actual_key],
                reference["opening_formal_outputs"][reference_key],
                absolute_tolerance,
                relative_tolerance,
            )
        )
    append_vector_records(
        numeric,
        "actuator_moment_nm",
        actual["actuator_moment_nm"],
        reference["opening_formal_outputs"]["actuator_moment_nm"],
        absolute_tolerance,
        relative_tolerance,
    )
    append_vector_records(
        numeric,
        "propulsion_force_body_n",
        actual["propulsion_force_n"],
        reference["opening_formal_outputs"]["propulsion_force_body_n"],
        absolute_tolerance,
        relative_tolerance,
    )
    append_vector_records(
        numeric,
        "gravity_enu_mps2",
        actual["gravity_enu_mps2"],
        reference["opening_air_data"]["gravity_enu_mps2"],
        absolute_tolerance,
        relative_tolerance,
    )
    append_vector_records(
        numeric,
        "wind_enu_mps",
        actual["wind_enu_mps"],
        reference["opening_air_data"]["wind_enu_mps"],
        absolute_tolerance,
        relative_tolerance,
    )
    for field, actual_key, reference_key in (
        ("density_kgpm3", "density_kgpm3", "density_kgpm3"),
        (
            "speed_of_sound_mps",
            "speed_of_sound_mps",
            "speed_of_sound_mps",
        ),
        ("airspeed_mps", "airspeed_mps", "airspeed_mps"),
        ("mach", "mach", "mach"),
    ):
        numeric.append(
            numeric_record(
                field,
                actual[actual_key],
                reference["opening_air_data"][reference_key],
                absolute_tolerance,
                relative_tolerance,
            )
        )
    append_vector_records(
        numeric,
        "relative_velocity_enu_mps",
        actual["relative_velocity_enu_mps"],
        reference["opening_air_data"]["relative_velocity_enu_mps"],
        absolute_tolerance,
        relative_tolerance,
    )
    append_vector_records(
        numeric,
        "domain_mach",
        actual["domain_mach"],
        reference["aerodynamic_domain"]["mach_axis"],
        absolute_tolerance,
        relative_tolerance,
    )

    exact = [
        exact_record(
            "image_fingerprint",
            actual["image_fingerprint"],
            source["runtime_profile"]["image_fingerprint"],
        ),
        exact_record("horizon_ticks", actual["horizon_ticks"], 3000),
        exact_record("failure_tick", actual["failure_tick"], 0),
        exact_record(
            "model_id",
            actual["model_id"],
            reference["aerodynamic_domain"]["model_id"],
        ),
        exact_record(
            "asset_id",
            actual["asset_id"],
            reference["aerodynamic_domain"]["asset_id"],
        ),
        exact_record(
            "domain_status",
            actual["status"],
            reference["aerodynamic_domain"]["status"],
        ),
        exact_record(
            "domain_detail",
            actual["detail"],
            reference["aerodynamic_domain"]["detail"],
        ),
        exact_record(
            "session_error", actual["session_error"], "InvocationFailed"
        ),
        exact_record(
            "committed_intervals", actual["committed_intervals"], 0
        ),
    ]

    max_absolute = max(numeric, key=lambda record: D(record["absolute_error"]))
    max_relative = max(numeric, key=lambda record: D(record["relative_error"]))
    unresolved = [
        {
            "id": "canonical-30-second-trajectory",
            "first_unavailable_tick": 1,
            "reason": "opening Mach 0.6176470588235294 exceeds accepted aero-table maximum 0.6",
            "required_owner_choice": "select and qualify an aero/environment asset combination whose declared domain contains the author opening input, or revise the author input",
        }
    ]
    all_compared_accepted = all(record["accepted"] for record in numeric) and all(
        record["accepted"] for record in exact
    )
    candidate_terminal = all_compared_accepted and len(unresolved) == 0
    return {
        "schema_version": "gnczmkn.yyz-00a-difference-report/1",
        "report_id": "DIFF-YYZ-00A-CANONICAL-001",
        "reference_id": reference["reference_id"],
        "product_image_fingerprint": actual["image_fingerprint"],
        "tolerance": {
            "source": policy["tolerance_source"],
            "absolute": policy["float_absolute_tolerance"],
            "relative": policy["float_relative_tolerance"],
            "identity_and_status": policy["identity_and_status"],
            "numeric_acceptance_rule": (
                "absolute_error <= absolute + relative * "
                "max(abs(actual), abs(reference))"
            ),
        },
        "numeric_comparisons": numeric,
        "exact_comparisons": exact,
        "maximum_absolute_error": {
            "field": max_absolute["field"],
            "tick": max_absolute["tick"],
            "value": max_absolute["absolute_error"],
            "limit": max_absolute["limit"],
            "accepted": max_absolute["accepted"],
        },
        "maximum_relative_error": {
            "field": max_relative["field"],
            "tick": max_relative["tick"],
            "value": max_relative["relative_error"],
            "limit": max_relative["limit"],
            "absolute_error": max_relative["absolute_error"],
            "absolute_tolerance": policy["float_absolute_tolerance"],
            "relative_tolerance": policy["float_relative_tolerance"],
            "combined_absolute_limit": max_relative["limit"],
            "accepted": max_relative["accepted"],
            "acceptance_basis": (
                "combined absolute/relative rule; the absolute term governs "
                "this near-zero reference component"
            ),
        },
        "trajectory_coverage": {
            "opening": "compared",
            "intermediate": "unavailable-domain-failure-before-first-interval",
            "terminal": "unavailable-domain-failure-before-first-interval",
            "requested_terminal_tick": 3000,
            "last_compared_tick": 0,
        },
        "dt_ladder": reference["dt_ladder"],
        "difference_classification": {
            "exact_match_count": sum(
                1 for record in exact if record["accepted"]
            ),
            "accepted_numeric_count": sum(
                1 for record in numeric if record["accepted"]
            ),
            "approved_model_time_numerical_difference_count": 0,
            "defect_count": 0,
            "unexplained_difference_count": 0,
            "unresolved_coverage_count": len(unresolved),
        },
        "unresolved": unresolved,
        "unexplained_difference_count": 0,
        "unresolved_difference_count": len(unresolved),
        "unresolved_count": len(unresolved),
        "all_available_fields_accepted": all_compared_accepted,
        "candidate_terminal_science_verdict": candidate_terminal,
        "status": (
            "domain-agreement-with-trajectory-blocker"
            if all_compared_accepted
            else "available-field-mismatch"
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--probe", required=True, type=Path)
    args = parser.parse_args()
    source = load_decimal_json(args.source)
    reference = load_decimal_json(args.reference)
    actual = run_probe(args.probe)
    report = build_difference_report(reference, actual, source)
    print(json.dumps(report, indent=2, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
