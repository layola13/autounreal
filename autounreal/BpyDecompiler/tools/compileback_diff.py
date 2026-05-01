from __future__ import annotations

import argparse
import filecmp
import shutil
from dataclasses import dataclass
from pathlib import Path

from Plugins.autounreal.autounreal.BpyDecompiler.api import decompile_blueprint
from Plugins.autounreal.autounreal.BpyDecompiler.compiler import emit_bpy_package_from_human, emit_bpy_package_from_upper


def _ensure_exportbpy_python_path() -> None:
    import sys

    root = Path(__file__).resolve().parents[2] / "ExportBpy" / "Content" / "Python"
    if str(root) not in sys.path:
        sys.path.insert(0, str(root))


@dataclass(slots=True)
class DiffResult:
    ok: bool
    upper_dir: Path
    compiled_dir: Path
    semantic_compiled_dir: Path
    human_dir: Path
    human_compiled_dir: Path
    semantic_ok: bool
    differing: list[str]
    missing: list[str]
    extra: list[str]
    human_differing: list[str]
    human_missing: list[str]
    human_extra: list[str]


def validate_compileback_diff(exported_dir: str | Path, work_dir: str | Path, *, label: str | None = None) -> DiffResult:
    source = Path(exported_dir).resolve()
    work = Path(work_dir).resolve()
    _assert_safe_tmp_output(source, work)
    name = label or source.name
    upper_dir = work / f"{name}_upper"
    compiled_dir = work / f"{name}_compiled_bpy"
    semantic_compiled_dir = work / f"{name}_semantic_compiled_bpy"
    human_dir = work / f"{name}_human"
    human_compiled_dir = work / f"{name}_human_compiled_bpy"
    shutil.rmtree(upper_dir, ignore_errors=True)
    shutil.rmtree(compiled_dir, ignore_errors=True)
    shutil.rmtree(semantic_compiled_dir, ignore_errors=True)
    shutil.rmtree(human_dir, ignore_errors=True)
    shutil.rmtree(human_compiled_dir, ignore_errors=True)
    upper_result = decompile_blueprint(source, upper_dir, human_dir)
    if not upper_result.ok:
        print(f"warning: decompile produced diagnostics for {source}")
    emit_bpy_package_from_upper(upper_dir, compiled_dir)
    emit_bpy_package_from_human(human_dir, human_compiled_dir)
    semantic_ok = _compile_upper_semantic_to_tmp(upper_dir, source, semantic_compiled_dir)
    differing, missing, extra = _diff_non_meta_py(source, compiled_dir)
    human_differing, human_missing, human_extra = _diff_non_meta_py(source, human_compiled_dir)
    ok = (
        not differing
        and not missing
        and not extra
        and not human_differing
        and not human_missing
        and not human_extra
        and semantic_ok
    )
    return DiffResult(
        ok,
        upper_dir,
        compiled_dir,
        semantic_compiled_dir,
        human_dir,
        human_compiled_dir,
        semantic_ok,
        differing,
        missing,
        extra,
        human_differing,
        human_missing,
        human_extra,
    )


def _compile_upper_semantic_to_tmp(upper_dir: Path, source_dir: Path, output_dir: Path) -> bool:
    _assert_safe_tmp_output(source_dir, output_dir)
    try:
        _ensure_exportbpy_python_path()
        from bpy_compile.api import compile_to_bpy_package

        compile_to_bpy_package(str(upper_dir), reference_dir=str(source_dir), output_dir=str(output_dir))
        return output_dir.is_dir() and any(output_dir.rglob("*.bp.py"))
    except Exception as exc:
        print(f"semantic compile warning: {exc}")
        return False


def _assert_safe_tmp_output(source: Path, work: Path) -> None:
    source = source.resolve()
    work = work.resolve()
    if work == source or source in work.parents or work in source.parents:
        raise ValueError(f"Unsafe compile-back work dir; must not overlap source. source={source} work={work}")
    if not any(part.lower() == "tmp" for part in work.parts):
        raise ValueError(f"Compile-back work dir must be under a tmp directory: {work}")


def _is_compared(path: Path) -> bool:
    name = path.name
    return name.endswith(".bp.py") and not name.endswith("_meta.py") and name != "__pycache__"


def _relative_compared_files(root: Path) -> set[str]:
    return {str(path.relative_to(root)).replace("\\", "/") for path in root.rglob("*.py") if _is_compared(path)}


def _diff_non_meta_py(source: Path, compiled: Path) -> tuple[list[str], list[str], list[str]]:
    source_files = _relative_compared_files(source)
    compiled_files = _relative_compared_files(compiled)
    missing = sorted(source_files - compiled_files)
    extra = sorted(compiled_files - source_files)
    common = sorted(source_files & compiled_files)
    differing = [rel for rel in common if not filecmp.cmp(source / rel, compiled / rel, shallow=False)]
    return differing, missing, extra


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Decompile ExportBpy package, compile it back to .bp.py under tmp, and diff non-meta .bp.py files.")
    parser.add_argument("exported_dir")
    parser.add_argument("work_dir")
    parser.add_argument("--label")
    args = parser.parse_args(argv)
    result = validate_compileback_diff(args.exported_dir, args.work_dir, label=args.label)
    print(f"upper_dir={result.upper_dir}")
    print(f"compiled_dir={result.compiled_dir}")
    print(f"semantic_compiled_dir={result.semantic_compiled_dir}")
    print(f"human_dir={result.human_dir}")
    print(f"human_compiled_dir={result.human_compiled_dir}")
    print(f"semantic_ok={result.semantic_ok}")
    print(f"differing={len(result.differing)} missing={len(result.missing)} extra={len(result.extra)}")
    print(
        f"human_differing={len(result.human_differing)} "
        f"human_missing={len(result.human_missing)} human_extra={len(result.human_extra)}"
    )
    for title, values in (
        ("DIFF", result.differing),
        ("MISSING", result.missing),
        ("EXTRA", result.extra),
        ("HUMAN_DIFF", result.human_differing),
        ("HUMAN_MISSING", result.human_missing),
        ("HUMAN_EXTRA", result.human_extra),
    ):
        for value in values[:50]:
            print(f"{title}: {value}")
    return 0 if result.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
