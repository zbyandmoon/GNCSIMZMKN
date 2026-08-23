#!/usr/bin/env python3
"""Offline product/reference difference-report generator."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from yyz_00a_canonical_reference import load_decimal_json
from yyz_00a_difference import build_difference_report, run_probe


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--reference", required=True, type=Path)
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    report = build_difference_report(
        load_decimal_json(args.reference),
        run_probe(args.probe),
        load_decimal_json(args.source),
    )
    args.output.write_text(
        json.dumps(report, indent=2, ensure_ascii=False, sort_keys=True)
        + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
