#!/usr/bin/env python3
"""Offline generator for the frozen independent 00A reference.

This command is intentionally absent from daily CTest. It reads fixture
inputs and writes an independent reference; it never launches a product
probe or treats production output as expected data.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from yyz_00a_canonical_reference import build_reference_document


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--repository-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    document = build_reference_document(
        args.source.resolve(), args.repository_root.resolve()
    )
    args.output.write_text(
        json.dumps(
            document, indent=2, ensure_ascii=False, sort_keys=True
        )
        + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
