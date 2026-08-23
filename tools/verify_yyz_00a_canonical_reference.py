#!/usr/bin/env python3
"""Daily verifier for frozen independent 00A evidence and diff report."""

from __future__ import annotations

import argparse
import copy
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from yyz_00a_canonical_reference import (
    build_reference_document,
    load_decimal_json,
)
from yyz_00a_difference import (
    build_difference_report,
    portable_report_contract,
    run_probe,
)


def verify_portable_projection_guards(report: dict) -> None:
    baseline = portable_report_contract(report)
    mutations = []

    bad_opening = copy.deepcopy(report)
    bad_opening["opening_query"]["actual"]["mach"] = "9"
    mutations.append(("opening tolerance", bad_opening))

    bad_envelope = copy.deepcopy(report)
    bad_envelope["actual_query_envelope"]["mach"][0] = "-1"
    mutations.append(("envelope domain", bad_envelope))

    bad_terminal_mass = copy.deepcopy(report)
    bad_terminal_mass["terminal"]["state"]["mass_kg"] = "999"
    mutations.append(("terminal mass", bad_terminal_mass))

    for label, mutation in mutations:
        if portable_report_contract(mutation) == baseline:
            raise RuntimeError(
                f"portable report projection ignored {label} mutation"
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository-root", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--probe", required=True, type=Path)
    args = parser.parse_args()

    frozen_reference = load_decimal_json(args.reference.resolve())
    recomputed_reference = build_reference_document(
        args.source.resolve(), args.repository_root.resolve()
    )
    if recomputed_reference != frozen_reference:
        raise RuntimeError("frozen independent 00A reference is stale")

    actual = run_probe(args.probe.resolve())
    recomputed_report = build_difference_report(
        frozen_reference,
        actual,
        load_decimal_json(args.source.resolve()),
    )
    verify_portable_projection_guards(recomputed_report)
    frozen_report = load_decimal_json(args.report.resolve())
    if portable_report_contract(recomputed_report) != portable_report_contract(
        frozen_report
    ):
        raise RuntimeError("frozen canonical 00A difference report is stale")
    if not frozen_report["all_available_fields_accepted"]:
        raise RuntimeError("frozen canonical 00A report is not accepted")
    if not recomputed_report["all_available_fields_accepted"]:
        raise RuntimeError("canonical available-field comparison failed")
    if recomputed_report["unresolved_count"] != 0:
        raise RuntimeError("canonical unresolved conformance count changed")
    if not recomputed_report["candidate_terminal_science_verdict"]:
        raise RuntimeError("canonical target-conformance verdict is missing")
    if recomputed_report["verdict"] != (
        "abstract_engineering_target_conformance"
    ):
        raise RuntimeError("canonical verdict phrase changed")
    terminal = recomputed_report["terminal"]
    if (
        terminal["status"] != "Completed"
        or terminal["reason"] != "duration-complete"
        or terminal["tick"] != 3000
        or terminal["committed_intervals"] != 3000
        or not recomputed_report["determinism"]["bit_deterministic"]
    ):
        raise RuntimeError("canonical terminal product evidence changed")

    print(
        "yyz-00a-canonical-reference: PASS "
        f"verdict={recomputed_report['verdict']} "
        f"max_abs={recomputed_report['maximum_absolute_error']['absolute_error']}@"
        f"{recomputed_report['maximum_absolute_error']['field']} "
        f"max_rel={recomputed_report['maximum_relative_error']['relative_error']}@"
        f"{recomputed_report['maximum_relative_error']['field']} "
        f"unresolved={recomputed_report['unresolved_count']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
