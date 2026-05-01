from __future__ import annotations

import argparse

from Plugins.autounreal.autounreal.BpyDecompiler.checker import CheckIssue, check_upper_dir


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Check decompiled upper Python for syntax and obvious non-human unreachable statements.")
    parser.add_argument("upper_dir", nargs="+")
    args = parser.parse_args(argv)
    issues: list[CheckIssue] = []
    for upper_dir in args.upper_dir:
        issues.extend(check_upper_dir(upper_dir))
    for issue in issues:
        print(f"{issue.path}:{issue.line}: {issue.message}")
    print(f"checked={len(args.upper_dir)} issue_count={len(issues)}")
    return 1 if issues else 0


if __name__ == "__main__":
    raise SystemExit(main())
