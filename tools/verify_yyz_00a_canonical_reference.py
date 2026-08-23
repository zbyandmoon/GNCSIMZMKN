#!/usr/bin/env python3
"""Daily verifier for frozen independent 00A expected data and diff report."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from yyz_00a_canonical_reference import (
    build_reference_document,
    load_decimal_json,
)
from yyz_00a_difference import build_difference_report, run_probe


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
    frozen_report = load_decimal_json(args.report.resolve())
    if recomputed_report != frozen_report:
        raise RuntimeError("frozen canonical 00A difference report is stale")
    if not recomputed_report["all_available_fields_accepted"]:
        raise RuntimeError("canonical available-field comparison failed")
    if recomputed_report["unresolved_count"] != 1:
        raise RuntimeError("canonical unresolved trajectory count changed")
    if recomputed_report["candidate_terminal_science_verdict"]:
        raise RuntimeError("terminal verdict was emitted with unresolved work")

    print(
        "yyz-00a-canonical-reference: PASS "
        f"status={recomputed_report['status']} "
        f"max_abs={recomputed_report['maximum_absolute_error']['value']}@"
        f"{recomputed_report['maximum_absolute_error']['field']}/tick0 "
        f"max_rel={recomputed_report['maximum_relative_error']['value']}@"
        f"{recomputed_report['maximum_relative_error']['field']}/tick0 "
        f"unresolved={recomputed_report['unresolved_count']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
