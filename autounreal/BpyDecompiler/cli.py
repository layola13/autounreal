from __future__ import annotations

import argparse
import sys

from .api import decompile_blueprint


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Decompile ExportBpy .bp.py packages to readable Upper Python.")
    parser.add_argument("input_dir", help="Directory containing __bp__.bp.py")
    parser.add_argument("output_dir", nargs="?", help="Output Upper Python package directory")
    args = parser.parse_args(argv)

    result = decompile_blueprint(args.input_dir, args.output_dir)
    print(f"Wrote {result.output_dir}")
    for diag in result.diagnostics:
        print(f"{diag.level}: {diag.graph or ''}: {diag.message}")
    return 0 if result.ok else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
