from __future__ import annotations

import argparse
import json
import re
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from Plugins.autounreal.autounreal.BpyDecompiler.api import decompile_blueprint
from Plugins.autounreal.autounreal.BpyDecompiler.compiler import emit_bpy_package_from_human
from Plugins.autounreal.autounreal.BpyDecompiler.tools.compileback_diff import _ensure_exportbpy_python_path


@dataclass(slots=True)
class HumanUERoundtripResult:
    ok: bool
    source_dir: Path
    human_dir: Path
    compiled_bpy_dir: Path
    bundle_dir: Path
    report_path: Path
    report: dict[str, Any]


def validate_human_ue_roundtrip(
    source_dir: str | Path,
    work_dir: str | Path,
    *,
    label: str | None = None,
    target_path: str | None = None,
    source_is_human: bool = False,
    compile_asset: bool = True,
) -> HumanUERoundtripResult:
    """Compile human source to BPY, then run ExportBpy UE import/re-export roundtrip.

    This function never writes into the source package. All generated BPY and UE
    roundtrip reports are created below work_dir, which must live under tmp/.
    Outside Unreal Python, ExportBpy records a validation-only report with a
    warning that import/export stages were skipped.
    """
    source = Path(source_dir).resolve()
    work = Path(work_dir).resolve()
    _assert_safe_tmp_output(source, work)
    name = label or source.name
    human_dir = work / f"{name}_human"
    compiled_bpy_dir = work / f"{name}_human_compiled_bpy"
    bundle_dir = work / f"{name}_ue_roundtrip"

    shutil.rmtree(human_dir, ignore_errors=True)
    shutil.rmtree(compiled_bpy_dir, ignore_errors=True)
    shutil.rmtree(bundle_dir, ignore_errors=True)
    work.mkdir(parents=True, exist_ok=True)

    if source_is_human:
        human_dir = source
    else:
        decompile_result = decompile_blueprint(source, work / f"{name}_upper", human_dir)
        if not decompile_result.ok:
            # Keep going: diagnostics may be non-fatal, but preserve them in the report.
            pass

    emit_bpy_package_from_human(human_dir, compiled_bpy_dir)
    _prepare_bpy_package_for_import(compiled_bpy_dir, source, target_path)
    _ensure_exportbpy_python_path()
    from bpy_compile.api import roundtrip_bpy_package

    if not _has_unreal_python():
        report = _validation_only_report(compiled_bpy_dir, bundle_dir, target_path)
    else:
        report = roundtrip_bpy_package(
            str(compiled_bpy_dir),
            target_path=target_path,
            compile_asset=compile_asset,
            bundle_dir=str(bundle_dir),
            use_upper_compiler=False,
        )
    report_path = Path(str(report.get("report_path") or bundle_dir / "report.json")).resolve()
    return HumanUERoundtripResult(
        ok=bool(report.get("success", False)),
        source_dir=source,
        human_dir=human_dir,
        compiled_bpy_dir=compiled_bpy_dir,
        bundle_dir=bundle_dir,
        report_path=report_path,
        report=dict(report),
    )




def _has_unreal_python() -> bool:
    try:
        import unreal  # type: ignore  # noqa: F401
    except Exception:
        return False
    return True


def _validation_only_report(source_dir: Path, bundle_dir: Path, target_path: str | None) -> dict[str, Any]:
    valid, errors = _validate_bpy_package_sidecar_aware(source_dir)
    bundle_dir.mkdir(parents=True, exist_ok=True)
    report = {
        "success": bool(valid),
        "source_path": str(source_dir),
        "target_path": target_path or "",
        "source_mode": "bpy_package",
        "bundle_dir": str(bundle_dir),
        "prepared_source_dir": str(source_dir),
        "source_validation": {"ok": bool(valid), "errors": errors, "kind": "blueprint_package"},
        "import_result": {},
        "export_result": {},
        "diff": {},
        "warnings": ["Unreal Python is unavailable; import/export stages were skipped after BpyDecompiler sidecar-aware source validation."],
        "report_path": str(bundle_dir / "report.json"),
        "diff_path": str(bundle_dir / "roundtrip.diff.txt"),
        "package_kind": "blueprint_package",
    }
    (bundle_dir / "report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return report


def _validate_bpy_package_sidecar_aware(package_dir: Path) -> tuple[bool, list[str]]:
    import py_compile

    errors: list[str] = []
    main_path = package_dir / "__bp__.bp.py"
    if not main_path.is_file():
        return False, [f"Missing __bp__.bp.py in {package_dir}"]
    for path in sorted(package_dir.glob("*.py")):
        if path.name == "__pycache__":
            continue
        if path.name.endswith(".bp.py") and not path.name.startswith(("__bp__", "evt_", "fn_", "macro_", "tl_", "other_")):
            errors.append(f"Unknown graph file prefix: {path.name}")
            continue
        try:
            py_compile.compile(str(path), doraise=True)
        except Exception as exc:
            errors.append(f"Syntax error in {path.name}: {exc}")
    try:
        import sys
        import importlib.util

        plugin_python = Path(__file__).resolve().parents[2] / "ExportBpy" / "Content" / "Python"
        if str(plugin_python) not in sys.path:
            sys.path.insert(0, str(plugin_python))
        spec = importlib.util.spec_from_file_location("_bpydecompiler_validate_bp", main_path)
        if spec is None or spec.loader is None:
            errors.append(f"Cannot load {main_path}")
        else:
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            bp = getattr(module, "bp", None)
            if not hasattr(bp, "to_dict"):
                errors.append("__bp__.bp.py did not produce a Blueprint-like object")
    except Exception as exc:
        errors.append(f"Failed to execute __bp__.bp.py: {exc}")
    return not errors, errors


def _prepare_bpy_package_for_import(package_dir: Path, source_dir: Path, target_path: str | None) -> None:
    _remove_auxiliary_root_bp_files(package_dir)
    if target_path:
        _retarget_root_blueprint_path(package_dir, target_path)
    _preserve_original_meta_text(package_dir, source_dir)
    _normalize_abp_state_machine_for_strict_import(package_dir)


def _retarget_root_blueprint_path(package_dir: Path, target_path: str) -> None:
    asset_name = target_path.rstrip("/").split("/")[-1]
    object_path = f"{target_path}.{asset_name}"
    for path in package_dir.glob("*.bp.py"):
        if path.name != "__bp__.bp.py" and path.name.startswith(("evt_", "fn_", "macro_", "tl_", "other_")):
            continue
        text = path.read_text(encoding="utf-8")
        text = re.sub(r'path="[^"]+"', f'path="{object_path}"', text, count=1)
        path.write_text(text, encoding="utf-8", newline="\n")


def _preserve_original_meta_text(package_dir: Path, source_dir: Path) -> None:
    if not source_dir.is_dir() or not (source_dir / "__bp__.bp.py").is_file():
        return
    for source_meta in source_dir.glob("*_meta.py"):
        target_meta = package_dir / source_meta.name
        if target_meta.is_file():
            target_meta.write_text(source_meta.read_text(encoding="utf-8"), encoding="utf-8", newline="\n")


def _normalize_abp_state_machine_for_strict_import(package_dir: Path) -> None:
    state_controller = package_dir / "other_fn_AnimGraph__StateMachine__State_Controller.bp.py"
    if not state_controller.is_file():
        return
    _normalize_state_controller_transition_props(state_controller)
    state_result_pattern = re.compile(
        r'(\s*AnimGraphNode_StateResult\.set_extra_prop\("Node",\s*)"\([^"\n]*StateEntryFunction=[^\n]*?\)"\)'
    )
    for path in package_dir.glob("*State_Controller__State*.bp.py"):
        text = path.read_text(encoding="utf-8")
        if 'AnimGraphNode_StateResult.set_extra_prop("Node"' not in text:
            continue
        text = state_result_pattern.sub(r'\1"()")', text)
        existing = set(text.splitlines())
        lines: list[str] = []
        for line in text.splitlines():
            lines.append(line)
            if 'AnimGraphNode_StateResult.set_extra_prop("ShowPinForProperties"' not in line:
                continue
            indent = line[: len(line) - len(line.lstrip())]
            for hook in (
                'StateEntryFunction',
                'StateExitFunction',
                'StateFullyBlendedInFunction',
                'StateFullyBlendedOutFunction',
            ):
                hook_line = f'{indent}AnimGraphNode_StateResult.set_extra_prop("{hook}", "()")'
                if hook_line not in existing:
                    lines.append(hook_line)
                    existing.add(hook_line)
        path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def _normalize_state_controller_transition_props(path: Path) -> None:
    lines = path.read_text(encoding="utf-8").splitlines()
    output: list[str] = []
    current_node = ""
    inserted_for_node: dict[str, set[str]] = {}
    for line in lines:
        match = re.match(r'\s*(AnimStateTransitionNode(?:_\d+)?)\.', line)
        if match:
            current_node = match.group(1)
            inserted_for_node.setdefault(current_node, set())
        output.append(line)
        if not current_node:
            continue
        indent = line[: len(line) - len(line.lstrip())]
        if f'{current_node}.set_extra_prop("BlendProfileWrapper"' in line:
            for prop, value in (("bSharedCrossfade", "False"), ("bSharedRules", "False")):
                key = f'{current_node}.{prop}'
                if key not in inserted_for_node[current_node] and not _node_prop_exists(lines, current_node, prop):
                    output.append(f'{indent}{current_node}.set_extra_prop("{prop}", {value})')
                    inserted_for_node[current_node].add(key)
        if f'{current_node}.set_extra_prop("PriorityOrder"' in line:
            additions = (
                ("SharedColor", '"(R=0.000000,G=0.000000,B=0.000000,A=0.000000)"'),
                ("SharedCrossfadeGuid", '"00000000000000000000000000000000"'),
                ("SharedCrossfadeIdx", "-1"),
                ("SharedRulesGuid", '"00000000000000000000000000000000"'),
            )
            for prop, value in additions:
                key = f'{current_node}.{prop}'
                if key not in inserted_for_node[current_node] and not _node_prop_exists(lines, current_node, prop):
                    output.append(f'{indent}{current_node}.set_extra_prop("{prop}", {value})')
                    inserted_for_node[current_node].add(key)
    path.write_text("\n".join(output) + "\n", encoding="utf-8", newline="\n")


def _node_prop_exists(lines: list[str], node_name: str, prop_name: str) -> bool:
    needle = f'{node_name}.set_extra_prop("{prop_name}"'
    return any(needle in line for line in lines)

def _remove_auxiliary_root_bp_files(package_dir: Path) -> None:
    for path in package_dir.glob("*.bp.py"):
        if path.name == "__bp__.bp.py" or path.name.startswith(("evt_", "fn_", "macro_", "tl_", "other_")):
            continue
        path.unlink()

def _assert_safe_tmp_output(source: Path, work: Path) -> None:
    source = source.resolve()
    work = work.resolve()
    if work == source or source in work.parents or work in source.parents:
        raise ValueError(f"Unsafe work dir; must not overlap source. source={source} work={work}")
    if not any(part.lower() == "tmp" for part in work.parts):
        raise ValueError(f"Human UE roundtrip work dir must be under a tmp directory: {work}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Compile BpyDecompiler human source to BPY under tmp, then run ExportBpy UE import/re-export roundtrip."
    )
    parser.add_argument("source_dir", help="ExportBpy package dir, or human dir when --source-is-human is set.")
    parser.add_argument("work_dir", help="Temporary output dir; must be under tmp/ and must not overlap source.")
    parser.add_argument("--label")
    parser.add_argument("--target-path", help="Optional Unreal asset path to import into, preferably a /Game/tmp/... scratch asset.")
    parser.add_argument("--source-is-human", action="store_true")
    parser.add_argument("--no-compile-asset", action="store_true")
    args = parser.parse_args(argv)

    result = validate_human_ue_roundtrip(
        args.source_dir,
        args.work_dir,
        label=args.label,
        target_path=args.target_path,
        source_is_human=args.source_is_human,
        compile_asset=not args.no_compile_asset,
    )
    print(f"source_dir={result.source_dir}")
    print(f"human_dir={result.human_dir}")
    print(f"compiled_bpy_dir={result.compiled_bpy_dir}")
    print(f"bundle_dir={result.bundle_dir}")
    print(f"report_path={result.report_path}")
    print(f"success={result.report.get('success', False)}")
    warnings = result.report.get("warnings") or []
    for warning in warnings:
        print(f"WARNING: {warning}")
    diff = result.report.get("diff") or {}
    if diff:
        print(f"diff_equal={diff.get('equal', False)}")
        print(f"changed={len(diff.get('changed_files') or [])} missing={len(diff.get('missing_in_export') or [])} extra={len(diff.get('extra_in_export') or [])}")
    if result.report_path.is_file():
        print(json.dumps(result.report, indent=2, ensure_ascii=False)[:4000])
    return 0 if result.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
