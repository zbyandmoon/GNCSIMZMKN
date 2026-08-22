#!/usr/bin/env python3
"""Compare the R1 typed rigid/mass product with the accepted R0 oracle."""

from __future__ import annotations

import argparse
from decimal import Decimal, getcontext
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys


FIXTURE_ID = "REF-YYZ-TWO-INTERVAL-MASS-COMMIT-001"
ORACLE_ID = "ORACLE-YYZ-TWO-INTERVAL-MASS-COMMIT-001"
REFERENCE_MODEL_ID = "MODEL-YYZ-TWO-INTERVAL-MASS-COMMIT-001"
REFERENCE_MASS_MODEL_ID = \
    "MODEL-YYZ-SCALAR-BURN-CONSTANT-GEOMETRY-001"
PRODUCT_MODEL_ID = \
    "gnc.package.yyz.two-interval-mass-commit.experimental@1"
PRODUCT_MASS_MODEL_ID = \
    "gnc.package.yyz.mass.scalar-burn-constant-geometry.experimental@1"
CONTRACT_ID = \
    "gnc.package.yyz.rigid-mass-boundary.contract.experimental@1"
PROPULSION_FIXTURE_ID = "REF-YYZ-PROPULSION-RESPONSE-001"
PROPULSION_ORACLE_ID = "ORACLE-YYZ-PROPULSION-RESPONSE-001"
PROPULSION_REFERENCE_MODEL_ID = "MODEL-YYZ-PROPULSION-RESPONSE-001"
PROPULSION_PRODUCT_MODEL_ID = \
    "gnc.package.yyz.propulsion-response.supplied.experimental@1"
PROPULSION_CONTRACT_ID = \
    "gnc.package.yyz.propulsion-response.contract.experimental@1"
MISSION_FIXTURE_ID = "REF-YYZ-MISSION-COMPOSITION-001"
MISSION_ORACLE_ID = "ORACLE-YYZ-MISSION-COMPOSITION-001"
MISSION_REFERENCE_MODEL_ID = \
    "MODEL-YYZ-FIXTURE-MISSION-COMPOSITION-004"
MISSION_PRODUCT_MODEL_ID = \
    "gnc.package.yyz.controlled-propelled-rigid-mass-step.experimental@1"
MISSION_TWO_INTERVAL_PRODUCT_MODEL_ID = \
    "gnc.package.yyz.two-interval-controlled-propelled-commit.experimental@1"
MISSION_RESULT_PRODUCT_MODEL_ID = \
    "gnc.package.yyz.committed-mission-result.experimental@1"
MISSION_CASE_ID = "CASE-YYZ-MISSION-COMPOSITION-BASELINE"
CASE_ID = "CASE-YYZ-TWO-INTERVAL-MASS-COMMIT-TRAJECTORY"
DIRECT_CHECKS = {
    "interval-zero-committed-mass-and-hidden-candidate",
    "first-atomic-boundary",
    "next-interval-consumes-committed-pair",
    "accepted-terminal-oracle-anchors",
    "constant-geometry-preserved",
    "zero-flow-mass-candidate",
    "negative-flow-rejection",
    "mass-invariant-rejection",
    "atomic-discard-on-mass-failure",
    "atomic-discard-on-rigid-failure",
    "contiguous-boundary-rejection",
}
PROPULSION_DIRECT_CHECKS = {
    "propulsion-three-oracle-cases",
    "propulsion-interval-partition-equivalence",
    "propulsion-ten-invalid-input-rejections",
    "propulsion-to-atomic-boundary-single-transport",
}
MISSION_CONTROL_DIRECT_CHECKS = {
    "mission-committed-observation-to-bounded-guidance",
    "mission-controller-to-current-cycle-actuation",
    "mission-control-propulsion-single-transport",
    "mission-controlled-candidate-oracle-anchors",
    "mission-control-three-invalid-input-rejections",
}
MISSION_TWO_INTERVAL_DIRECT_CHECKS = {
    "mission-interval-zero-committed-feedback",
    "mission-first-controlled-atomic-commit",
    "mission-next-interval-recomputes-feedback",
    "mission-two-interval-terminal-oracle-anchors",
    "mission-controlled-interval-gap-rejection",
}
MISSION_RESULT_DIRECT_CHECKS = {
    "mission-result-committed-sample-metrics",
    "mission-result-inclusive-priority-decision",
    "mission-result-terminal-committed-boundary",
    "mission-result-earliest-committed-boundary",
    "mission-result-three-invalid-input-rejections",
}

PROPULSION_RESPONSE_VECTOR_FIELDS = (
    "force_B_N",
    "r_CoM_to_application_B_m",
    "moment_at_application_B_Nm",
)
PROPULSION_CLOSURE_VECTOR_FIELDS = (
    "force_B_N",
    "moment_at_application_B_Nm",
    "lever_arm_moment_B_Nm",
    "moment_about_CoM_B_Nm",
)
PROPULSION_MASS_SCALAR_FIELDS = (
    "interval_duration_s",
    "fuel_consumption_rate_kgps",
    "consumed_fuel_mass_kg",
    "mass_delta_kg",
    "committed_mass_kg",
    "mass_candidate_kg",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def decimal(value: object) -> Decimal:
    result = value if isinstance(value, Decimal) else Decimal(str(value))
    require(result.is_finite(), f"non-finite numeric value: {value}")
    return result


def compare_number(actual: object, expected: object,
                   absolute: Decimal, relative: Decimal,
                   label: str) -> Decimal:
    left = decimal(actual)
    right = decimal(expected)
    difference = abs(left - right)
    limit = absolute + relative * max(Decimal(1), abs(left), abs(right))
    require(difference <= limit, f"{label} differs: {left} vs {right}")
    return difference


def compare_vector(actual: object, expected: object,
                   absolute: Decimal, relative: Decimal,
                   label: str) -> Decimal:
    require(isinstance(actual, list) and isinstance(expected, list) and
            len(actual) == len(expected), f"{label} shape differs")
    return max((compare_number(left, right, absolute, relative,
                               f"{label}[{index}]")
                for index, (left, right) in enumerate(zip(actual, expected))),
               default=Decimal(0))


def orientation_error(actual: object, expected: object) -> Decimal:
    require(isinstance(actual, list) and isinstance(expected, list) and
            len(actual) == 4 and len(expected) == 4,
            "quaternion shape differs")
    left = [decimal(value) for value in actual]
    right = [decimal(value) for value in expected]
    left_norm = sum((value * value for value in left), Decimal(0)).sqrt()
    right_norm = sum((value * value for value in right), Decimal(0)).sqrt()
    require(left_norm > 0 and right_norm > 0,
            "quaternion has zero norm")
    left = [value / left_norm for value in left]
    right = [value / right_norm for value in right]
    if sum((a * b for a, b in zip(left, right)), Decimal(0)) < 0:
        right = [-value for value in right]
    chord = sum(((a - b) * (a - b)
                 for a, b in zip(left, right)), Decimal(0)).sqrt()
    half_chord = min(Decimal(1), chord / Decimal(2))
    return Decimal(str(4.0 * math.asin(float(half_chord))))


def compare_state(actual: dict, expected: dict,
                  absolute: Decimal, relative: Decimal,
                  label: str) -> tuple[Decimal, Decimal]:
    maximum = max(
        compare_vector(actual["position_I_m"], expected["position_I_m"],
                       absolute, relative, f"{label}.position"),
        compare_vector(actual["velocity_I_mps"],
                       expected["velocity_I_mps"], absolute, relative,
                       f"{label}.velocity"),
        compare_vector(actual["omega_BI_B_radps"],
                       expected["omega_BI_B_radps"], absolute, relative,
                       f"{label}.angular_rate"),
    )
    attitude_error = orientation_error(
        actual["q_I_B_wxyz"], expected["q_I_B_wxyz"])
    require(attitude_error <= absolute,
            f"{label}.attitude differs by {attitude_error} rad")
    return maximum, attitude_error


def run_probe(probe: Path) -> tuple[bytes, dict]:
    completed = subprocess.run(
        [str(probe), "--self-check"], check=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return completed.stdout, json.loads(
        completed.stdout.decode("utf-8"), parse_float=Decimal)


def compare_propulsion_case(actual: dict, expected: dict,
                            absolute: Decimal,
                            relative: Decimal) -> Decimal:
    case_id = expected["id"]
    require(actual["id"] == case_id,
            f"propulsion case id differs for {case_id}")
    maximum = Decimal(0)
    for section, exact_fields, vector_fields, scalar_fields in (
        (
            "response",
            ("source_id", "quality", "body_frame_id", "sample_tick",
             "clock_domain", "configuration_revision",
             "valid_from_tick", "valid_until_tick"),
            PROPULSION_RESPONSE_VECTOR_FIELDS,
            ("fuel_consumption_rate_kgps",),
        ),
        (
            "closure_consumer",
            ("source_id", "body_frame_id", "sample_tick",
             "clock_domain", "configuration_revision"),
            PROPULSION_CLOSURE_VECTOR_FIELDS,
            (),
        ),
        (
            "mass_consumer",
            ("mass_state_id", "clock_domain", "configuration_revision",
             "valid_from_tick", "valid_until_tick"),
            (),
            PROPULSION_MASS_SCALAR_FIELDS,
        ),
    ):
        observed = actual[section]
        reference = expected[section]
        if section == "response":
            require(observed["model_id"] == PROPULSION_PRODUCT_MODEL_ID,
                    f"{case_id}.response.model_id differs")
        for field in exact_fields:
            require(observed[field] == reference[field],
                    f"{case_id}.{section}.{field} differs")
        for field in vector_fields:
            maximum = max(
                maximum,
                compare_vector(observed[field], reference[field],
                               absolute, relative,
                               f"{case_id}.{section}.{field}"))
        for field in scalar_fields:
            maximum = max(
                maximum,
                compare_number(observed[field], reference[field],
                               absolute, relative,
                               f"{case_id}.{section}.{field}"))
    return maximum


def compare_propulsion_equivalence(actual: list, expected: list,
                                   absolute: Decimal,
                                   relative: Decimal) -> Decimal:
    require(len(actual) == len(expected) == 1,
            "propulsion equivalence count differs")
    observed = actual[0]
    reference = expected[0]
    require(observed["id"] == reference["id"] and
            observed["status"] == reference["status"],
            "propulsion equivalence identity differs")
    maximum = Decimal(0)
    for field in (
            "force_and_response_max_abs_difference",
            "application_wrench_max_abs_difference",
            "summed_consumed_fuel_mass_kg",
            "consumed_fuel_mass_difference_kg",
            "sequential_final_mass_candidate_kg",
            "final_mass_candidate_difference_kg"):
        maximum = max(
            maximum,
            compare_number(observed[field], reference[field],
                           absolute, relative,
                           f"propulsion.equivalence.{field}"))
    return maximum


def verify(cases_path: Path, oracle_path: Path, probe_path: Path) -> dict:
    getcontext().prec = 80
    raw_cases = cases_path.read_bytes()
    cases = json.loads(raw_cases.decode("utf-8"), parse_float=Decimal)
    oracle = json.loads(oracle_path.read_text(encoding="utf-8"),
                        parse_float=Decimal)
    require(cases["fixture_id"] == oracle["fixture_id"] == FIXTURE_ID,
            "source fixture identity differs")
    require(cases["oracle_id"] == oracle["oracle_id"] == ORACLE_ID,
            "source oracle identity differs")
    require(cases["model"]["model_id"] ==
            oracle["model_id"] == REFERENCE_MODEL_ID,
            "reference model identity differs")
    require(cases["model"]["mass_evolution_model_id"] ==
            REFERENCE_MASS_MODEL_ID,
            "reference mass model identity differs")
    raw_hash = hashlib.sha256(raw_cases).hexdigest()
    require(oracle["input_identity"]["bytes"] == len(raw_cases) and
            oracle["input_identity"]["sha256"] == raw_hash,
            "source input byte identity differs")

    first_stdout, probe = run_probe(probe_path)
    second_stdout, second_probe = run_probe(probe_path)
    require(first_stdout == second_stdout and probe == second_probe,
            "product probe reruns differ")
    require(probe["schema_version"] ==
            "gnczmkn.yyz-two-interval-mass-commit-product-probe/5" and
            probe["product_model_id"] == PRODUCT_MODEL_ID and
            probe["mass_model_id"] == PRODUCT_MASS_MODEL_ID and
            probe["contract_id"] == CONTRACT_ID and
            probe["source_fixture_id"] == FIXTURE_ID and
            probe["source_oracle_id"] == ORACLE_ID and
            probe["reference_model_id"] == REFERENCE_MODEL_ID and
            probe["status"] == "passed",
            "product probe identity differs")
    require(PRODUCT_MODEL_ID != REFERENCE_MODEL_ID and
            PRODUCT_MASS_MODEL_ID != REFERENCE_MASS_MODEL_ID,
            "product and reference model identities must remain distinct")
    require(set(probe["direct_checks"]) == DIRECT_CHECKS and
            len(probe["direct_checks"]) == len(DIRECT_CHECKS),
            "product direct-check coverage differs")

    repo_root = cases_path.resolve().parents[2]
    propulsion_cases_path = (
        repo_root / "fixtures" / "ref-yyz-propulsion-response" /
        "cases.json")
    propulsion_oracle_path = (
        repo_root / "oracles" / "ref-yyz-propulsion-response" /
        "reference.json")
    raw_propulsion_cases = propulsion_cases_path.read_bytes()
    propulsion_cases = json.loads(
        raw_propulsion_cases.decode("utf-8"), parse_float=Decimal)
    propulsion_oracle = json.loads(
        propulsion_oracle_path.read_text(encoding="utf-8"),
        parse_float=Decimal)
    require(propulsion_cases["fixture_id"] ==
            propulsion_oracle["fixture_id"] == PROPULSION_FIXTURE_ID,
            "propulsion fixture identity differs")
    require(propulsion_cases["oracle_id"] ==
            propulsion_oracle["oracle_id"] == PROPULSION_ORACLE_ID,
            "propulsion oracle identity differs")
    require(propulsion_cases["model"]["model_id"] ==
            propulsion_oracle["model_id"] ==
            PROPULSION_REFERENCE_MODEL_ID,
            "propulsion reference model identity differs")
    propulsion_raw_hash = hashlib.sha256(
        raw_propulsion_cases).hexdigest()
    require(propulsion_oracle["input_identity"]["bytes"] ==
            len(raw_propulsion_cases) and
            propulsion_oracle["input_identity"]["sha256"] ==
            propulsion_raw_hash,
            "propulsion source input byte identity differs")

    propulsion = probe["propulsion"]
    require(propulsion["product_model_id"] ==
            PROPULSION_PRODUCT_MODEL_ID and
            propulsion["contract_id"] == PROPULSION_CONTRACT_ID and
            propulsion["source_fixture_id"] == PROPULSION_FIXTURE_ID and
            propulsion["source_oracle_id"] == PROPULSION_ORACLE_ID and
            propulsion["reference_model_id"] ==
            PROPULSION_REFERENCE_MODEL_ID and
            propulsion["status"] == "passed",
            "propulsion product identity differs")
    require(PROPULSION_PRODUCT_MODEL_ID !=
            PROPULSION_REFERENCE_MODEL_ID,
            "propulsion product and reference identities must differ")
    require(set(propulsion["direct_checks"]) ==
            PROPULSION_DIRECT_CHECKS and
            len(propulsion["direct_checks"]) ==
            len(PROPULSION_DIRECT_CHECKS),
            "propulsion direct-check coverage differs")

    propulsion_absolute = decimal(
        propulsion_cases["tolerances"]["formula_absolute"])
    propulsion_relative = decimal(
        propulsion_cases["tolerances"]["formula_relative"])
    observed_propulsion_cases = {
        entry["id"]: entry for entry in propulsion["cases"]
    }
    require(len(observed_propulsion_cases) == len(propulsion["cases"]) ==
            len(propulsion_oracle["cases"]),
            "propulsion product cases are incomplete")
    maximum_propulsion_error = Decimal(0)
    for case_id, reference_case in propulsion_oracle["cases"].items():
        require(case_id in observed_propulsion_cases,
                f"propulsion product case is missing: {case_id}")
        maximum_propulsion_error = max(
            maximum_propulsion_error,
            compare_propulsion_case(
                observed_propulsion_cases[case_id], reference_case,
                propulsion_absolute, propulsion_relative))
    maximum_propulsion_error = max(
        maximum_propulsion_error,
        compare_propulsion_equivalence(
            propulsion["equivalence_results"],
            propulsion_oracle["equivalence_results"],
            propulsion_absolute, propulsion_relative))
    expected_invalid = {
        entry["id"] for entry in
        propulsion_cases["invalid_input_cases"]
    }
    require(set(propulsion["invalid_input_rejections"]) ==
            expected_invalid and
            len(propulsion["invalid_input_rejections"]) ==
            len(expected_invalid),
            "propulsion invalid-input coverage differs")

    off_axis_id = "CASE-YYZ-PROPULSION-OFF-AXIS-CONSUMERS"
    off_axis_reference = propulsion_oracle["cases"][off_axis_id]
    fixture_cases = {
        entry["id"]: entry for entry in propulsion_cases["cases"]
    }
    off_axis_fixture = fixture_cases[off_axis_id]
    consumer = propulsion["atomic_boundary_consumer"]
    maximum_propulsion_error = max(
        maximum_propulsion_error,
        compare_vector(
            consumer["force_B_N"],
            off_axis_reference["closure_consumer"]["force_B_N"],
            propulsion_absolute, propulsion_relative,
            "propulsion.atomic_consumer.force"),
        compare_vector(
            consumer["moment_about_CoM_B_Nm"],
            off_axis_reference["closure_consumer"]
                ["moment_about_CoM_B_Nm"],
            propulsion_absolute, propulsion_relative,
            "propulsion.atomic_consumer.moment"),
    )
    one_tick_consumed = (
        decimal(off_axis_fixture["supplied_response"]
                ["fuel_consumption_rate_kgps"]) *
        decimal(off_axis_fixture["context"]["base_dt_s"]))
    one_tick_candidate = (
        decimal(off_axis_fixture["mass_consumer"]
                ["committed_mass_kg"]) - one_tick_consumed)
    maximum_propulsion_error = max(
        maximum_propulsion_error,
        compare_number(
            consumer["consumed_fuel_mass_kg"], one_tick_consumed,
            propulsion_absolute, propulsion_relative,
            "propulsion.atomic_consumer.consumed_mass"),
        compare_number(
            consumer["mass_candidate_kg"], one_tick_candidate,
            propulsion_absolute, propulsion_relative,
            "propulsion.atomic_consumer.mass_candidate"),
    )
    require(consumer["candidate_tick"] == 1,
            "propulsion atomic consumer candidate tick differs")

    mission_cases_path = (
        repo_root / "fixtures" / "ref-yyz-mission-composition" /
        "cases.json")
    mission_oracle_path = (
        repo_root / "oracles" / "ref-yyz-mission-composition" /
        "reference.json")
    mission_cases = json.loads(
        mission_cases_path.read_text(encoding="utf-8"),
        parse_float=Decimal)
    mission_oracle = json.loads(
        mission_oracle_path.read_text(encoding="utf-8"),
        parse_float=Decimal)
    require(mission_cases["fixture_id"] ==
            mission_oracle["fixture_id"] == MISSION_FIXTURE_ID and
            mission_cases["oracle_id"] ==
            mission_oracle["oracle_id"] == MISSION_ORACLE_ID and
            mission_cases["model"]["model_id"] ==
            mission_oracle["model_id"] == MISSION_REFERENCE_MODEL_ID,
            "mission source identity differs")
    mission_control = probe["mission_control"]
    require(mission_control["product_model_id"] ==
            MISSION_PRODUCT_MODEL_ID and
            mission_control["source_fixture_id"] == MISSION_FIXTURE_ID and
            mission_control["source_oracle_id"] == MISSION_ORACLE_ID and
            mission_control["reference_model_id"] ==
            MISSION_REFERENCE_MODEL_ID and
            mission_control["status"] == "passed" and
            MISSION_PRODUCT_MODEL_ID != MISSION_REFERENCE_MODEL_ID,
            "mission control product identity differs")
    require(set(mission_control["direct_checks"]) ==
            MISSION_CONTROL_DIRECT_CHECKS and
            len(mission_control["direct_checks"]) ==
            len(MISSION_CONTROL_DIRECT_CHECKS),
            "mission control direct-check coverage differs")
    mission_reference = mission_oracle["cases"][MISSION_CASE_ID]
    interval_one = mission_reference["interval_executions"][1]
    control_reference = interval_one["guidance_control"]
    mission_absolute = decimal(
        mission_cases["tolerances"]["formula_absolute"])
    mission_relative = decimal(
        mission_cases["tolerances"]["formula_relative"])
    maximum_mission_error = Decimal(0)
    require(mission_control["observation_tick"] ==
            control_reference["source_observation"]["sample_tick"] == 1,
            "mission committed observation tick differs")
    for section, fields in (
        ("guidance", (
            "altitude_error_m", "altitude_feedback_rad",
            "vertical_speed_feedback_rad", "raw_pitch_command_rad",
            "pitch_command_rad")),
        ("controller", (
            "pitch_error_rad", "proportional_moment_Nm",
            "rate_damping_moment_Nm", "raw_moment_command_Nm",
            "moment_command_Nm")),
    ):
        observed = mission_control[section]
        reference = control_reference[section]
        require(observed["saturated"] == reference["saturated"],
                f"mission {section} saturation differs")
        for field in fields:
            maximum_mission_error = max(
                maximum_mission_error,
                compare_number(observed[field], reference[field],
                               mission_absolute, mission_relative,
                               f"mission.{section}.{field}"))
    actuation = mission_control["actuator"]
    actuation_reference = control_reference["ideal_moment_actuation"]
    require(actuation["valid_from_tick"] ==
            actuation_reference["valid_from_tick"] and
            actuation["valid_until_tick"] ==
            actuation_reference["valid_until_tick"],
            "mission actuation interval differs")
    maximum_mission_error = max(
        maximum_mission_error,
        compare_vector(
            actuation["moment_about_CoM_B_Nm"],
            actuation_reference["moment_contribution_about_CoM_B_Nm"],
            mission_absolute, mission_relative,
            "mission.actuator.moment"))
    atomic = mission_control["atomic_boundary"]
    require(atomic["candidate_tick"] == 2,
            "mission controlled candidate tick differs")
    maximum_mission_error = max(
        maximum_mission_error,
        compare_vector(
            atomic["supplied_force_B_N"], [100, 0, 0],
            mission_absolute, mission_relative,
            "mission.atomic.supplied_force"),
        compare_vector(
            atomic["supplied_moment_about_CoM_B_Nm"],
            actuation_reference["moment_contribution_about_CoM_B_Nm"],
            mission_absolute, mission_relative,
            "mission.atomic.supplied_moment"),
        compare_vector(
            atomic["total_moment_about_CoM_B_Nm"],
            interval_one["closure"]["moment_total_about_CoM_B_Nm"],
            mission_absolute, mission_relative,
            "mission.atomic.total_moment"),
        compare_number(
            atomic["candidate_mass_kg"],
            interval_one["mass_transition"]
                ["pending_mass_candidate_kg"],
            mission_absolute, mission_relative,
            "mission.atomic.candidate_mass"),
    )
    terminal_reference = mission_reference["committed_samples"][2]
    state_error, attitude_error = compare_state(
        atomic["candidate_rigid_state"], terminal_reference,
        mission_absolute, mission_relative,
        "mission.atomic.candidate_rigid_state")
    maximum_mission_error = max(maximum_mission_error, state_error)
    require(attitude_error <= mission_absolute,
            "mission controlled candidate attitude differs")

    mission_two_interval = probe["mission_two_interval"]
    require(mission_two_interval["product_model_id"] ==
            MISSION_TWO_INTERVAL_PRODUCT_MODEL_ID and
            mission_two_interval["source_fixture_id"] ==
            MISSION_FIXTURE_ID and
            mission_two_interval["source_oracle_id"] ==
            MISSION_ORACLE_ID and
            mission_two_interval["reference_model_id"] ==
            MISSION_REFERENCE_MODEL_ID and
            mission_two_interval["status"] == "passed" and
            MISSION_TWO_INTERVAL_PRODUCT_MODEL_ID !=
            MISSION_REFERENCE_MODEL_ID,
            "mission two-interval product identity differs")
    require(set(mission_two_interval["direct_checks"]) ==
            MISSION_TWO_INTERVAL_DIRECT_CHECKS and
            len(mission_two_interval["direct_checks"]) ==
            len(MISSION_TWO_INTERVAL_DIRECT_CHECKS),
            "mission two-interval direct-check coverage differs")
    observed_mission_intervals = mission_two_interval["intervals"]
    reference_mission_intervals = mission_reference["interval_executions"]
    require(len(observed_mission_intervals) ==
            len(reference_mission_intervals) == 2 and
            mission_two_interval["terminal_tick"] == 2,
            "mission two-interval shape differs")
    maximum_two_interval_error = Decimal(0)
    maximum_two_interval_orientation_error = Decimal(0)
    for index, (observed, reference) in enumerate(zip(
            observed_mission_intervals, reference_mission_intervals)):
        label = f"mission_two_interval.intervals[{index}]"
        control = reference["guidance_control"]
        actuation = control["ideal_moment_actuation"]
        transition = reference["mass_transition"]
        closing_sample = mission_reference["committed_samples"][index + 1]
        require(observed["observation_tick"] ==
                control["source_observation"]["sample_tick"] and
                observed["closing_tick"] ==
                closing_sample["sample_tick"],
                f"{label} tick identity differs")
        for observed_field, reference_value in (
            ("pitch_command_rad",
             control["guidance"]["pitch_command_rad"]),
            ("moment_command_Nm",
             control["controller"]["moment_command_Nm"]),
            ("integration_mass_kg",
             transition["opening_committed_mass_kg"]),
            ("consumed_mass_kg", transition["consumed_mass_kg"]),
            ("closing_mass_kg", closing_sample["committed_mass_kg"]),
        ):
            maximum_two_interval_error = max(
                maximum_two_interval_error,
                compare_number(
                    observed[observed_field], reference_value,
                    mission_absolute, mission_relative,
                    f"{label}.{observed_field}"))
        for observed_field, reference_value in (
            ("realized_moment_B_Nm",
             actuation["moment_contribution_about_CoM_B_Nm"]),
            ("supplied_moment_about_CoM_B_Nm",
             actuation["moment_contribution_about_CoM_B_Nm"]),
            ("total_moment_about_CoM_B_Nm",
             reference["closure"]["moment_total_about_CoM_B_Nm"]),
        ):
            maximum_two_interval_error = max(
                maximum_two_interval_error,
                compare_vector(
                    observed[observed_field], reference_value,
                    mission_absolute, mission_relative,
                    f"{label}.{observed_field}"))
        state_error, state_attitude_error = compare_state(
            observed["closing_rigid_state"], closing_sample,
            mission_absolute, mission_relative,
            f"{label}.closing_rigid_state")
        maximum_two_interval_error = max(
            maximum_two_interval_error, state_error)
        maximum_two_interval_orientation_error = max(
            maximum_two_interval_orientation_error,
            state_attitude_error)

    mission_result = probe["mission_result"]
    reference_result = mission_reference["mission_result"]
    require(mission_result["product_model_id"] ==
            MISSION_RESULT_PRODUCT_MODEL_ID and
            mission_result["source_fixture_id"] == MISSION_FIXTURE_ID and
            mission_result["source_oracle_id"] == MISSION_ORACLE_ID and
            mission_result["reference_model_id"] ==
            MISSION_REFERENCE_MODEL_ID and
            MISSION_RESULT_PRODUCT_MODEL_ID != MISSION_REFERENCE_MODEL_ID,
            "mission result product identity differs")
    require(set(mission_result["direct_checks"]) ==
            MISSION_RESULT_DIRECT_CHECKS and
            len(mission_result["direct_checks"]) ==
            len(MISSION_RESULT_DIRECT_CHECKS),
            "mission result direct-check coverage differs")
    require(mission_result["status"] ==
            reference_result["final_status"] and
            mission_result["initial_tick"] ==
            reference_result["initial_tick"] and
            mission_result["final_tick"] ==
            reference_result["final_tick"],
            "mission result status or ticks differ")
    maximum_mission_result_error = compare_number(
        mission_result["final_time_s"], reference_result["final_time_s"],
        mission_absolute, mission_relative,
        "mission_result.final_time_s")
    observed_termination = mission_result["termination"]
    reference_termination = reference_result["termination"]
    require(observed_termination["action"] ==
            reference_termination["action"] and
            observed_termination["reason_code"] ==
            reference_termination["reason_code"] and
            observed_termination["priority"] ==
            reference_termination["priority"],
            "mission result termination identity differs")
    maximum_mission_result_error = max(
        maximum_mission_result_error,
        compare_number(
            observed_termination["trigger_time_s"],
            reference_termination["trigger_time_s"],
            mission_absolute, mission_relative,
            "mission_result.termination.trigger_time_s"))
    observed_metrics = mission_result["metrics"]
    reference_metrics = reference_result["metrics"]
    for field in (
            "duration_s", "downrange_m", "vertical_displacement_m",
            "remaining_mass_kg", "consumed_mass_kg",
            "terminal_speed_mps", "peak_speed_mps",
            "maximum_downrange_m", "minimum_remaining_mass_kg"):
        maximum_mission_result_error = max(
            maximum_mission_result_error,
            compare_number(
                observed_metrics[field], reference_metrics[field],
                mission_absolute, mission_relative,
                f"mission_result.metrics.{field}"))
    for field in (
            "evaluated_sample_count", "peak_speed_tick",
            "maximum_downrange_tick", "minimum_remaining_mass_tick"):
        require(observed_metrics[field] == reference_metrics[field],
                f"mission_result.metrics.{field} differs")
    terminal_sample = mission_result["terminal_sample"]
    terminal_observation = mission_reference["terminal_observation"]
    require(terminal_sample["tick"] ==
            terminal_observation["sample_tick"],
            "mission result terminal sample tick differs")
    maximum_mission_result_error = max(
        maximum_mission_result_error,
        compare_number(
            terminal_sample["committed_mass_kg"],
            terminal_observation["fields"]["committed_mass_kg"],
            mission_absolute, mission_relative,
            "mission_result.terminal_sample.committed_mass_kg"))
    observed_predicates = {
        entry["predicate_id"]: entry
        for entry in mission_result["terminal_predicates"]
    }
    reference_predicates = {
        entry["predicate_id"]: entry
        for entry in mission_reference["evaluation_trace"][-1]
            ["predicate_results"]
    }
    require(set(observed_predicates) == set(reference_predicates),
            "mission result terminal predicate identities differ")
    for predicate_id, observed in observed_predicates.items():
        reference = reference_predicates[predicate_id]
        require(observed["met"] == reference["met"],
                f"mission result predicate {predicate_id} differs")
        maximum_mission_result_error = max(
            maximum_mission_result_error,
            compare_number(
                observed["observed"], reference["observed"],
                mission_absolute, mission_relative,
                f"mission_result.predicates.{predicate_id}.observed"))

    expected = oracle["cases"][CASE_ID]
    actual = probe["accepted"]
    absolute = decimal(cases["tolerances"]["formula_absolute"])
    relative = decimal(cases["tolerances"]["formula_relative"])
    maximum_numeric_error = Decimal(0)
    maximum_orientation_error = Decimal(0)
    require(len(actual["intervals"]) == len(expected["intervals"]) == 2,
            "interval count differs")
    for index, (observed, reference) in enumerate(
            zip(actual["intervals"], expected["intervals"])):
        label = f"intervals[{index}]"
        for field in ("sample_tick", "valid_from_tick", "valid_until_tick",
                      "pending_visibility_before_commit"):
            require(observed[field] == reference[field],
                    f"{label}.{field} differs")
        for field in ("current_committed_mass_kg", "integration_mass_kg",
                      "consumed_mass_kg", "pending_mass_candidate_kg"):
            maximum_numeric_error = max(
                maximum_numeric_error,
                compare_number(observed[field], reference[field],
                               absolute, relative, f"{label}.{field}"))
        maximum_numeric_error = max(
            maximum_numeric_error,
            compare_vector(observed["acceleration_I_mps2"],
                           reference["acceleration_I_mps2"],
                           absolute, relative, f"{label}.acceleration"))
        for state_field in ("initial_rigid_state", "rigid_candidate"):
            state_error, attitude_error = compare_state(
                observed[state_field], reference[state_field],
                absolute, relative, f"{label}.{state_field}")
            maximum_numeric_error = max(maximum_numeric_error, state_error)
            maximum_orientation_error = max(
                maximum_orientation_error, attitude_error)
        observed_commit = observed["closing_commit"]
        reference_commit = reference["closing_commit"]
        require(observed_commit["tick"] == reference_commit["tick"] and
                observed_commit["kind"] == reference_commit["kind"],
                f"{label}.closing_commit identity differs")
        for field in ("mass_kg",):
            maximum_numeric_error = max(
                maximum_numeric_error,
                compare_number(observed_commit[field],
                               reference_commit[field], absolute, relative,
                               f"{label}.closing_commit.{field}"))
        for field in ("r_body_origin_to_CoM_B_m",
                      "inertia_about_CoM_B_kgm2_row_major"):
            maximum_numeric_error = max(
                maximum_numeric_error,
                compare_vector(observed_commit[field],
                               reference_commit[field], absolute, relative,
                               f"{label}.closing_commit.{field}"))
        state_error, attitude_error = compare_state(
            observed_commit["rigid_state"],
            reference_commit["rigid_state"], absolute, relative,
            f"{label}.closing_commit.rigid_state")
        maximum_numeric_error = max(maximum_numeric_error, state_error)
        maximum_orientation_error = max(
            maximum_orientation_error, attitude_error)

    observed_terminal = actual["terminal"]
    reference_terminal = expected["terminal"]
    require(observed_terminal["tick"] == reference_terminal["tick"] and
            observed_terminal["termination_kind"] ==
            reference_terminal["termination_kind"],
            "terminal identity differs")
    for field in ("time_s", "committed_mass_kg"):
        maximum_numeric_error = max(
            maximum_numeric_error,
            compare_number(observed_terminal[field],
                           reference_terminal[field], absolute, relative,
                           f"terminal.{field}"))
    for field in ("r_body_origin_to_CoM_B_m",
                  "inertia_about_CoM_B_kgm2_row_major"):
        maximum_numeric_error = max(
            maximum_numeric_error,
            compare_vector(observed_terminal[field],
                           reference_terminal[field], absolute, relative,
                           f"terminal.{field}"))
    state_error, attitude_error = compare_state(
        observed_terminal["rigid_state"],
        reference_terminal["rigid_state"], absolute, relative,
        "terminal.rigid_state")
    maximum_numeric_error = max(maximum_numeric_error, state_error)
    maximum_orientation_error = max(
        maximum_orientation_error, attitude_error)

    return {
        "source_oracle_id": ORACLE_ID,
        "product_model_id": PRODUCT_MODEL_ID,
        "status": "passed",
        "fixture_sha256": raw_hash,
        "maximum_numeric_error": str(maximum_numeric_error),
        "maximum_orientation_error_rad":
            str(maximum_orientation_error),
        "interval0_integration_mass_kg":
            str(actual["intervals"][0]["integration_mass_kg"]),
        "tick1_committed_mass_kg":
            str(actual["intervals"][0]["closing_commit"]["mass_kg"]),
        "interval1_integration_mass_kg":
            str(actual["intervals"][1]["integration_mass_kg"]),
        "terminal_mass_kg": str(actual["terminal"]["committed_mass_kg"]),
        "terminal_position_I_m": [
            str(value) for value in
            actual["terminal"]["rigid_state"]["position_I_m"]],
        "terminal_velocity_I_mps": [
            str(value) for value in
            actual["terminal"]["rigid_state"]["velocity_I_mps"]],
        "direct_checks_passed": len(probe["direct_checks"]),
        "propulsion_source_oracle_id": PROPULSION_ORACLE_ID,
        "propulsion_product_model_id": PROPULSION_PRODUCT_MODEL_ID,
        "propulsion_maximum_numeric_error":
            str(maximum_propulsion_error),
        "propulsion_cases_passed": len(observed_propulsion_cases),
        "propulsion_invalid_inputs_rejected": len(expected_invalid),
        "propulsion_direct_checks_passed":
            len(propulsion["direct_checks"]),
        "mission_control_source_oracle_id": MISSION_ORACLE_ID,
        "mission_control_product_model_id": MISSION_PRODUCT_MODEL_ID,
        "mission_control_maximum_numeric_error":
            str(maximum_mission_error),
        "mission_control_maximum_orientation_error_rad":
            str(attitude_error),
        "mission_control_direct_checks_passed":
            len(mission_control["direct_checks"]),
        "mission_two_interval_product_model_id":
            MISSION_TWO_INTERVAL_PRODUCT_MODEL_ID,
        "mission_two_interval_maximum_numeric_error":
            str(maximum_two_interval_error),
        "mission_two_interval_maximum_orientation_error_rad":
            str(maximum_two_interval_orientation_error),
        "mission_two_interval_direct_checks_passed":
            len(mission_two_interval["direct_checks"]),
        "mission_result_product_model_id":
            MISSION_RESULT_PRODUCT_MODEL_ID,
        "mission_result_maximum_numeric_error":
            str(maximum_mission_result_error),
        "mission_result_direct_checks_passed":
            len(mission_result["direct_checks"]),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cases", required=True, type=Path)
    parser.add_argument("--oracle", required=True, type=Path)
    parser.add_argument("--probe", required=True, type=Path)
    arguments = parser.parse_args()
    print(json.dumps(
        verify(arguments.cases, arguments.oracle, arguments.probe),
        separators=(",", ":")))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ArithmeticError, KeyError, OSError, TypeError, ValueError,
            json.JSONDecodeError, subprocess.SubprocessError) as error:
        print(
            f"YYZ two-interval product validation failed: {error}",
            file=sys.stderr)
        raise SystemExit(1)
