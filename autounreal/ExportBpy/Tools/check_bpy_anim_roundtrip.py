#!/usr/bin/env python3
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PYTHON_DIR = ROOT / "Content" / "Python"
if str(PYTHON_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_DIR))

from bpy_anim_roundtrip_check import validate_export_dir


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate BPY AnimBlueprint roundtrip output for TPOSE-risk Chooser state")
    parser.add_argument("export_dir", type=Path, help="Directory containing __bp__.bp.py and exported *_meta.py files")
    parser.add_argument("--target", default="", help="Expected target Blueprint asset name, e.g. SandboxCharacter_Mover_ABP_error31")
    args = parser.parse_args()

    errors, warnings = validate_export_dir(args.export_dir, args.target)
    for warning in warnings:
        print(f"WARN: {warning}")
    if errors:
        print("FAIL: BPY roundtrip validation failed")
        for error in errors:
            print(f"  - {error}")
        return 1
    print("PASS: BPY roundtrip validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
