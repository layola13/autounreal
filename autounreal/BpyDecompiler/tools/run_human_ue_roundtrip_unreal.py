from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def _project_root() -> Path:
    return Path(__file__).resolve().parents[5]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run BpyDecompiler human -> BPY -> UE import -> re-export roundtrip inside Unreal Python.")
    parser.add_argument("--source", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--target-path", required=True)
    parser.add_argument("--source-is-human", action="store_true")
    parser.add_argument("--no-compile-asset", action="store_true")
    args = parser.parse_args(argv)

    root = _project_root()
    if str(root) not in sys.path:
        sys.path.insert(0, str(root))

    from Plugins.autounreal.autounreal.BpyDecompiler.tools.human_ue_roundtrip import validate_human_ue_roundtrip

    result = validate_human_ue_roundtrip(
        args.source,
        args.work,
        label=args.label,
        target_path=args.target_path,
        source_is_human=args.source_is_human,
        compile_asset=not args.no_compile_asset,
    )
    print("BPYDECOMPILER_HUMAN_UE_ROUNDTRIP_REPORT=" + str(result.report_path))
    print(json.dumps(result.report, ensure_ascii=False, indent=2)[:8000])
    return 0 if result.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
