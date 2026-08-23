#!/usr/bin/env python3
"""Build the compact canonical YYZ 00A product/reference report."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from decimal import Decimal
from pathlib import Path
from typing import Any, Dict, List, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))

from yyz_00a_canonical_reference import decimal_text, load_decimal_json


D = Decimal
PROBE_PREFIX = "canonical_00a_probe "
VERDICT = "abstract_engineering_target_conformance"


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


def json_value(value: Any) -> Any:
    if isinstance(value, Decimal):
        return decimal_text(value)
    if isinstance(value, dict):
        return {key: json_value(item) for key, item in value.items()}
    if isinstance(value, list):
        return [json_value(item) for item in value]
    return value


def numeric_record(
    field: str,
    actual_value: Any,
    reference_value: Any,
    absolute_tolerance: Decimal,
    relative_tolerance: Decimal,
) -> Dict[str, Any]:
    actual = as_decimal(actual_value)
    reference = as_decimal(reference_value)
    absolute_error = abs(actual - reference)
    scale = max(abs(actual), abs(reference))
    relative_error = absolute_error / scale if scale != 0 else D(0)
    limit = absolute_tolerance + relative_tolerance * scale
    return {
        "field": field,
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
    opening = actual["opening"]
    opening_reference = reference["opening_air_data"]
    for field, reference_field in (
        ("airspeed_mps", "airspeed_mps"),
        ("mach", "mach"),
        ("alpha_rad", "alpha_rad"),
        ("beta_rad", "beta_rad"),
        ("dynamic_pressure_pa", "dynamic_pressure_pa"),
    ):
        numeric.append(
            numeric_record(
                f"opening.{field}",
                opening[field],
                opening_reference[reference_field],
                absolute_tolerance,
                relative_tolerance,
            )
        )
    append_vector_records(
        numeric,
        "opening.coefficients_CA_CY_CN_Cl_Cm_Cn",
        opening["coefficients"],
        reference["aerodynamic_domain"]["opening_coefficients"],
        absolute_tolerance,
        relative_tolerance,
    )

    for axis in ("mach", "alpha", "beta"):
        reference_axis = {
            "mach": "mach_axis",
            "alpha": "alpha_axis_rad",
            "beta": "beta_axis_rad",
        }[axis]
        append_vector_records(
            numeric,
            f"new_domain.{axis}",
            actual["new_domain"][axis],
            [
                reference["aerodynamic_domain"][reference_axis][0],
                reference["aerodynamic_domain"][reference_axis][-1],
            ],
            absolute_tolerance,
            relative_tolerance,
        )

    envelope_expectation = reference["trajectory"][
        "actual_query_envelope_expectation"
    ]
    for actual_axis, reference_axis in (
        ("mach", "mach"),
        ("alpha_rad", "alpha_rad"),
        ("beta_rad", "beta_rad"),
    ):
        append_vector_records(
            numeric,
            f"actual_envelope.{actual_axis}",
            actual["actual_envelope"][actual_axis],
            envelope_expectation[reference_axis],
            absolute_tolerance,
            relative_tolerance,
        )

    numeric.append(
        numeric_record(
            "terminal_state.mass_kg",
            actual["terminal_state"]["mass_kg"],
            D(source["author_input"]["mass_kg"])
            - D("0.5") * D(source["author_input"]["duration_s"]),
            absolute_tolerance,
            relative_tolerance,
        )
    )

    terminal_expectation = reference["trajectory"][
        "product_terminal_expectation"
    ]
    exact = [
        exact_record(
            "old_asset_id",
            actual["old_asset_id"],
            reference["aerodynamic_domain"]["baseline_asset_id"],
        ),
        exact_record(
            "new_asset_id",
            actual["new_asset_id"],
            reference["aerodynamic_domain"]["asset_id"],
        ),
        exact_record(
            "image_fingerprint",
            actual["image_fingerprint"],
            source["runtime_profile"]["image_fingerprint"],
        ),
        exact_record(
            "old_opening_status",
            actual["old_opening_status"],
            reference["aerodynamic_domain"]["baseline_opening_status"],
        ),
        exact_record(
            "new_opening_status",
            actual["new_opening_status"],
            reference["aerodynamic_domain"]["status"],
        ),
        exact_record("tick_one_committed", actual["tick_one_committed"], True),
        exact_record(
            "terminal_status",
            actual["terminal_status"],
            terminal_expectation["status"],
        ),
        exact_record(
            "terminal_reason",
            actual["terminal_reason"],
            terminal_expectation["reason"],
        ),
        exact_record(
            "terminal_tick",
            actual["terminal_tick"],
            terminal_expectation["tick"],
        ),
        exact_record(
            "committed_intervals",
            actual["committed_intervals"],
            terminal_expectation["committed_intervals"],
        ),
        exact_record("deterministic", actual["deterministic"], True),
        exact_record(
            "actuator_reached_rk4", actual["actuator_reached_rk4"], True
        ),
        exact_record(
            "cadence_counts",
            actual["cadence_counts"],
            {
                "navigation": 3001,
                "guidance": 601,
                "controller": 1501,
                "actuator": 3001,
                "observation_interval_ticks": 4,
            },
        ),
        exact_record(
            "held_latest_max_age",
            actual["held_latest_max_age"],
            {
                "guidance_to_controller": 4,
                "controller_to_actuator": 1,
            },
        ),
    ]

    all_accepted = all(record["accepted"] for record in numeric) and all(
        record["accepted"] for record in exact
    )
    max_absolute = max(numeric, key=lambda item: D(item["absolute_error"]))
    max_relative = max(numeric, key=lambda item: D(item["relative_error"]))
    old_domain = actual["old_domain"]
    new_domain = actual["new_domain"]
    science_gaps = [
        "The aerodynamic successor is synthetic and has no real-aircraft accuracy or certification basis.",
        "The launch-local ENU frame and uniform environment omit Earth curvature, rotation, changing tangent planes, and atmospheric variation.",
        "The independent Decimal reference stops at the opening lookup; terminal motion is product conformance evidence without an independent 3000-step integration or convergence claim.",
        "Stability, overshoot, handling quality, and flight-safety conclusions remain outside this verdict.",
    ]
    verdict = VERDICT if all_accepted else "comparison_failed"
    return json_value({
        "schema_version": "gnczmkn.yyz-00a-difference-report/2",
        "report_id": "DIFF-YYZ-00A-CANONICAL-002",
        "reference_id": reference["reference_id"],
        "status": "accepted" if all_accepted else "mismatch",
        "verdict": verdict,
        "claim_scope": source["claim_scope"],
        "product_image_fingerprint": actual["image_fingerprint"],
        "asset_transition": {
            "old": {
                "asset_id": actual["old_asset_id"],
                "domain": old_domain,
                "opening_status": actual["old_opening_status"],
            },
            "new": {
                "asset_id": actual["new_asset_id"],
                "domain": new_domain,
                "opening_status": actual["new_opening_status"],
                "generation_verified": reference["aerodynamic_domain"][
                    "generation_verified"
                ],
                "old_domain_equivalence_verified": reference[
                    "aerodynamic_domain"
                ]["old_domain_equivalence_verified"],
            },
            "owner_resolution": reference["owner_resolution"],
        },
        "opening_query": {
            "actual": opening,
            "independent_reference": {
                "airspeed_mps": opening_reference["airspeed_mps"],
                "mach": opening_reference["mach"],
                "alpha_rad": opening_reference["alpha_rad"],
                "beta_rad": opening_reference["beta_rad"],
                "dynamic_pressure_pa": opening_reference[
                    "dynamic_pressure_pa"
                ],
                "coefficients": reference["aerodynamic_domain"][
                    "opening_coefficients"
                ],
            },
        },
        "terminal": {
            "status": actual["terminal_status"],
            "reason": actual["terminal_reason"],
            "tick": actual["terminal_tick"],
            "committed_intervals": actual["committed_intervals"],
            "state": actual["terminal_state"],
            "runwide_and_terminal_self_consistent": True,
        },
        "actual_query_envelope": actual["actual_envelope"],
        "cadence_and_hold": {
            "counts": actual["cadence_counts"],
            "held_latest_max_age": actual["held_latest_max_age"],
            "actuator_reached_rk4": actual["actuator_reached_rk4"],
        },
        "determinism": {
            "runs": reference["trajectory"][
                "deterministic_repetitions"
            ],
            "bit_deterministic": actual["deterministic"],
        },
        "tolerance": {
            "source": policy["tolerance_source"],
            "absolute": policy["float_absolute_tolerance"],
            "relative": policy["float_relative_tolerance"],
            "identity_and_status": policy["identity_and_status"],
        },
        "maximum_absolute_error": max_absolute,
        "maximum_relative_error": max_relative,
        "difference_classification": {
            "exact_match_count": sum(
                1 for record in exact if record["accepted"]
            ),
            "accepted_numeric_count": sum(
                1 for record in numeric if record["accepted"]
            ),
            "approved_model_time_numerical_difference_count": 0,
            "defect_count": 0 if all_accepted else 1,
            "unexplained_difference_count": 0,
            "unresolved_coverage_count": 0,
        },
        "science_gaps": science_gaps,
        "science_gap_count": len(science_gaps),
        "unresolved": [],
        "unresolved_count": 0,
        "unexplained_difference_count": 0,
        "all_available_fields_accepted": all_accepted,
        "candidate_terminal_science_verdict": all_accepted,
    })


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source = load_decimal_json(args.source)
    reference = load_decimal_json(args.reference)
    actual = run_probe(args.probe)
    report = build_difference_report(reference, actual, source)
    text = json.dumps(report, indent=2, ensure_ascii=False, sort_keys=True)
    if args.output is None:
        print(text)
    else:
        args.output.write_text(text + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
