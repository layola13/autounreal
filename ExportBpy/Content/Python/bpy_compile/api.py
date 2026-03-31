# Copyright sonygodx@gmailcom. All Rights Reserved.
# -*- coding: utf-8 -*-

from __future__ import annotations

import difflib
import importlib
import json
import os
import ast
from datetime import datetime
from pathlib import Path
import sys
from typing import Any, Dict, List, Optional, Tuple

from ue_bp_dsl.core import Blueprint

import asset_exporter
import asset_importer
import bp_exporter
import bp_importer
import bp_validator

try:
    import unreal  # type: ignore

    _HAS_UNREAL = True
except ImportError:
    _HAS_UNREAL = False

from .compiler import compile_loaded_package
from .emitter import emit_bpy_package, emit_debug_package
from .loader import load_upper_package


def ensure_plugin_python_path() -> str:
    plugin_python_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    if plugin_python_dir not in sys.path:
        sys.path.insert(0, plugin_python_dir)
    return plugin_python_dir


def compile_package(
    source_dir: str,
    *,
    reference_dir: Optional[str] = None,
    target_path: Optional[str] = None,
    emit_debug_bpy: bool = False,
    debug_output_dir: Optional[str] = None,
) -> Blueprint:
    ensure_plugin_python_path()
    package = load_upper_package(source_dir, reference_dir=reference_dir)
    blueprint = compile_loaded_package(package)
    if target_path:
        blueprint._path = target_path
    if emit_debug_bpy:
        debug_root = debug_output_dir or os.path.join(
            os.path.abspath(os.path.join(source_dir, "..", "..")),
            "Intermediate",
            "BpyCompile",
            os.path.basename(os.path.abspath(source_dir)),
        )
        emit_debug_package(blueprint, debug_root)
    return blueprint


def compile_to_bpy_package(
    source_dir: str,
    *,
    reference_dir: Optional[str] = None,
    output_dir: Optional[str] = None,
    target_path: Optional[str] = None,
) -> str:
    ensure_plugin_python_path()
    blueprint = compile_package(
        source_dir,
        reference_dir=reference_dir,
        target_path=target_path,
        emit_debug_bpy=False,
    )
    package_output_dir = output_dir or _default_compiled_package_dir(source_dir)
    return emit_bpy_package(blueprint, package_output_dir, clean_package=True)


def compile_and_import(
    source_dir: str,
    *,
    reference_dir: Optional[str] = None,
    target_path: Optional[str] = None,
    compile_asset: bool = True,
    emit_debug_bpy: bool = False,
    debug_output_dir: Optional[str] = None,
    asset_meta_dir: Optional[str] = None,
) -> Tuple[bool, str]:
    """
    编译并导入蓝图，可选同时导入关联的独立资产 meta 文件。

    参数
    ----
    asset_meta_dir : str, optional
        包含 *__asset__.meta.py 文件的目录。若传入，会在蓝图导入成功后
        自动调用 asset_importer.import_asset_meta_dir() 写回资产属性。
        若不传，跳过独立资产导入步骤。
    """
    blueprint = compile_package(
        source_dir,
        reference_dir=reference_dir,
        target_path=target_path,
        emit_debug_bpy=emit_debug_bpy,
        debug_output_dir=debug_output_dir,
    )
    ok, err = bp_importer.import_blueprint_object(
        blueprint,
        target_path=target_path,
        compile_blueprint=compile_asset,
    )
    if not ok:
        return ok, err

    # Import standalone asset metas (InputAction / IMC / Chooser / PoseSearchDatabase)
    if asset_meta_dir and os.path.isdir(asset_meta_dir):
        results = asset_importer.import_asset_meta_dir(asset_meta_dir)
        if results["failed"]:
            msgs = [f"{f['scroll']}: {f['error']}" for f in results["failed"]]
            return False, "独立资产导入部分失败:\n" + "\n".join(msgs)

    return True, ""


def import_bpy_package(
    source_path: str,
    *,
    target_path: Optional[str] = None,
    compile_asset: bool = True,
    reference_dir: Optional[str] = None,
    emit_debug_bpy: bool = False,
    debug_output_dir: Optional[str] = None,
    asset_meta_dir: Optional[str] = None,
    use_upper_compiler: Optional[bool] = None,
) -> Dict[str, object]:
    """
    Unified import entry for both __upper__.py packages and normal .bp.py packages.
    """
    ensure_plugin_python_path()
    normalized_source = os.path.abspath(source_path)
    normalized_target = target_path or None
    should_use_upper = _is_upper_package_source(normalized_source) if use_upper_compiler is None else bool(use_upper_compiler)

    if should_use_upper:
        compile_source = normalized_source if os.path.isdir(normalized_source) else os.path.dirname(normalized_source)
        ok, err = compile_and_import(
            compile_source,
            reference_dir=reference_dir,
            target_path=normalized_target,
            compile_asset=compile_asset,
            emit_debug_bpy=emit_debug_bpy,
            debug_output_dir=debug_output_dir,
            asset_meta_dir=asset_meta_dir,
        )
        return {
            "success": bool(ok),
            "error": "" if ok else str(err),
            "asset_path": normalized_target or "",
            "import_mode": "upper_package",
            "compiled": bool(ok and compile_asset),
        }

    bp_importer_module = importlib.reload(bp_importer)
    return bp_importer_module.import_path_with_details(
        normalized_source,
        target_path=normalized_target,
        compile_blueprint=compile_asset,
    )


def import_asset_metas(asset_meta_dir: str) -> Dict:
    """
    单独导入一个目录下所有 *__asset__.meta.py（不涉及蓝图编译）。

    返回 {"succeeded": [...], "failed": [...]} 字典。
    """
    ensure_plugin_python_path()
    return asset_importer.import_asset_meta_dir(asset_meta_dir)


def roundtrip_bpy_package(
    source_path: str,
    *,
    target_path: Optional[str] = None,
    compile_asset: bool = True,
    reference_dir: Optional[str] = None,
    emit_debug_bpy: bool = False,
    debug_output_dir: Optional[str] = None,
    asset_meta_dir: Optional[str] = None,
    use_upper_compiler: Optional[bool] = None,
    bundle_dir: Optional[str] = None,
) -> Dict[str, object]:
    """
    Standard regression flow for ExportBpy packages.

    Stages:
      1. Validate source package
      2. Import/compile into the current Unreal session
      3. Re-export to a report bundle
      4. Diff source package against the exported package

    Notes:
      - When source_path points at an __upper__.py package, this helper first
        compiles it to a temporary low-level bpy package inside the bundle.
      - Outside Unreal Python, only source validation is performed.
    """
    ensure_plugin_python_path()

    normalized_source = os.path.abspath(source_path)
    normalized_target = target_path or None
    should_use_upper = (
        _is_upper_package_source(normalized_source)
        if use_upper_compiler is None
        else bool(use_upper_compiler)
    )

    bundle_root = os.path.abspath(
        bundle_dir or _default_roundtrip_bundle_dir(normalized_source, normalized_target)
    )
    os.makedirs(bundle_root, exist_ok=True)

    report: Dict[str, Any] = {
        "success": False,
        "source_path": normalized_source,
        "target_path": normalized_target or "",
        "source_mode": "upper_package" if should_use_upper else "bpy_package",
        "bundle_dir": bundle_root,
        "prepared_source_dir": "",
        "source_validation": {},
        "import_result": {},
        "export_result": {},
        "diff": {},
        "warnings": [],
        "report_path": os.path.join(bundle_root, "report.json"),
        "diff_path": os.path.join(bundle_root, "roundtrip.diff.txt"),
    }

    try:
        prepared_source_dir, package_kind = _prepare_roundtrip_source(
            normalized_source,
            should_use_upper=should_use_upper,
            reference_dir=reference_dir,
            target_path=normalized_target,
            bundle_root=bundle_root,
        )
    except Exception as exc:
        report["warnings"].append(f"Failed to prepare roundtrip source: {exc}")
        _persist_roundtrip_report(report)
        return report

    report["prepared_source_dir"] = prepared_source_dir
    report["package_kind"] = package_kind

    validation = _validate_roundtrip_source(prepared_source_dir, package_kind)
    report["source_validation"] = validation
    if not validation.get("ok", False):
        _persist_roundtrip_report(report)
        return report

    if not _HAS_UNREAL:
        report["success"] = True
        report["warnings"].append(
            "Unreal Python is unavailable; import/export stages were skipped after source validation."
        )
        _persist_roundtrip_report(report)
        return report

    import_result = import_bpy_package(
        normalized_source,
        target_path=normalized_target,
        compile_asset=compile_asset,
        reference_dir=reference_dir,
        emit_debug_bpy=emit_debug_bpy,
        debug_output_dir=debug_output_dir,
        asset_meta_dir=asset_meta_dir,
        use_upper_compiler=use_upper_compiler,
    )
    report["import_result"] = import_result
    if not import_result.get("success"):
        _persist_roundtrip_report(report)
        return report

    imported_asset_path = str(
        import_result.get("asset_path") or normalized_target or ""
    )
    if not imported_asset_path:
        report["warnings"].append("Import succeeded but no asset_path was returned.")
        _persist_roundtrip_report(report)
        return report

    export_root = os.path.join(bundle_root, "exported")
    os.makedirs(export_root, exist_ok=True)
    export_result = _export_roundtrip_package(
        imported_asset_path,
        package_kind=package_kind,
        export_root=export_root,
    )
    report["export_result"] = export_result
    if not export_result.get("success"):
        _persist_roundtrip_report(report)
        return report

    exported_package_dir = str(export_result.get("package_dir") or "")
    diff_summary, diff_text = _diff_package_dirs(prepared_source_dir, exported_package_dir)
    diff_summary["diff_path"] = report["diff_path"]
    report["diff"] = diff_summary

    import_validation_ok = bool(import_result.get("validation_ok", True))
    report["success"] = bool(
        validation.get("ok", False)
        and import_result.get("success", False)
        and export_result.get("success", False)
        and import_validation_ok
        and diff_summary.get("equal", False)
    )

    if diff_text:
        with open(report["diff_path"], "w", encoding="utf-8", newline="\n") as handle:
            handle.write(diff_text)
    elif os.path.isfile(report["diff_path"]):
        os.remove(report["diff_path"])

    _persist_roundtrip_report(report)
    return report


def _prepare_roundtrip_source(
    source_path: str,
    *,
    should_use_upper: bool,
    reference_dir: Optional[str],
    target_path: Optional[str],
    bundle_root: str,
) -> Tuple[str, str]:
    if should_use_upper:
        compile_source = (
            source_path if os.path.isdir(source_path) else os.path.dirname(source_path)
        )
        prepared_root = os.path.join(bundle_root, "prepared_source")
        os.makedirs(prepared_root, exist_ok=True)
        prepared_dir = compile_to_bpy_package(
            compile_source,
            reference_dir=reference_dir,
            output_dir=prepared_root,
            target_path=target_path,
        )
        return prepared_dir, _detect_package_kind(prepared_dir)

    prepared_dir = _resolve_roundtrip_package_dir(source_path)
    return prepared_dir, _detect_package_kind(prepared_dir)


def _resolve_roundtrip_package_dir(source_path: str) -> str:
    normalized_path = os.path.abspath(source_path)
    if os.path.isdir(normalized_path):
        return normalized_path

    if os.path.isfile(normalized_path):
        file_name = os.path.basename(normalized_path).lower()
        if file_name in {"__bp__.bp.py", "__upper__.py"}:
            return os.path.dirname(normalized_path)

    raise ValueError(
        "Roundtrip regression requires a package directory, __bp__.bp.py, or __upper__.py source."
    )


def _detect_package_kind(package_dir: str) -> str:
    main_path = os.path.join(package_dir, asset_importer.MAIN_BP_FILE)
    descriptor = _load_literal_assignment(main_path, "bp")
    if isinstance(descriptor, dict) and descriptor.get("kind") == "standalone_asset":
        return "standalone_asset_package"
    return "blueprint_package"


def _validate_roundtrip_source(package_dir: str, package_kind: str) -> Dict[str, object]:
    if package_kind == "standalone_asset_package":
        errors: List[str] = []
        main_path = os.path.join(package_dir, asset_importer.MAIN_BP_FILE)
        meta_path = os.path.join(package_dir, asset_importer.DEFAULT_META_FILE)
        descriptor = _load_literal_assignment(main_path, "bp")
        meta = _load_literal_assignment(meta_path, "META")

        if not os.path.isdir(package_dir):
            errors.append(f"Directory does not exist: {package_dir}")
        if descriptor is None:
            errors.append(f"Cannot parse standalone asset descriptor: {main_path}")
        elif descriptor.get("kind") != "standalone_asset":
            errors.append(f"Unsupported standalone package descriptor kind: {descriptor.get('kind')}")
        if meta is None:
            errors.append(f"Cannot parse standalone asset meta: {meta_path}")

        return {
            "ok": len(errors) == 0,
            "errors": errors,
            "kind": package_kind,
        }

    valid, errors = bp_validator.validate_path(package_dir)
    return {
        "ok": bool(valid),
        "errors": list(errors),
        "kind": package_kind,
    }


def _export_roundtrip_package(
    asset_path: str,
    *,
    package_kind: str,
    export_root: str,
) -> Dict[str, object]:
    normalized_asset_path = asset_path.strip()
    if package_kind == "standalone_asset_package":
        return asset_exporter.export_asset_bpy_package(normalized_asset_path, export_root)

    ok, err = bp_exporter.export_directory(normalized_asset_path, export_root)
    package_dir = os.path.join(export_root, _asset_name_from_path(normalized_asset_path))
    return {
        "success": bool(ok),
        "error": str(err or ""),
        "asset_path": normalized_asset_path,
        "package_dir": package_dir,
        "format": "bpy_directory",
    }


def _diff_package_dirs(source_dir: str, exported_dir: str) -> Tuple[Dict[str, object], str]:
    source_files = _collect_package_files(source_dir)
    exported_files = _collect_package_files(exported_dir)

    source_keys = set(source_files)
    exported_keys = set(exported_files)
    missing_in_export = sorted(source_keys - exported_keys)
    extra_in_export = sorted(exported_keys - source_keys)

    changed_files: List[Dict[str, object]] = []
    diff_chunks: List[str] = []
    for relative_path in sorted(source_keys & exported_keys):
        source_text = _read_text_normalized(source_files[relative_path])
        exported_text = _read_text_normalized(exported_files[relative_path])
        if source_text == exported_text:
            continue

        diff_lines = list(
            difflib.unified_diff(
                source_text.splitlines(),
                exported_text.splitlines(),
                fromfile=f"source/{relative_path}",
                tofile=f"exported/{relative_path}",
                lineterm="",
            )
        )
        changed_files.append(
            {
                "path": relative_path,
                "diff_line_count": len(diff_lines),
            }
        )
        diff_chunks.append("\n".join(diff_lines))

    summary: Dict[str, object] = {
        "equal": not missing_in_export and not extra_in_export and not changed_files,
        "source_file_count": len(source_files),
        "exported_file_count": len(exported_files),
        "missing_in_export": missing_in_export,
        "extra_in_export": extra_in_export,
        "changed_files": changed_files,
    }
    return summary, "\n\n".join(chunk for chunk in diff_chunks if chunk)


def _collect_package_files(package_dir: str) -> Dict[str, str]:
    file_map: Dict[str, str] = {}
    for root, dirs, files in os.walk(package_dir):
        dirs[:] = [item for item in dirs if item != "__pycache__"]
        for file_name in sorted(files):
            if file_name.endswith((".pyc", ".pyo")):
                continue
            full_path = os.path.join(root, file_name)
            relative_path = os.path.relpath(full_path, package_dir).replace("\\", "/")
            file_map[relative_path] = full_path
    return file_map


def _read_text_normalized(path: str) -> str:
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read().replace("\r\n", "\n")


def _persist_roundtrip_report(report: Dict[str, Any]) -> None:
    report_path = str(report.get("report_path") or "")
    if not report_path:
        return
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    with open(report_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")


def _default_roundtrip_bundle_dir(source_path: str, target_path: Optional[str]) -> str:
    project_root = _project_root_dir()
    package_name = _asset_name_from_path(target_path or source_path)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return os.path.join(
        project_root,
        "Saved",
        ".Aura",
        "reports",
        "exportbpy_roundtrip",
        f"{package_name}_{timestamp}",
    )


def _project_root_dir() -> str:
    return str(Path(__file__).resolve().parents[5])


def _asset_name_from_path(asset_path: str) -> str:
    normalized = asset_path.replace("\\", "/").rstrip("/")
    token = normalized.split("/")[-1]
    if token.lower() in {"__bp__.bp.py", "__upper__.py"}:
        parent = normalized.rsplit("/", 1)[0]
        return parent.split("/")[-1] or "package"
    if "." in token:
        token = token.split(".")[-1] or token.split(".")[0]
    return token or "package"


def _load_literal_assignment(py_path: str, variable_name: str) -> Optional[Any]:
    if not os.path.isfile(py_path):
        return None

    try:
        with open(py_path, "r", encoding="utf-8") as handle:
            tree = ast.parse(handle.read(), filename=py_path)
    except Exception:
        return None

    for stmt in tree.body:
        if not isinstance(stmt, ast.Assign):
            continue
        for target in stmt.targets:
            if not isinstance(target, ast.Name) or target.id != variable_name:
                continue
            try:
                return ast.literal_eval(stmt.value)
            except Exception:
                return None
    return None


def _default_compiled_package_dir(source_dir: str) -> str:
    package_name = os.path.basename(os.path.abspath(source_dir))
    project_dir = os.path.abspath(os.path.join(source_dir, "..", ".."))
    return os.path.join(project_dir, "ExportedBlueprints", "bpy", package_name)


def _is_upper_package_source(source_path: str) -> bool:
    clean_name = os.path.basename(source_path)
    if clean_name.lower() == "__upper__.py":
        return True
    if os.path.isdir(source_path):
        return os.path.isfile(os.path.join(source_path, "__upper__.py"))
    return False
