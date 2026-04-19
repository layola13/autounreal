# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-
"""
bp_importer.py — 从 Python DSL 导入蓝图到 Unreal

支持两种输入：
  1. 目录模式（推荐）：目录内包含 __bp__.bp.py + evt_/fn_/macro_/tl_*.bp.py + *_meta.py
  2. 单文件模式：单个低层 .bp.py 文件，或目录级入口 <BlueprintName>.bp.py
"""

from __future__ import annotations

import ast
import importlib.util
import json
import math
import os
import re
import sys
import uuid
from typing import Any, Dict, List, Optional, Set, Tuple

try:
    import unreal  # type: ignore
    _HAS_UNREAL = True
except ImportError:
    _HAS_UNREAL = False


GRAPH_PREFIXES = ("evt_", "fn_", "macro_", "tl_")
MAIN_BP_FILE = "__bp__.bp.py"


def _env_flag_enabled(name: str, default: bool = False) -> bool:
    raw_value = str(os.environ.get(name, "")).strip().lower()
    if not raw_value:
        return bool(default)
    return raw_value in {"1", "true", "yes", "on"}


def _begin_local_python_package_import(package_root: str) -> Tuple[List[str], Dict[str, Any]]:
    plugin_python_dir = os.path.dirname(os.path.abspath(__file__))
    original_sys_path = list(sys.path)
    normalized_plugin_dir = os.path.normcase(os.path.normpath(plugin_python_dir))
    sys.path[:] = [plugin_python_dir] + [
        path for path in original_sys_path
        if os.path.normcase(os.path.normpath(path or "")) != normalized_plugin_dir
    ]

    saved_modules: Dict[str, Any] = {}
    stale_keys = [
        key for key in list(sys.modules)
        if key == package_root or key.startswith(package_root + ".")
    ]
    for key in stale_keys:
        saved_modules[key] = sys.modules.pop(key)

    return original_sys_path, saved_modules


def _end_local_python_package_import(
    package_root: str,
    original_sys_path: List[str],
    saved_modules: Dict[str, Any],
) -> None:
    stale_keys = [
        key for key in list(sys.modules)
        if key == package_root or key.startswith(package_root + ".")
    ]
    for key in stale_keys:
        sys.modules.pop(key, None)

    sys.path[:] = original_sys_path
    sys.modules.update(saved_modules)


def _exec_pin_variants(pin_name: str) -> Tuple[str, ...]:
    normalized = str(pin_name or "")
    if normalized == "exec":
        return ("exec", "execute")
    if normalized == "execute":
        return ("execute", "exec")
    return (normalized,)


def _graph_module_stem(file_name: str) -> str:
    if file_name.endswith(".bp.py"):
        return file_name[:-6]
    if file_name.endswith(".py"):
        return file_name[:-3]
    return os.path.splitext(file_name)[0]


def _is_graph_dsl_file(file_name: str) -> bool:
    if file_name == MAIN_BP_FILE or file_name.endswith("_meta.py"):
        return False
    if not file_name.endswith(".bp.py"):
        return False
    return any(file_name.startswith(prefix) for prefix in GRAPH_PREFIXES)


def _graph_meta_path(dir_path: str, file_name: str) -> str:
    return os.path.join(dir_path, _graph_module_stem(file_name) + "_meta.py")


def _is_directory_entry_file(py_path: str) -> bool:
    file_name = os.path.basename(py_path)
    if file_name == MAIN_BP_FILE:
        return True
    if not file_name.endswith(".bp.py") or file_name.endswith("_meta.py"):
        return False
    if _is_graph_dsl_file(file_name):
        return False
    directory_name = os.path.basename(os.path.dirname(py_path.rstrip("\\/")))
    return bool(directory_name) and file_name == f"{directory_name}.bp.py"


def _load_meta_dict(meta_path: str) -> Optional[Dict[str, Any]]:
    if not os.path.isfile(meta_path):
        return None

    with open(meta_path, "r", encoding="utf-8") as handle:
        tree = ast.parse(handle.read(), filename=meta_path)

    for stmt in tree.body:
        if not isinstance(stmt, ast.Assign):
            continue
        for target in stmt.targets:
            if isinstance(target, ast.Name) and target.id == "META":
                value = ast.literal_eval(stmt.value)
                return value if isinstance(value, dict) else None
    return None


def _find_legacy_variable_export_path(source_path: str, blueprint_name: str) -> Optional[str]:
    if not blueprint_name:
        return None

    base_dir = source_path if os.path.isdir(source_path) else os.path.dirname(source_path)
    if not base_dir:
        return None

    cursor = os.path.abspath(base_dir)
    search_roots: List[str] = []
    for _ in range(6):
        search_roots.append(cursor)
        parent = os.path.dirname(cursor)
        if not parent or parent == cursor:
            break
        cursor = parent

    file_name = f"{blueprint_name}.yaml"
    for root in search_roots:
        for candidate in (
            os.path.join(root, "variables", file_name),
            os.path.join(root, "ExportedBlueprints", "variables", file_name),
        ):
            if os.path.isfile(candidate):
                return candidate
    return None


def _extract_legacy_controlrig_dest_properties(node_text: str) -> List[str]:
    match = re.search(r"DestPropertyNames=\((.*?)\)", node_text)
    if match is None:
        return []

    seen: Set[str] = set()
    result: List[str] = []
    for value in re.findall(r'"([^\"]+)"', match.group(1)):
        if value and value not in seen:
            seen.add(value)
            result.append(value)
    return result


def _build_legacy_controlrig_custom_pin_properties(dest_properties: List[str]) -> str:
    entries = [
        f'(PropertyName="{name}",bShowPin=True,bCanToggleVisibility=True,bIsOverrideEnabled=False)'
        for name in dest_properties
        if name
    ]
    if not entries:
        return ""
    return "(" + ",".join(entries) + ")"


def _load_legacy_anim_node_props(variable_export_path: str) -> Dict[str, Dict[str, Any]]:
    if not os.path.isfile(variable_export_path):
        return {}

    node_props: Dict[str, Dict[str, Any]] = {}
    current_name = ""
    current_cpp_type = ""
    with open(variable_export_path, "r", encoding="utf-8") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            if line.startswith("name: "):
                try:
                    current_name = str(ast.literal_eval(line.split(":", 1)[1].strip()))
                except Exception:
                    current_name = line.split(":", 1)[1].strip().strip("\"'")
                continue

            if line.startswith("cpp_type: "):
                try:
                    current_cpp_type = str(ast.literal_eval(line.split(":", 1)[1].strip()))
                except Exception:
                    current_cpp_type = line.split(":", 1)[1].strip().strip("\"'")
                continue

            if not line.startswith("default_value: ") or not current_name or not current_cpp_type:
                continue

            try:
                default_value = str(ast.literal_eval(line.split(":", 1)[1].strip()))
            except Exception:
                default_value = line.split(":", 1)[1].strip().strip("\"'")

            if current_name.startswith("AnimGraphNode_") and current_cpp_type.startswith("FAnimNode_"):
                props: Dict[str, Any] = {"Node": default_value}
                if current_cpp_type == "FAnimNode_ControlRig":
                    custom_pin_props = _build_legacy_controlrig_custom_pin_properties(
                        _extract_legacy_controlrig_dest_properties(default_value)
                    )
                    if custom_pin_props:
                        props["CustomPinProperties"] = custom_pin_props
                node_props[current_name] = props

            current_name = ""
            current_cpp_type = ""

    return node_props


def _augment_legacy_animgraph_node_props(bp: Any, source_path: str) -> None:
    blueprint_name = str(getattr(bp, "_name", "") or "").strip()
    if not blueprint_name:
        blueprint_name = os.path.basename(source_path.rstrip("\\/"))
        if blueprint_name.endswith(".bp.py"):
            blueprint_name = blueprint_name[:-6]

    variable_export_path = _find_legacy_variable_export_path(source_path, blueprint_name)
    if not variable_export_path:
        return

    legacy_node_props = _load_legacy_anim_node_props(variable_export_path)
    if not legacy_node_props:
        return

    for graph in getattr(bp, "_graphs", []):
        if str(getattr(graph, "name", "") or "") != "AnimGraph":
            continue

        for node in getattr(graph, "nodes", []):
            readable_name = str(getattr(node, "readable_name", "") or "")
            props = legacy_node_props.get(readable_name)
            if not props:
                continue

            extra_props = getattr(node, "extra_props", None)
            if not isinstance(extra_props, dict):
                continue

            for key, value in props.items():
                extra_props.setdefault(key, value)


def import_directory(
    dir_path: str,
    target_path: Optional[str] = None,
    compile_blueprint: bool = True,
) -> Tuple[bool, str]:
    details = import_directory_with_details(
        dir_path, target_path=target_path, compile_blueprint=compile_blueprint
    )
    return bool(details.get("success")), str(details.get("error", ""))


def import_directory_with_details(
    dir_path: str,
    target_path: Optional[str] = None,
    compile_blueprint: bool = True,
) -> Dict[str, Any]:
    """
    从 DSL 目录导入蓝图或 standalone asset package。
    """
    if not os.path.isdir(dir_path):
        return _error_details(f"目录不存在: {dir_path}")

    try:
        bp_obj = _exec_directory_dsl(dir_path)
    except Exception as exc:
        return _error_details(f"执行 DSL 目录失败: {exc}")

    if _is_standalone_asset_descriptor(bp_obj):
        return _import_standalone_asset_directory(
            dir_path, bp_obj, target_path=target_path
        )

    return _import_blueprint_object_with_details(
        bp_obj, target_path, compile_blueprint=compile_blueprint
    )


def import_file(
    py_path: str,
    target_path: Optional[str] = None,
    compile_blueprint: bool = True,
) -> Tuple[bool, str]:
    details = import_file_with_details(
        py_path, target_path=target_path, compile_blueprint=compile_blueprint
    )
    return bool(details.get("success")), str(details.get("error", ""))


def import_file_with_details(
    py_path: str,
    target_path: Optional[str] = None,
    compile_blueprint: bool = True,
) -> Dict[str, Any]:
    """
    兼容旧单文件导入。
    """
    if not os.path.isfile(py_path):
        return _error_details(f"文件不存在: {py_path}")

    if _is_directory_entry_file(py_path):
        return import_directory_with_details(
            os.path.dirname(py_path),
            target_path=target_path,
            compile_blueprint=compile_blueprint,
        )
    if not py_path.endswith(".bp.py"):
        return _error_details(f"仅支持导入 .bp.py 文件: {py_path}")

    try:
        bp_obj = _exec_file_dsl(py_path)
    except Exception as exc:
        return _error_details(f"执行 DSL 脚本失败: {exc}")

    return _import_blueprint_object_with_details(
        bp_obj, target_path, compile_blueprint=compile_blueprint
    )


def import_path(
    path: str,
    target_path: Optional[str] = None,
    compile_blueprint: bool = True,
) -> Tuple[bool, str]:
    details = import_path_with_details(
        path, target_path=target_path, compile_blueprint=compile_blueprint
    )
    return bool(details.get("success")), str(details.get("error", ""))


def import_path_with_details(
    path: str,
    target_path: Optional[str] = None,
    compile_blueprint: bool = True,
) -> Dict[str, Any]:
    if os.path.isdir(path):
        return import_directory_with_details(
            path, target_path=target_path, compile_blueprint=compile_blueprint
        )
    return import_file_with_details(
        path, target_path=target_path, compile_blueprint=compile_blueprint
    )


def import_batch(files: Dict[str, str]) -> Dict[str, Any]:
    succeeded, failed = [], []
    for source_path, target in files.items():
        ok, err = import_path(source_path, target)
        if ok:
            succeeded.append(target)
        else:
            failed.append({"target": target, "error": err})
    return {"succeeded": succeeded, "failed": failed}


def import_blueprint_object(
    bp_obj: Any,
    target_path: Optional[str] = None,
    compile_blueprint: bool = True,
) -> Tuple[bool, str]:
    """
    导入一个已经构建完成的 Blueprint 对象。
    """
    details = _import_blueprint_object_with_details(
        bp_obj, target_path, compile_blueprint=compile_blueprint
    )
    return bool(details.get("success")), str(details.get("error", ""))


def _import_blueprint_object(
    bp_obj: Any,
    target_path: Optional[str],
    compile_blueprint: bool = True,
) -> Tuple[bool, str]:
    details = _import_blueprint_object_with_details(
        bp_obj, target_path, compile_blueprint=compile_blueprint
    )
    return bool(details.get("success")), str(details.get("error", ""))


def _import_blueprint_object_with_details(
    bp_obj: Any,
    target_path: Optional[str],
    compile_blueprint: bool = True,
) -> Dict[str, Any]:
    asset_path = target_path or bp_obj._path
    if not asset_path:
        return _error_details("未指定目标资产路径，且脚本中 Blueprint(path=...) 未设置")

    try:
        payload = bp_obj.to_dict()
        _augment_explicit_variable_type_metadata(payload)
        _sanitize_problematic_default_strings(payload)
        preflight_stats = _collect_expected_import_stats(payload)
        json_str = json.dumps(payload, ensure_ascii=False, indent=2)
    except Exception as exc:
        return _error_details(f"序列化失败: {exc}")

    if not _HAS_UNREAL:
        print(f"[bp_importer] dry-run — target: {asset_path}")
        print(json_str[:2000])
        return {
            "success": True,
            "error": "",
            "asset_path": asset_path,
            "import_mode": "bpy_directory",
            "compiled": bool(compile_blueprint),
            "validation_ok": True,
            "preflight_summary": preflight_stats,
            "validation_summary": {"ok": True, "warnings": ["dry-run"]},
        }

    ok, err = _call_cpp_importer(
        json_str,
        asset_path,
        compile_blueprint=bool(compile_blueprint),
    )
    if not ok and err:
        err = _describe_missing_connection_nodes(payload, err)
    bridge_asset_path = _normalize_bridge_blueprint_path(asset_path)
    parent_class_path = str(payload.get("parent", "") or "")
    is_anim_blueprint_payload = "AnimInstance" in parent_class_path
    force_py_post_repair = str(
        os.environ.get("EXPORTBPY_FORCE_PY_POST_REPAIR", "")
    ).strip().lower() in {"1", "true", "yes", "on"}
    disable_py_post_repair = str(
        os.environ.get("EXPORTBPY_DISABLE_PY_POST_REPAIR", "")
    ).strip().lower() in {"1", "true", "yes", "on"}
    enable_py_post_save = _env_flag_enabled("EXPORTBPY_ENABLE_PY_POST_SAVE", default=False)
    enable_py_import_validation = _env_flag_enabled(
        "EXPORTBPY_ENABLE_PY_IMPORT_VALIDATION",
        default=False,
    )
    run_legacy_post_import_repairs = (
        not disable_py_post_repair
        and (
            force_py_post_repair
            or (not compile_blueprint)
            or (not is_anim_blueprint_payload)
        )
    )
    expected_delegate_bindings = preflight_stats.get("expected_create_delegate_bindings", [])
    has_expected_delegate_bindings = bool(
        isinstance(expected_delegate_bindings, list) and expected_delegate_bindings
    )
    repair_ok = ok
    repair_err = ""
    post_repair_ok = True
    post_repair_err = ""
    compiled_ok = bool(ok if compile_blueprint else True)
    compile_err = ""
    delegate_restore_ok = True
    delegate_restore_err = ""
    delegate_recompile_ok = True
    delegate_recompile_err = ""
    delegate_final_restore_ok = True
    delegate_final_restore_err = ""

    if ok and run_legacy_post_import_repairs:
        repair_ok, repair_err = _repair_imported_blueprint_pin_defaults(
            bridge_asset_path,
            payload,
            stage_label="pre-compile",
        )
        if compile_blueprint:
            compiled_ok, compile_err = _compile_blueprint_with_bridge(bridge_asset_path)
        if compile_blueprint and compiled_ok:
            post_repair_ok, post_repair_err = _repair_imported_blueprint_pin_defaults(
                bridge_asset_path,
                payload,
                stage_label="post-compile",
            )
        if compile_blueprint and has_expected_delegate_bindings:
            delegate_restore_ok, delegate_restore_err = _restore_create_delegate_bindings_with_bridge(
                json_str,
                bridge_asset_path,
            )
            if delegate_restore_ok:
                delegate_recompile_ok, delegate_recompile_err = _compile_blueprint_with_bridge(
                    bridge_asset_path
                )
            if delegate_restore_ok and delegate_recompile_ok:
                delegate_final_restore_ok, delegate_final_restore_err = _restore_create_delegate_bindings_with_bridge(
                    json_str,
                    bridge_asset_path,
                )
        if (
            (not compile_blueprint or compiled_ok)
            and delegate_restore_ok
            and delegate_recompile_ok
            and delegate_final_restore_ok
            and enable_py_post_save
        ):
            _save_asset_if_possible(bridge_asset_path)
    elif ok and enable_py_post_save:
        _save_asset_if_possible(bridge_asset_path)

    if ok and enable_py_import_validation:
        validation_summary = _validate_imported_blueprint(
            asset_path,
            payload,
            preflight_stats=preflight_stats,
        )
        if ok and validation_summary.get("missing_components"):
            if enable_py_post_save:
                _save_asset_if_possible(bridge_asset_path)
            retried_summary = _validate_imported_blueprint(
                asset_path,
                payload,
                preflight_stats=preflight_stats,
            )
            if retried_summary:
                validation_summary = retried_summary
    elif ok:
        validation_summary = {
            "ok": True,
            "warnings": [
                "python import validation skipped; set EXPORTBPY_ENABLE_PY_IMPORT_VALIDATION=1 to enable"
            ],
        }
    else:
        validation_summary = {}
    if validation_summary and isinstance(validation_summary, dict):
        validation_warnings = validation_summary.get("warnings")
        if not isinstance(validation_warnings, list):
            validation_warnings = []
            validation_summary["warnings"] = validation_warnings
        if repair_err:
            validation_warnings.append(f"pin repair (pre-compile) diagnostics: {repair_err}")
        if post_repair_err:
            validation_warnings.append(f"pin repair (post-compile) diagnostics: {post_repair_err}")
    validation_ok = bool(validation_summary.get("ok", False)) if validation_summary else False
    success = bool(
        ok
        and (compiled_ok if compile_blueprint else True)
        and delegate_restore_ok
        and delegate_recompile_ok
        and delegate_final_restore_ok
        and validation_ok
    )
    error_parts = [
        part
        for part in (
            err,
            compile_err,
            delegate_restore_err,
            delegate_recompile_err,
            delegate_final_restore_err,
        )
        if part
    ]
    if ok and not validation_ok:
        error_parts.append("strict import validation failed")
    return {
        "success": success,
        "error": " | ".join(error_parts),
        "asset_path": asset_path,
        "import_mode": "bpy_directory",
        "compiled": bool(success and compile_blueprint),
        "validation_ok": validation_ok,
        "preflight_summary": preflight_stats,
        "validation_summary": validation_summary,
    }


def _exec_directory_dsl(dir_path: str):
    main_path = os.path.join(dir_path, MAIN_BP_FILE)
    if not os.path.isfile(main_path):
        raise FileNotFoundError(f"目录缺少 {MAIN_BP_FILE}: {dir_path}")

    original_sys_path, saved_dsl_modules = _begin_local_python_package_import("ue_bp_dsl")
    package_name = f"_exportbpy_pkg_{uuid.uuid4().hex}"
    spec = importlib.util.spec_from_file_location(package_name, main_path)
    if spec is None or spec.loader is None:
        _end_local_python_package_import("ue_bp_dsl", original_sys_path, saved_dsl_modules)
        raise ImportError(f"无法为 {main_path} 创建 import spec")

    module = importlib.util.module_from_spec(spec)
    sys.modules[package_name] = module

    previous_dont_write_bytecode = bool(getattr(sys, "dont_write_bytecode", False))
    sys.dont_write_bytecode = True
    try:
        spec.loader.exec_module(module)
        bp = getattr(module, "bp", None)
        if bp is None:
            raise ValueError(f"{os.path.basename(main_path)} 未定义顶层变量 'bp'")

        if _is_standalone_asset_descriptor(bp):
            return bp

        from ue_bp_dsl.core import Blueprint

        if not isinstance(bp, Blueprint):
            raise TypeError(f"'bp' 类型错误: 期望 Blueprint，得到 {type(bp)}")

        used_graph_indexes: Set[int] = set()
        for fname in sorted(os.listdir(dir_path)):
            if not _is_graph_dsl_file(fname):
                continue

            graph_meta = _load_meta_dict(_graph_meta_path(dir_path, fname))
            graph_infos = _parse_graph_source_info(os.path.join(dir_path, fname))
            for info in graph_infos:
                _apply_graph_source_info(bp, info, graph_meta, used_graph_indexes)

        _augment_legacy_animgraph_node_props(bp, dir_path)
        return bp
    finally:
        sys.dont_write_bytecode = previous_dont_write_bytecode
        stale_keys = [key for key in sys.modules if key == package_name or key.startswith(package_name + ".")]
        for key in stale_keys:
            sys.modules.pop(key, None)
        _end_local_python_package_import("ue_bp_dsl", original_sys_path, saved_dsl_modules)


def _is_standalone_asset_descriptor(bp_obj: Any) -> bool:
    return isinstance(bp_obj, dict) and bp_obj.get("kind") == "standalone_asset"


def _import_standalone_asset_directory(
    dir_path: str,
    descriptor: Dict[str, Any],
    target_path: Optional[str] = None,
) -> Dict[str, Any]:
    import asset_importer

    _ = descriptor
    return asset_importer.import_asset_package(dir_path, target_path=target_path)


def _error_details(message: str) -> Dict[str, Any]:
    return {
        "success": False,
        "error": message,
        "asset_path": "",
        "import_mode": "bpy_directory",
        "compiled": False,
        "validation_ok": False,
        "validation_summary": {},
    }


def _augment_explicit_variable_type_metadata(payload: Dict[str, Any]) -> None:
    variables = payload.get("variables", [])
    if not isinstance(variables, list):
        return

    for variable in variables:
        if not isinstance(variable, dict):
            continue

        type_str = str(variable.get("type", "") or "")
        if not type_str or "|mapvalue=" not in type_str:
            continue

        for token in type_str.split("|")[1:]:
            lowered = token.lower()
            if lowered.startswith("mapvalue="):
                variable["map_value_type"] = token[len("mapvalue="):]
            elif lowered == "mapvalueconst":
                variable["map_value_const"] = True
            elif lowered == "mapvalueweak":
                variable["map_value_weak"] = True
            elif lowered == "mapvaluewrapper":
                variable["map_value_wrapper"] = True


def _sanitize_empty_container_assignments(text: Any) -> Any:
    if not isinstance(text, str) or not text.startswith("("):
        return text

    result: List[str] = []
    length = len(text)
    for index, char in enumerate(text):
        result.append(char)
        if char == "=" and (index + 1) < length and text[index + 1] in "),":
            result.extend(["(", ")"])
    return "".join(result)


def _sanitize_problematic_default_strings(payload: Dict[str, Any]) -> None:
    variables = payload.get("variables", [])
    if isinstance(variables, list):
        for variable in variables:
            if not isinstance(variable, dict):
                continue
            variable["default"] = _sanitize_empty_container_assignments(
                variable.get("default", "")
            )

    graphs = payload.get("graphs", [])
    if not isinstance(graphs, list):
        return

    for graph in graphs:
        if not isinstance(graph, dict):
            continue
        nodes = graph.get("nodes", [])
        if not isinstance(nodes, list):
            continue
        for node in nodes:
            if not isinstance(node, dict):
                continue
            defaults = node.get("defaults")
            if not isinstance(defaults, dict):
                continue
            for pin_name, value in list(defaults.items()):
                defaults[pin_name] = _sanitize_empty_container_assignments(value)


def _exec_file_dsl(py_path: str):
    """
    在隔离命名空间里执行单文件 DSL。
    """
    original_sys_path, saved_dsl_modules = _begin_local_python_package_import("ue_bp_dsl")
    try:
        from ue_bp_dsl.core import (
            Blueprint, float_track, vector_track, color_track,
            event_track, key, asset, soft_ref, class_ref, vec3, struct, enum,
        )

        ns: Dict[str, Any] = {
            "__file__": py_path,
            "__name__": "_bp_importer_exec",
            "Blueprint": Blueprint,
            "float_track": float_track,
            "vector_track": vector_track,
            "color_track": color_track,
            "event_track": event_track,
            "key": key,
            "asset": asset,
            "soft_ref": soft_ref,
            "class_ref": class_ref,
            "vec3": vec3,
            "struct": struct,
            "enum": enum,
        }

        with open(py_path, "r", encoding="utf-8") as handle:
            src = handle.read()

        exec(compile(src, py_path, "exec"), ns)  # noqa: S102

        bp = ns.get("bp")
        if bp is None:
            raise ValueError("脚本未定义顶层变量 'bp'（需要 bp = Blueprint(...)）")
        if not isinstance(bp, Blueprint):
            raise TypeError(f"'bp' 类型错误: 期望 Blueprint，得到 {type(bp)}")

        graph_infos = _parse_graph_source_info(py_path)
        meta = ns.get("META")
        used_graph_indexes: Set[int] = set()
        for info in graph_infos:
            _apply_graph_source_info(bp, info, meta, used_graph_indexes)

        _augment_legacy_animgraph_node_props(bp, py_path)
        return bp
    finally:
        _end_local_python_package_import("ue_bp_dsl", original_sys_path, saved_dsl_modules)


def _parse_graph_source_info(py_path: str) -> List[Dict[str, Any]]:
    with open(py_path, "r", encoding="utf-8") as handle:
        source = handle.read()

    tree = ast.parse(source, filename=py_path)
    infos: List[Dict[str, Any]] = []

    bodies: List[List[ast.stmt]] = [tree.body]
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name == "register":
            bodies.append(node.body)

    for body in bodies:
        for stmt in body:
            if not isinstance(stmt, ast.With):
                continue

            info = _parse_with_graph_info(stmt)
            if info is not None:
                infos.append(info)

    return infos


def _parse_with_graph_info(stmt: ast.With) -> Optional[Dict[str, Any]]:
    if not stmt.items:
        return None

    call = stmt.items[0].context_expr
    if not isinstance(call, ast.Call):
        return None
    if not isinstance(call.func, ast.Attribute):
        return None
    if not isinstance(call.func.value, ast.Name) or call.func.value.id != "bp":
        return None

    method = call.func.attr
    graph_type = {
        "event_graph": "event_graph",
        "function": "function",
        "macro": "macro",
    }.get(method)
    if graph_type is None:
        return None

    graph_name = _extract_string_arg(call)
    if not graph_name:
        return None

    node_names: List[str] = []
    for child in stmt.body:
        target_name = _extract_assigned_node_name(child)
        if target_name:
            node_names.append(target_name)

    return {
        "graph_name": graph_name,
        "graph_type": graph_type,
        "node_names": node_names,
    }


def _extract_string_arg(call: ast.Call) -> str:
    if call.args and isinstance(call.args[0], ast.Constant) and isinstance(call.args[0].value, str):
        return call.args[0].value

    for keyword in call.keywords:
        if keyword.arg == "name" and isinstance(keyword.value, ast.Constant) and isinstance(keyword.value.value, str):
            return keyword.value.value

    return ""


def _extract_assigned_node_name(stmt: ast.stmt) -> Optional[str]:
    if isinstance(stmt, ast.Assign) and len(stmt.targets) == 1 and isinstance(stmt.targets[0], ast.Name):
        target = stmt.targets[0].id
        value = stmt.value
    elif isinstance(stmt, ast.AnnAssign) and isinstance(stmt.target, ast.Name):
        target = stmt.target.id
        value = stmt.value
    else:
        return None

    if value is None or not isinstance(value, ast.Call):
        return None
    if not isinstance(value.func, ast.Attribute):
        return None
    if not isinstance(value.func.value, ast.Name) or value.func.value.id != "g":
        return None

    return target


def _apply_graph_source_info(
    bp: Any,
    info: Dict[str, Any],
    meta: Optional[Dict[str, Any]],
    used_graph_indexes: Set[int],
) -> None:
    graph = _find_graph(bp, info, used_graph_indexes)
    if graph is None:
        return

    node_names = info.get("node_names", [])
    for node, readable_name in zip(graph.nodes, node_names):
        node.readable_name = readable_name

    graph.metadata = meta if isinstance(meta, dict) else {}
    node_guid_map = graph.metadata.get("node_guid", {})
    node_pos_map = graph.metadata.get("node_pos", {})
    node_props_map = graph.metadata.get("node_props", {})
    pin_alias_map = graph.metadata.get("pin_alias", {})
    pin_id_map = graph.metadata.get("pin_id", {})
    input_pin_types_map = graph.metadata.get("input_pin_types", {})
    output_pin_types_map = graph.metadata.get("output_pin_types", {})

    node_by_name = {
        node.readable_name: node
        for node in graph.nodes
        if getattr(node, "readable_name", "")
    }
    auto_position_nodes: List[Tuple[int, str, Any]] = []
    for index, node in enumerate(graph.nodes):
        readable_name = getattr(node, "readable_name", "")
        if not readable_name:
            readable_name = f"__node_{index}"

        if readable_name in node_guid_map:
            node.node_guid = str(node_guid_map[readable_name])
        elif not getattr(node, "node_guid", ""):
            node.node_guid = _make_fallback_node_guid(info, readable_name, index)

        if readable_name in node_pos_map:
            pos_value = node_pos_map[readable_name]
            if isinstance(pos_value, (list, tuple)) and len(pos_value) >= 2:
                node.pos_x = pos_value[0]
                node.pos_y = pos_value[1]
        elif _should_auto_place_node(node):
            auto_position_nodes.append((index, readable_name, node))

        if readable_name in node_props_map and isinstance(node_props_map[readable_name], dict):
            node.extra_props.update(node_props_map[readable_name])

    _apply_fallback_positions(graph, auto_position_nodes)

    for alias_key, full_pin_name in pin_alias_map.items():
        if "." not in alias_key:
            continue
        readable_name, pin_name = alias_key.split(".", 1)
        node = node_by_name.get(readable_name)
        if node is not None:
            alias_value = str(full_pin_name)
            for pin_variant in _exec_pin_variants(pin_name):
                node.pin_aliases[pin_variant] = alias_value

    for pin_key, pin_id in pin_id_map.items():
        if "." not in pin_key:
            continue
        readable_name, pin_name = pin_key.split(".", 1)
        node = node_by_name.get(readable_name)
        if node is not None:
            pin_id_value = str(pin_id)
            for pin_variant in _exec_pin_variants(pin_name):
                node.pin_ids[pin_variant] = pin_id_value

    for pin_key, pin_type in input_pin_types_map.items():
        if "." not in pin_key:
            continue
        readable_name, pin_name = pin_key.split(".", 1)
        node = node_by_name.get(readable_name)
        if node is not None:
            pin_type_value = str(pin_type)
            for pin_variant in _exec_pin_variants(pin_name):
                node.input_pin_types[pin_variant] = pin_type_value

    for pin_key, pin_type in output_pin_types_map.items():
        if "." not in pin_key:
            continue
        readable_name, pin_name = pin_key.split(".", 1)
        node = node_by_name.get(readable_name)
        if node is not None:
            pin_type_value = str(pin_type)
            for pin_variant in _exec_pin_variants(pin_name):
                node.output_pin_types[pin_variant] = pin_type_value


_MISSING_NODE_RE = re.compile(
    r"Connection references missing node\(s\):\s*([0-9a-fA-F-]+)\s*->\s*([0-9a-fA-F-]+)"
)


def _describe_missing_connection_nodes(payload: Dict[str, Any], error_text: str) -> str:
    match = _MISSING_NODE_RE.search(str(error_text or ""))
    if not match or not isinstance(payload, dict):
        return str(error_text or "")

    missing_ids = {match.group(1), match.group(2)}
    described: List[str] = []
    graphs = payload.get("graphs", [])
    if isinstance(graphs, list):
        for graph in graphs:
            if not isinstance(graph, dict):
                continue
            graph_name = str(graph.get("name", "") or "")
            for node in graph.get("nodes", []) if isinstance(graph.get("nodes"), list) else []:
                if not isinstance(node, dict):
                    continue
                node_uid = str(node.get("uid", "") or "")
                if node_uid not in missing_ids:
                    continue
                readable_name = str(node.get("readable_name", "") or "")
                node_class = str(node.get("node_class", "") or "")
                function_ref = str(node.get("function_ref", "") or "")
                member_name = str(node.get("member_name", "") or "")
                described.append(
                    f"{node_uid} [{graph_name}] {readable_name or '<unnamed>'} "
                    f"class={node_class} function={function_ref or '-'} member={member_name or '-'}"
                )

    if not described:
        return str(error_text or "")
    return f"{error_text} | Missing node details: {'; '.join(described)}"


def _make_fallback_node_guid(info: Dict[str, Any], readable_name: str, index: int) -> str:
    graph_type = str(info.get("graph_type", ""))
    graph_name = str(info.get("graph_name", ""))
    seed = f"exportbpy:{graph_type}:{graph_name}:{readable_name}:{index}"
    return uuid.uuid5(uuid.NAMESPACE_URL, seed).hex.upper()


def _should_auto_place_node(node: Any) -> bool:
    return float(getattr(node, "pos_x", 0.0) or 0.0) == 0.0 and float(getattr(node, "pos_y", 0.0) or 0.0) == 0.0


def _apply_fallback_positions(graph: Any, auto_position_nodes: List[Tuple[int, str, Any]]) -> None:
    if not auto_position_nodes:
        return

    auto_position_node_ids = {id(item[2]) for item in auto_position_nodes}
    placed_nodes = [
        node
        for node in getattr(graph, "nodes", [])
        if id(node) not in auto_position_node_ids
    ]
    placed_x = [float(getattr(node, "pos_x", 0.0) or 0.0) for node in placed_nodes]
    placed_y = [float(getattr(node, "pos_y", 0.0) or 0.0) for node in placed_nodes]

    base_x = (max(placed_x) + 384.0) if placed_x else -3200.0
    base_y = min(placed_y) if placed_y else -768.0
    row_stride = 224.0
    column_stride = 384.0
    max_rows = 6

    for offset, (_, _, node) in enumerate(auto_position_nodes):
        column = offset // max_rows
        row = offset % max_rows
        node.pos_x = base_x + column * column_stride
        node.pos_y = base_y + row * row_stride


def _find_graph(bp: Any, info: Dict[str, Any], used_graph_indexes: Set[int]):
    graph_name = info.get("graph_name", "")
    graph_type = info.get("graph_type", "")

    for index, graph in enumerate(getattr(bp, "_graphs", [])):
        if index in used_graph_indexes:
            continue
        if getattr(graph, "name", "") != graph_name:
            continue
        if graph_type and getattr(graph, "graph_type", "") != graph_type:
            continue

        used_graph_indexes.add(index)
        return graph

    return None


def _normalize_bridge_blueprint_path(asset_path: str) -> str:
    if not isinstance(asset_path, str) or not asset_path.startswith("/"):
        return asset_path

    leaf = asset_path.rsplit("/", 1)[-1]
    if "." not in leaf:
        return asset_path

    asset_name, object_name = leaf.split(".", 1)
    if asset_name != object_name:
        return asset_path

    head = asset_path[: -len(leaf)]
    return f"{head}{asset_name}"


def _camel_to_snake(name: str) -> str:
    if not name:
        return ""
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()


def _component_name_candidates(name: str) -> Set[str]:
    raw_name = str(name or "")
    candidates = {raw_name}
    stripped = re.sub(r"_[0-9A-Fa-f-]+$", "", raw_name)
    if stripped:
        candidates.add(stripped)

    aliases = {
        "CollisionCylinder": ("CapsuleComponent",),
        "CapsuleComponent": ("CollisionCylinder",),
        "CharMoveComp": ("CharacterMovement",),
        "CharacterMovement": ("CharMoveComp",),
        "CharacterMesh0": ("Mesh", "CharacterMesh"),
        "CharacterMesh": ("Mesh", "CharacterMesh0"),
        "Mesh": ("CharacterMesh0", "CharacterMesh"),
    }
    for candidate in list(candidates):
        for alias in aliases.get(candidate, ()):
            candidates.add(alias)

    return {candidate.lower() for candidate in candidates if candidate}


def _component_name_matches(expected: str, actual: str) -> bool:
    if not expected or not actual:
        return False
    return bool(_component_name_candidates(expected).intersection(_component_name_candidates(actual)))


def _get_generated_class(blueprint: Any):
    if blueprint is None:
        return None

    try:
        generated_class = blueprint.generated_class()
    except Exception:
        generated_class = None
    if generated_class is not None:
        return generated_class

    try:
        generated_class = getattr(blueprint, "generated_class")
    except Exception:
        generated_class = None
    return generated_class


def _get_default_object(target: Any):
    if not _HAS_UNREAL or target is None:
        return None
    try:
        return unreal.get_default_object(target)
    except Exception:
        return None


def _iter_actor_components(actor: Any) -> List[Any]:
    if actor is None:
        return []
    components: List[Any] = []
    try:
        actor.get_components(components)
        return list(components)
    except Exception:
        return []


def _find_component_on_actor(actor: Any, component_name: str):
    for component in _iter_actor_components(actor):
        actual_names: List[str] = []
        try:
            actual_names.append(str(component.get_name() or ""))
        except Exception:
            pass
        try:
            actual_names.append(str(component.get_fname() or ""))
        except Exception:
            pass
        for actual_name in actual_names:
            if _component_name_matches(component_name, actual_name):
                return component
    return None


def _iter_blueprint_component_nodes(blueprint: Any) -> List[Any]:
    if blueprint is None:
        return []

    try:
        scs = getattr(blueprint, "simple_construction_script", None)
    except Exception:
        scs = None
    if scs is None:
        try:
            scs = blueprint.get_editor_property("simple_construction_script")
        except Exception:
            scs = None
    if scs is None:
        return []

    get_all_nodes = getattr(scs, "get_all_nodes", None)
    if callable(get_all_nodes):
        try:
            return list(get_all_nodes() or [])
        except Exception:
            return []
    return []


def _get_component_node_name(node: Any) -> str:
    if node is None:
        return ""

    try:
        variable_name = node.get_editor_property("variable_name")
        text = str(variable_name or "")
        if text:
            return text
    except Exception:
        pass

    try:
        get_variable_name = getattr(node, "get_variable_name", None)
        if callable(get_variable_name):
            text = str(get_variable_name() or "")
            if text:
                return text
    except Exception:
        pass

    try:
        component_template = node.get_editor_property("component_template")
    except Exception:
        component_template = None
    if component_template is not None:
        try:
            return str(component_template.get_name() or "")
        except Exception:
            return ""

    return ""


def _find_component_node_on_blueprint(blueprint: Any, component_name: str):
    for node in _iter_blueprint_component_nodes(blueprint):
        actual_name = _get_component_node_name(node)
        if _component_name_matches(component_name, actual_name):
            return node
    return None


def _get_component_parent_name(component: Any) -> str:
    if component is None or not hasattr(component, "get_attach_parent"):
        return ""
    try:
        parent = component.get_attach_parent()
    except Exception:
        parent = None
    if parent is None:
        return ""
    try:
        return str(parent.get_name() or "")
    except Exception:
        return ""


def _get_component_attach_socket_name(component: Any) -> str:
    if component is None or not hasattr(component, "get_attach_socket_name"):
        return ""
    try:
        return str(component.get_attach_socket_name() or "")
    except Exception:
        return ""


def _normalize_compare_value(value: Any) -> Any:
    object_path = _extract_compare_object_path(value)
    if object_path:
        return object_path
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        return float(value)
    if value is None:
        return ""
    return str(value)


def _extract_compare_object_path(value: Any) -> str:
    if value is None:
        return ""

    try:
        get_path_name = getattr(value, "get_path_name", None)
        if callable(get_path_name):
            path_name = str(get_path_name() or "").strip()
            if path_name.startswith("/"):
                return path_name
    except Exception:
        pass

    if not isinstance(value, str):
        return ""

    text = value.strip()
    if not text:
        return ""

    if text.startswith("/"):
        object_ref_match = re.search(r"'(/[^']+)'", text)
        if object_ref_match:
            return object_ref_match.group(1)
        return text

    object_repr_match = re.search(r"'(/[^']+)'", text)
    if object_repr_match:
        return object_repr_match.group(1)

    return ""


def _values_equivalent(expected: Any, actual: Any) -> bool:
    normalized_expected = _normalize_compare_value(expected)
    normalized_actual = _normalize_compare_value(actual)
    if isinstance(normalized_expected, float) and isinstance(normalized_actual, float):
        return math.isclose(normalized_expected, normalized_actual, rel_tol=1e-6, abs_tol=1e-6)
    return normalized_expected == normalized_actual


def _read_editor_property_flex(target: Any, property_name: str) -> Tuple[bool, Any]:
    if target is None or not property_name:
        return False, None

    names_to_try: List[str] = []
    for candidate in (property_name, _camel_to_snake(property_name)):
        if candidate and candidate not in names_to_try:
            names_to_try.append(candidate)

    for candidate in names_to_try:
        try:
            return True, target.get_editor_property(candidate)
        except Exception:
            pass
        try:
            return True, getattr(target, candidate)
        except Exception:
            pass

    return False, None


def _normalize_mobility_value(value: Any) -> str:
    text = str(value or "")
    if "." in text:
        text = text.split(".")[-1]
    return text.strip().lower()


def _normalize_function_name_text(value: Any) -> str:
    text = str(value or "").strip()
    if not text:
        return ""
    if text.lower() == "none":
        return ""
    return text


def _normalize_missing_graph_name(value: Any) -> str:
    if isinstance(value, dict):
        for key in ("graph", "name", "graph_name"):
            text = str(value.get(key, "") or "").strip()
            if text:
                return text
    return str(value or "").strip()


def _is_known_equivalent_default_mismatch(entry: Any) -> bool:
    if not isinstance(entry, dict):
        return False

    graph_name = str(entry.get("graph", "") or "").strip()
    node_class = str(entry.get("node_class", "") or "").strip()
    pin_name = str(entry.get("pin", "") or "").strip()
    expected = str(entry.get("expected", "") or "").strip().strip('"')
    actual = str(entry.get("actual", "") or "").strip().strip('"')

    if graph_name != "AnimGraph":
        return False
    if node_class != "AnimGraphNode_PoseSearchHistoryCollector":
        return False
    if pin_name != "TransformTrajectory":
        return False
    return expected == "(Samples=())" and actual == "(Samples=)"


def _collect_expected_import_stats(payload: Dict[str, Any]) -> Dict[str, Any]:
    delegate_node_classes = (
        "K2Node_CreateDelegate",
        "K2Node_AssignDelegate",
        "K2Node_AddDelegate",
        "K2Node_CallDelegate",
        "K2Node_RemoveDelegate",
    )
    summary: Dict[str, Any] = {
        "expected_graph_count": 0,
        "expected_function_count": 0,
        "expected_total_node_count": 0,
        "expected_graphs": [],
        "expected_graph_node_counts": {},
        "expected_function_names": [],
        "expected_node_class_counts": {},
        "expected_create_delegate_bindings": [],
        "expected_delegate_node_counts": {name: 0 for name in delegate_node_classes},
        "expected_delegate_nodes": [],
        "expected_delegate_connections": [],
        "warnings": [],
    }

    if not isinstance(payload, dict):
        summary["warnings"].append("payload is not a dict")
        return summary

    graphs = payload.get("graphs", [])
    if not isinstance(graphs, list):
        summary["warnings"].append("payload.graphs is missing or not a list")
        return summary

    for graph_index, graph in enumerate(graphs):
        if not isinstance(graph, dict):
            continue

        graph_name = str(graph.get("name", "") or "").strip() or f"<unnamed:{graph_index}>"
        graph_type = str(graph.get("graph_type", "") or "").strip().lower()
        raw_nodes = graph.get("nodes", [])
        nodes = raw_nodes if isinstance(raw_nodes, list) else []
        if not isinstance(raw_nodes, list):
            summary["warnings"].append(f"{graph_name}: nodes field is not a list")

        graph_entry = {
            "name": graph_name,
            "graph_type": graph_type,
            "node_count": len(nodes),
        }
        summary["expected_graphs"].append(graph_entry)
        summary["expected_graph_node_counts"][graph_name] = len(nodes)
        summary["expected_graph_count"] += 1
        summary["expected_total_node_count"] += len(nodes)

        if graph_type == "function":
            summary["expected_function_count"] += 1
            summary["expected_function_names"].append(graph_name)

        node_uid_to_info: Dict[str, Dict[str, Any]] = {}
        for node in nodes:
            if not isinstance(node, dict):
                continue

            node_class = str(node.get("node_class", "") or "")
            if node_class:
                summary["expected_node_class_counts"][node_class] = (
                    int(summary["expected_node_class_counts"].get(node_class, 0)) + 1
                )

            node_uid = str(node.get("uid", "") or "").strip()
            node_guid = str(node.get("node_guid", "") or "").strip()
            node_readable_name = str(node.get("readable_name", "") or "").strip()
            node_member_name = _normalize_function_name_text(node.get("member_name"))
            node_props = node.get("node_props", {})
            if not isinstance(node_props, dict):
                node_props = {}

            selected_function = _normalize_function_name_text(node_props.get("SelectedFunctionName"))
            delegate_reference = str(node_props.get("DelegateReference", "") or "").strip()
            if not selected_function:
                selected_function = node_member_name

            if node_uid:
                node_uid_to_info[node_uid] = {
                    "uid": node_uid,
                    "node_guid": node_guid,
                    "node_class": node_class,
                    "readable_name": node_readable_name,
                    "member_name": node_member_name,
                    "selected_function": selected_function,
                    "delegate_reference": delegate_reference,
                }

            if node_class not in delegate_node_classes:
                continue

            summary["expected_delegate_node_counts"][node_class] = (
                int(summary["expected_delegate_node_counts"].get(node_class, 0)) + 1
            )
            summary["expected_delegate_nodes"].append(
                {
                    "graph_name": graph_name,
                    "uid": node_uid,
                    "node_guid": node_guid,
                    "node_class": node_class,
                    "readable_name": node_readable_name,
                    "member_name": node_member_name,
                    "selected_function": selected_function,
                    "delegate_reference": delegate_reference,
                }
            )

            if node_class != "K2Node_CreateDelegate":
                continue

            delegate_info = {
                "graph_name": graph_name,
                "node_guid": node_guid,
                "readable_name": node_readable_name,
                "expected_function": selected_function,
            }
            summary["expected_create_delegate_bindings"].append(delegate_info)
            if not selected_function:
                summary["warnings"].append(
                    f"{graph_name}:{delegate_info['readable_name'] or '<CreateDelegate>'} missing expected function name"
                )

        raw_connections = graph.get("connections", [])
        connections = raw_connections if isinstance(raw_connections, list) else []
        if not isinstance(raw_connections, list):
            summary["warnings"].append(f"{graph_name}: connections field is not a list")
        for connection in connections:
            if not isinstance(connection, dict):
                continue
            src_uid = str(connection.get("src_node", "") or "").strip()
            dst_uid = str(connection.get("dst_node", "") or "").strip()
            src_pin = str(connection.get("src_pin", "") or "").strip()
            dst_pin = str(connection.get("dst_pin", "") or "").strip()
            if src_pin != "OutputDelegate" or dst_pin != "Delegate":
                continue

            src_info = node_uid_to_info.get(src_uid, {})
            dst_info = node_uid_to_info.get(dst_uid, {})
            summary["expected_delegate_connections"].append(
                {
                    "graph_name": graph_name,
                    "src_uid": src_uid,
                    "dst_uid": dst_uid,
                    "src_node_guid": str(src_info.get("node_guid", "") or ""),
                    "dst_node_guid": str(dst_info.get("node_guid", "") or ""),
                    "src_node_class": str(src_info.get("node_class", "") or ""),
                    "dst_node_class": str(dst_info.get("node_class", "") or ""),
                    "src_readable_name": str(src_info.get("readable_name", "") or ""),
                    "dst_readable_name": str(dst_info.get("readable_name", "") or ""),
                }
            )

    summary["expected_function_names"] = sorted(
        {
            str(name or "").strip()
            for name in summary["expected_function_names"]
            if str(name or "").strip()
        }
    )
    return summary


def _read_blueprint_payload_via_exporter(asset_path: str) -> Tuple[Optional[Dict[str, Any]], str]:
    if not _HAS_UNREAL:
        return None, "Unreal bridge unavailable"

    exporter = getattr(unreal, "BPDirectExporter", None)
    if exporter is None:
        return None, "BPDirectExporter is unavailable"

    export_method = None
    for method_name in ("read_blueprint_to_json", "ReadBlueprintToJson"):
        method = getattr(exporter, method_name, None)
        if callable(method):
            export_method = method
            break
    if export_method is None:
        return None, "BPDirectExporter.ReadBlueprintToJson is unavailable"

    candidates = [asset_path, _normalize_bridge_blueprint_path(asset_path)]
    seen_candidates: Set[str] = set()
    for candidate in candidates:
        text_candidate = str(candidate or "").strip()
        if not text_candidate or text_candidate in seen_candidates:
            continue
        seen_candidates.add(text_candidate)
        try:
            json_text = export_method(text_candidate)
        except Exception:
            json_text = ""
        json_text = str(json_text or "").strip()
        if not json_text:
            continue
        try:
            payload = json.loads(json_text)
        except Exception:
            continue
        if isinstance(payload, dict):
            return payload, ""

    return None, f"failed to export imported blueprint to json: {asset_path}"


def _get_live_function_graph_names(blueprint: Any) -> List[str]:
    if blueprint is None:
        return []

    names: List[str] = []
    seen: Set[str] = set()

    def _append_name(value: Any) -> None:
        name_text = str(value or "").strip()
        if not name_text:
            return
        key = name_text.lower()
        if key in seen:
            return
        seen.add(key)
        names.append(name_text)

    found, function_graphs = _read_editor_property_flex(blueprint, "function_graphs")
    if found and function_graphs is not None:
        try:
            iterable = list(function_graphs)
        except Exception:
            iterable = []
        for graph in iterable:
            _append_name(_safe_graph_name(graph))

    library = getattr(unreal, "BlueprintEditorLibrary", None) if _HAS_UNREAL else None
    if library is not None and hasattr(library, "get_function_graphs"):
        try:
            iterable = list(library.get_function_graphs(blueprint) or [])
        except Exception:
            iterable = []
        for graph in iterable:
            _append_name(_safe_graph_name(graph))

    return names


def _read_live_create_delegate_function_name(node: Any) -> str:
    if node is None:
        return ""

    for method_name in ("get_function_name", "GetFunctionName"):
        method = getattr(node, method_name, None)
        if callable(method):
            try:
                value = method()
            except Exception:
                value = None
            text = _normalize_function_name_text(value)
            if text:
                return text

    for property_name in (
        "selected_function_name",
        "SelectedFunctionName",
        "function_name",
        "FunctionName",
    ):
        found, value = _read_editor_property_flex(node, property_name)
        if not found:
            continue
        text = _normalize_function_name_text(value)
        if text:
            return text

    return ""


def _validate_imported_blueprint(
    asset_path: str,
    payload: Dict[str, Any],
    preflight_stats: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    summary: Dict[str, Any] = {
        "ok": True,
        "preflight_stats": preflight_stats if isinstance(preflight_stats, dict) else {},
        "missing_graphs": [],
        "graph_node_count_mismatches": [],
        "function_count_mismatch": {},
        "missing_functions": [],
        "unexpected_functions": [],
        "create_delegate_mismatches": [],
        "delegate_node_count_mismatches": [],
        "delegate_decl_mismatches": [],
        "delegate_connection_mismatches": [],
        "missing_components": [],
        "component_parent_mismatches": [],
        "component_socket_mismatches": [],
        "inherited_component_mobility_mismatches": [],
        "class_default_mismatches": [],
        "missing_default_keys": [],
        "default_mismatches": [],
        "default_compare_mode": "cpp_bridge_strict",
        "warnings": [],
    }

    if not _HAS_UNREAL or not isinstance(payload, dict):
        return summary

    if not isinstance(summary["preflight_stats"], dict) or not summary["preflight_stats"]:
        summary["preflight_stats"] = _collect_expected_import_stats(payload)

    summary["optional_accessor_function_graphs"] = []

    blueprint = _load_blueprint_asset_for_repair(asset_path)
    generated_class = _get_generated_class(blueprint)
    cdo = _get_default_object(generated_class)
    parent_class = getattr(blueprint, "parent_class", None) if blueprint is not None else None
    parent_cdo = _get_default_object(parent_class)

    if blueprint is None or cdo is None:
        summary["warnings"].append(f"Unable to load imported blueprint for validation: {asset_path}")
        summary["ok"] = False
        return summary

    expected_stats = summary["preflight_stats"]
    live_payload, live_payload_error = _read_blueprint_payload_via_exporter(asset_path)
    live_stats: Dict[str, Any] = {}
    if live_payload is None:
        summary["warnings"].append(live_payload_error)
        summary["missing_graphs"].append("<unable to read imported blueprint json>")
    else:
        live_stats = _collect_expected_import_stats(live_payload)
    summary["live_stats"] = live_stats

    bridge_validation_payload: Dict[str, Any] = {}
    try:
        source_json_text = json.dumps(payload, ensure_ascii=False)
    except Exception as exc:
        source_json_text = ""
        summary["warnings"].append(f"Failed to serialize payload for default validation: {exc}")

    if source_json_text:
        bridge_validation_payload, bridge_validation_error = _validate_imported_defaults_with_bridge(
            source_json_text,
            _normalize_bridge_blueprint_path(asset_path),
        )
        if bridge_validation_error:
            summary["warnings"].append(bridge_validation_error)
        if isinstance(bridge_validation_payload, dict):
            missing_default_keys = bridge_validation_payload.get("missing_default_keys", [])
            default_mismatches = bridge_validation_payload.get("default_mismatches", [])
            if isinstance(missing_default_keys, list):
                summary["missing_default_keys"] = missing_default_keys
            if isinstance(default_mismatches, list):
                summary["default_mismatches"] = [
                    mismatch
                    for mismatch in default_mismatches
                    if not _is_known_equivalent_default_mismatch(mismatch)
                ]
            for key in ("missing_graphs",):
                value = bridge_validation_payload.get(key)
                if isinstance(value, list) and value:
                    existing = summary.get(key, [])
                    if isinstance(existing, list):
                        if key == "missing_graphs":
                            filtered: List[str] = []
                            for item in value:
                                item_text = _normalize_missing_graph_name(item)
                                if not item_text:
                                    continue
                                filtered.append(item_text)
                            summary[key] = existing + filtered
                        else:
                            summary[key] = existing + value

    expected_graph_node_counts = expected_stats.get("expected_graph_node_counts", {})
    live_graph_node_counts = live_stats.get("expected_graph_node_counts", {})
    if not isinstance(expected_graph_node_counts, dict):
        expected_graph_node_counts = {}
    if not isinstance(live_graph_node_counts, dict):
        live_graph_node_counts = {}

    for graph_name, expected_node_count_raw in expected_graph_node_counts.items():
        graph_name_text = str(graph_name or "").strip()
        if not graph_name_text:
            continue

        if graph_name_text not in live_graph_node_counts:
            summary["missing_graphs"].append(graph_name_text)
            continue

        expected_node_count = int(expected_node_count_raw or 0)
        actual_node_count = int(live_graph_node_counts.get(graph_name_text, 0) or 0)
        if actual_node_count != expected_node_count:
            summary["graph_node_count_mismatches"].append(
                {
                    "graph": graph_name_text,
                    "expected_nodes": expected_node_count,
                    "actual_nodes": actual_node_count,
                }
            )

    expected_function_names = expected_stats.get("expected_function_names", [])
    live_function_names = live_stats.get("expected_function_names", [])
    if not isinstance(expected_function_names, list):
        expected_function_names = []
    if not isinstance(live_function_names, list):
        live_function_names = []

    expected_function_name_set = {
        str(name or "").strip()
        for name in expected_function_names
        if str(name or "").strip()
    }
    required_function_name_set = expected_function_name_set
    live_function_name_set = {
        str(name or "").strip()
        for name in live_function_names
        if str(name or "").strip()
    }

    expected_function_count = len(required_function_name_set)
    actual_function_count = len(required_function_name_set.intersection(live_function_name_set))
    if actual_function_count != expected_function_count:
        summary["function_count_mismatch"] = {
            "expected": expected_function_count,
            "actual": actual_function_count,
            "actual_total": len(live_function_name_set),
        }

    summary["missing_functions"] = sorted(
        required_function_name_set.difference(live_function_name_set)
    )
    summary["unexpected_functions"] = sorted(
        live_function_name_set.difference(expected_function_name_set)
    )
    summary["missing_graphs"] = sorted(
        {
            _normalize_missing_graph_name(item)
            for item in summary.get("missing_graphs", [])
            if _normalize_missing_graph_name(item)
        }
    )

    expected_delegate_bindings = expected_stats.get("expected_create_delegate_bindings", [])
    live_delegate_bindings = live_stats.get("expected_create_delegate_bindings", [])
    if not isinstance(expected_delegate_bindings, list):
        expected_delegate_bindings = []
    if not isinstance(live_delegate_bindings, list):
        live_delegate_bindings = []

    live_delegate_by_guid: Dict[str, Dict[str, Any]] = {}
    live_delegate_by_name: Dict[Tuple[str, str], List[Dict[str, Any]]] = {}
    for live_binding in live_delegate_bindings:
        if not isinstance(live_binding, dict):
            continue
        live_graph_name = str(live_binding.get("graph_name", "") or "").strip()
        live_readable_name = str(live_binding.get("readable_name", "") or "").strip()
        live_guid = str(live_binding.get("node_guid", "") or "").strip()
        if live_guid:
            live_delegate_by_guid[live_guid] = live_binding
        key = (live_graph_name, live_readable_name.lower())
        live_delegate_by_name.setdefault(key, []).append(live_binding)

    for expected_binding in expected_delegate_bindings:
        if not isinstance(expected_binding, dict):
            continue
        expected_function = _normalize_function_name_text(expected_binding.get("expected_function"))
        if not expected_function:
            continue

        expected_graph_name = str(expected_binding.get("graph_name", "") or "").strip()
        expected_guid = str(expected_binding.get("node_guid", "") or "").strip()
        expected_readable_name = str(expected_binding.get("readable_name", "") or "").strip()

        live_binding = None
        if expected_guid:
            live_binding = live_delegate_by_guid.get(expected_guid)
        if live_binding is None:
            fallback_key = (expected_graph_name, expected_readable_name.lower())
            candidates = live_delegate_by_name.get(fallback_key, [])
            if candidates:
                live_binding = candidates[0]

        if live_binding is None:
            summary["create_delegate_mismatches"].append(
                {
                    "graph": expected_graph_name,
                    "node": expected_readable_name or "<CreateDelegate>",
                    "expected_function": expected_function,
                    "actual_function": "<missing node>",
                }
            )
            continue

        actual_function = _normalize_function_name_text(live_binding.get("expected_function"))
        if actual_function != expected_function:
            summary["create_delegate_mismatches"].append(
                {
                    "graph": expected_graph_name,
                    "node": expected_readable_name or "<CreateDelegate>",
                    "expected_function": expected_function,
                    "actual_function": actual_function or "None",
                }
            )

    expected_delegate_counts = expected_stats.get("expected_delegate_node_counts", {})
    live_delegate_counts = live_stats.get("expected_delegate_node_counts", {})
    if not isinstance(expected_delegate_counts, dict):
        expected_delegate_counts = {}
    if not isinstance(live_delegate_counts, dict):
        live_delegate_counts = {}
    for delegate_class in (
        "K2Node_CreateDelegate",
        "K2Node_AssignDelegate",
        "K2Node_AddDelegate",
        "K2Node_CallDelegate",
        "K2Node_RemoveDelegate",
    ):
        expected_count = int(expected_delegate_counts.get(delegate_class, 0) or 0)
        actual_count = int(live_delegate_counts.get(delegate_class, 0) or 0)
        if expected_count != actual_count:
            summary["delegate_node_count_mismatches"].append(
                {
                    "node_class": delegate_class,
                    "expected": expected_count,
                    "actual": actual_count,
                }
            )

    expected_delegate_nodes = expected_stats.get("expected_delegate_nodes", [])
    live_delegate_nodes = live_stats.get("expected_delegate_nodes", [])
    if not isinstance(expected_delegate_nodes, list):
        expected_delegate_nodes = []
    if not isinstance(live_delegate_nodes, list):
        live_delegate_nodes = []

    live_delegate_nodes_by_guid: Dict[str, Dict[str, Any]] = {}
    live_delegate_nodes_by_fallback: Dict[Tuple[str, str, str], List[Dict[str, Any]]] = {}
    for live_node in live_delegate_nodes:
        if not isinstance(live_node, dict):
            continue
        live_guid = str(live_node.get("node_guid", "") or "").strip()
        live_graph = str(live_node.get("graph_name", "") or "").strip()
        live_class = str(live_node.get("node_class", "") or "").strip()
        live_name = str(live_node.get("readable_name", "") or "").strip().lower()
        if live_guid:
            live_delegate_nodes_by_guid[live_guid] = live_node
        fallback_key = (live_graph, live_class, live_name)
        live_delegate_nodes_by_fallback.setdefault(fallback_key, []).append(live_node)

    for expected_node in expected_delegate_nodes:
        if not isinstance(expected_node, dict):
            continue
        expected_graph = str(expected_node.get("graph_name", "") or "").strip()
        expected_class = str(expected_node.get("node_class", "") or "").strip()
        expected_name = str(expected_node.get("readable_name", "") or "").strip()
        expected_guid = str(expected_node.get("node_guid", "") or "").strip()
        expected_member = _normalize_function_name_text(expected_node.get("member_name"))
        expected_selected = _normalize_function_name_text(expected_node.get("selected_function"))
        expected_ref = str(expected_node.get("delegate_reference", "") or "").strip()

        live_node = None
        if expected_guid:
            live_node = live_delegate_nodes_by_guid.get(expected_guid)
        if live_node is None:
            fallback_key = (expected_graph, expected_class, expected_name.lower())
            candidates = live_delegate_nodes_by_fallback.get(fallback_key, [])
            if candidates:
                live_node = candidates[0]

        if live_node is None:
            summary["delegate_decl_mismatches"].append(
                {
                    "graph": expected_graph,
                    "node": expected_name or "<delegate>",
                    "reason": "missing delegate node",
                }
            )
            continue

        actual_member = _normalize_function_name_text(live_node.get("member_name"))
        actual_selected = _normalize_function_name_text(live_node.get("selected_function"))
        actual_ref = str(live_node.get("delegate_reference", "") or "").strip()

        mismatch_reasons: List[str] = []
        if expected_member != actual_member:
            mismatch_reasons.append(f"member_name expected={expected_member or 'None'} actual={actual_member or 'None'}")
        if expected_selected != actual_selected:
            mismatch_reasons.append(
                f"selected_function expected={expected_selected or 'None'} actual={actual_selected or 'None'}"
            )
        if expected_ref != actual_ref:
            mismatch_reasons.append(
                f"delegate_reference expected={expected_ref or 'None'} actual={actual_ref or 'None'}"
            )

        if mismatch_reasons:
            summary["delegate_decl_mismatches"].append(
                {
                    "graph": expected_graph,
                    "node": expected_name or "<delegate>",
                    "reason": "; ".join(mismatch_reasons),
                }
            )

    expected_delegate_connections = expected_stats.get("expected_delegate_connections", [])
    live_delegate_connections = live_stats.get("expected_delegate_connections", [])
    if not isinstance(expected_delegate_connections, list):
        expected_delegate_connections = []
    if not isinstance(live_delegate_connections, list):
        live_delegate_connections = []

    def _delegate_edge_key(edge: Dict[str, Any]) -> Tuple[str, str, str]:
        graph_name = str(edge.get("graph_name", "") or "").strip()
        src_guid = str(edge.get("src_node_guid", "") or "").strip()
        dst_guid = str(edge.get("dst_node_guid", "") or "").strip()
        if src_guid and dst_guid:
            return (graph_name, src_guid, dst_guid)
        src_name = str(edge.get("src_readable_name", "") or "").strip()
        dst_name = str(edge.get("dst_readable_name", "") or "").strip()
        return (graph_name, src_name, dst_name)

    expected_edge_keys = {_delegate_edge_key(edge) for edge in expected_delegate_connections if isinstance(edge, dict)}
    live_edge_keys = {_delegate_edge_key(edge) for edge in live_delegate_connections if isinstance(edge, dict)}
    missing_delegate_edges = sorted(expected_edge_keys.difference(live_edge_keys))
    extra_delegate_edges = sorted(live_edge_keys.difference(expected_edge_keys))
    if missing_delegate_edges or extra_delegate_edges:
        summary["delegate_connection_mismatches"].append(
            {
                "missing_edges": missing_delegate_edges,
                "unexpected_edges": extra_delegate_edges,
            }
        )

    components = payload.get("components", [])
    component_validation_available = bool(_iter_actor_components(cdo)) or bool(
        _iter_blueprint_component_nodes(blueprint)
    )
    if isinstance(components, list) and not component_validation_available:
        summary["warnings"].append(
            "Component validation unavailable in current Unreal Python context; skipped component checks"
        )
    elif isinstance(components, list):
        for component_data in components:
            if not isinstance(component_data, dict):
                continue

            component_name = str(component_data.get("name", "") or "")
            if not component_name:
                continue

            live_component = _find_component_on_actor(cdo, component_name)
            component_node = None
            if live_component is None:
                component_node = _find_component_node_on_blueprint(blueprint, component_name)
            if live_component is None and component_node is None:
                summary["missing_components"].append(component_name)
                continue

            expected_parent = str(component_data.get("parent", "") or "")
            if expected_parent and live_component is not None:
                actual_parent = _get_component_parent_name(live_component)
                if not _component_name_matches(expected_parent, actual_parent):
                    summary["component_parent_mismatches"].append(
                        {
                            "component": component_name,
                            "expected_parent": expected_parent,
                            "actual_parent": actual_parent,
                        }
                    )

            expected_socket = str(component_data.get("attach_to_name", "") or "")
            if expected_socket and live_component is not None:
                actual_socket = _get_component_attach_socket_name(live_component)
                if actual_socket != expected_socket:
                    summary["component_socket_mismatches"].append(
                        {
                            "component": component_name,
                            "expected_socket": expected_socket,
                            "actual_socket": actual_socket,
                        }
                    )

    inherited_components = payload.get("inherited_components", [])
    if isinstance(inherited_components, list) and parent_cdo is not None:
        for component_data in inherited_components:
            if not isinstance(component_data, dict):
                continue

            component_name = str(component_data.get("name", "") or "")
            properties = component_data.get("properties", {})
            if not component_name or not isinstance(properties, dict) or "Mobility" in properties:
                continue

            target_component = _find_component_on_actor(cdo, component_name)
            parent_component = _find_component_on_actor(parent_cdo, component_name)
            if target_component is None or parent_component is None:
                continue

            target_ok, target_mobility = _read_editor_property_flex(target_component, "Mobility")
            parent_ok, parent_mobility = _read_editor_property_flex(parent_component, "Mobility")
            if not target_ok or not parent_ok:
                continue

            normalized_target = _normalize_mobility_value(target_mobility)
            normalized_parent = _normalize_mobility_value(parent_mobility)
            if normalized_target and normalized_parent and normalized_target != normalized_parent:
                summary["inherited_component_mobility_mismatches"].append(
                    {
                        "component": component_name,
                        "expected_mobility": normalized_parent,
                        "actual_mobility": normalized_target,
                    }
                )

    class_defaults = payload.get("class_defaults", [])
    if isinstance(class_defaults, list):
        for default_entry in class_defaults:
            if not isinstance(default_entry, dict):
                continue

            property_name = str(default_entry.get("name", "") or "")
            if not property_name:
                continue

            expected_value = default_entry.get("value")
            if not isinstance(expected_value, (bool, int, float, str)):
                continue

            found, actual_value = _read_editor_property_flex(cdo, property_name)
            if not found:
                continue
            if not _values_equivalent(expected_value, actual_value):
                summary["class_default_mismatches"].append(
                    {
                        "property": property_name,
                        "expected": expected_value,
                        "actual": actual_value,
                    }
                )

    summary["ok"] = not any(
        summary[key]
        for key in (
            "missing_graphs",
            "graph_node_count_mismatches",
            "function_count_mismatch",
            "missing_functions",
            "unexpected_functions",
            "create_delegate_mismatches",
            "delegate_node_count_mismatches",
            "delegate_decl_mismatches",
            "delegate_connection_mismatches",
            "missing_components",
            "component_parent_mismatches",
            "component_socket_mismatches",
            "inherited_component_mobility_mismatches",
            "class_default_mismatches",
            "missing_default_keys",
            "default_mismatches",
        )
    )

    if not summary["ok"]:
        try:
            unreal.log_warning("[ExportBpy] Import validation reported structural mismatches")
            unreal.log_warning(json.dumps(summary, ensure_ascii=False))
        except Exception:
            pass

    return summary


def _load_blueprint_asset_for_repair(asset_path: str):
    if not _HAS_UNREAL:
        return None

    candidates = [asset_path, _normalize_bridge_blueprint_path(asset_path)]
    tried: Set[str] = set()
    for candidate in candidates:
        if not candidate or candidate in tried:
            continue
        tried.add(candidate)

        for loader_name in ("load_asset",):
            loader = getattr(unreal, loader_name, None)
            if loader is None:
                continue
            try:
                loaded = loader(candidate)
            except Exception:
                loaded = None
            if loaded is not None:
                return loaded

        editor_asset_library = getattr(unreal, "EditorAssetLibrary", None)
        if editor_asset_library is not None and hasattr(editor_asset_library, "load_asset"):
            try:
                loaded = editor_asset_library.load_asset(candidate)
            except Exception:
                loaded = None
            if loaded is not None:
                return loaded

    return None


def _coerce_guid_component_int(value: Any) -> Optional[int]:
    if value is None:
        return None
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        try:
            return int(value)
        except Exception:
            return None

    text = str(value).strip()
    if not text:
        return None
    try:
        return int(text, 0)
    except Exception:
        return None


def _extract_guid_components(value: Any) -> Optional[Tuple[int, int, int, int]]:
    if value is None:
        return None

    component_values: List[int] = []
    for component_name in ("a", "b", "c", "d"):
        component = None

        if hasattr(value, "get_editor_property"):
            try:
                component = value.get_editor_property(component_name)
            except Exception:
                component = None
            if component is None:
                try:
                    component = value.get_editor_property(component_name.upper())
                except Exception:
                    component = None

        if component is None:
            try:
                component = getattr(value, component_name)
            except Exception:
                component = None
        if component is None:
            try:
                component = getattr(value, component_name.upper())
            except Exception:
                component = None

        coerced = _coerce_guid_component_int(component)
        if coerced is None:
            return None
        component_values.append(coerced)

    if len(component_values) != 4:
        return None
    return tuple(component_values)  # type: ignore[return-value]


def _normalize_guid_text(value: Any) -> str:
    if value is None:
        return ""

    direct_components = _extract_guid_components(value)
    if direct_components is not None:
        return "".join(f"{(component & 0xFFFFFFFF):08X}" for component in direct_components)

    text_candidate = None
    for method_name in ("to_string", "ToString"):
        if not hasattr(value, method_name):
            continue
        try:
            method = getattr(value, method_name)
            produced = method() if callable(method) else None
        except Exception:
            produced = None
        if produced:
            text_candidate = str(produced).strip()
            if text_candidate:
                break

    text = text_candidate if text_candidate else str(value).strip()
    if not text:
        return ""

    component_matches = re.findall(
        r"([AaBbCcDd])\s*=\s*(0x[0-9A-Fa-f]+|-?\d+)",
        text,
    )
    if component_matches:
        component_map: Dict[str, int] = {}
        for key, raw_value in component_matches:
            try:
                component_map[key.upper()] = int(raw_value, 0)
            except Exception:
                component_map = {}
                break
        if all(name in component_map for name in ("A", "B", "C", "D")):
            return "".join(
                f"{(component_map[name] & 0xFFFFFFFF):08X}"
                for name in ("A", "B", "C", "D")
            )

    stripped = text.replace("{", "").replace("}", "").replace("-", "").upper()
    if re.fullmatch(r"[0-9A-F]{32}", stripped or ""):
        return stripped

    hex_run = re.search(r"[0-9A-Fa-f]{32}", stripped)
    if hex_run:
        return hex_run.group(0).upper()

    return ""


def _looks_like_graph_object(candidate: Any) -> bool:
    if candidate is None:
        return False

    class_name = ""
    try:
        node_class = candidate.get_class()
    except Exception:
        node_class = None
    if node_class is not None:
        try:
            class_name = str(node_class.get_name() or "")
        except Exception:
            class_name = ""
    if not class_name:
        try:
            class_name = str(type(candidate).__name__ or "")
        except Exception:
            class_name = ""

    lowered_class = class_name.lower()
    if "graph" in lowered_class:
        return True

    if hasattr(candidate, "get_nodes") or hasattr(candidate, "nodes"):
        return True

    if hasattr(candidate, "get_editor_property"):
        for property_name in ("nodes", "Nodes", "schema", "Schema"):
            try:
                value = candidate.get_editor_property(property_name)
            except Exception:
                value = None
            if value is not None:
                return True

    return False


def _unwrap_graph_candidate(candidate: Any) -> Any:
    if candidate is None:
        return None

    if isinstance(candidate, (list, tuple)):
        for item in candidate:
            unwrapped = _unwrap_graph_candidate(item)
            if unwrapped is not None:
                return unwrapped
        return None

    if isinstance(candidate, dict):
        for key in ("graph", "result", "value", "return_value", "out_graph"):
            if key in candidate:
                unwrapped = _unwrap_graph_candidate(candidate.get(key))
                if unwrapped is not None:
                    return unwrapped
        for value in candidate.values():
            unwrapped = _unwrap_graph_candidate(value)
            if unwrapped is not None:
                return unwrapped
        return None

    if _looks_like_graph_object(candidate):
        return candidate
    return None


def _safe_graph_name(graph: Any) -> str:
    if graph is None:
        return ""

    for property_name in ("graph_name", "GraphName"):
        try:
            value = graph.get_editor_property(property_name)
        except Exception:
            value = None
        if value:
            return str(value)

    if hasattr(graph, "get_name"):
        try:
            value = graph.get_name()
        except Exception:
            value = ""
        if value:
            return str(value)

    return ""


def _normalize_graph_name(value: Any) -> str:
    text = str(value or "").strip().lower()
    if "." in text:
        text = text.rsplit(".", 1)[-1]
    return text


def _iter_blueprint_graphs(blueprint: Any) -> List[Any]:
    if blueprint is None:
        return []

    candidates: List[Any] = []
    seen_ids: Set[int] = set()

    def _append_graph(value: Any) -> None:
        if value is None:
            return
        if isinstance(value, (list, tuple, set)):
            for item in value:
                _append_graph(item)
            return

        graph = _unwrap_graph_candidate(value)
        if graph is None:
            return

        key = id(graph)
        if key in seen_ids:
            return
        seen_ids.add(key)
        candidates.append(graph)

    library = getattr(unreal, "BlueprintEditorLibrary", None)
    if library is not None:
        for method_name in (
            "get_all_graphs",
            "get_uber_graph_pages",
            "get_function_graphs",
            "get_macro_graphs",
        ):
            if not hasattr(library, method_name):
                continue
            method = getattr(library, method_name, None)
            if method is None:
                continue
            try:
                _append_graph(method(blueprint))
            except Exception:
                pass

    if hasattr(blueprint, "get_editor_property"):
        for property_name in (
            "ubergraph_pages",
            "function_graphs",
            "macro_graphs",
            "delegate_signature_graphs",
            "all_graphs",
            "event_graph",
        ):
            try:
                value = blueprint.get_editor_property(property_name)
            except Exception:
                value = None
            _append_graph(value)

    return candidates


def _find_live_graph(blueprint: Any, graph_name: str):
    library = getattr(unreal, "BlueprintEditorLibrary", None)
    if blueprint is None or not graph_name:
        return None

    target_name = _normalize_graph_name(graph_name)

    if library is not None and target_name == "eventgraph" and hasattr(library, "find_event_graph"):
        try:
            graph = library.find_event_graph(blueprint)
        except Exception:
            graph = None
        unwrapped = _unwrap_graph_candidate(graph)
        if unwrapped is not None:
            return unwrapped

    if library is not None and hasattr(library, "find_graph"):
        try:
            graph = library.find_graph(blueprint, graph_name)
        except Exception:
            graph = None
        unwrapped = _unwrap_graph_candidate(graph)
        if unwrapped is not None:
            return unwrapped

    if target_name == "eventgraph":
        for graph in _iter_blueprint_graphs(blueprint):
            name = _normalize_graph_name(_safe_graph_name(graph))
            if name == target_name:
                return graph

    for graph in _iter_blueprint_graphs(blueprint):
        name = _normalize_graph_name(_safe_graph_name(graph))
        if name == target_name:
            return graph

    return None


def _get_live_graph_nodes(graph: Any) -> List[Any]:
    live_graph = _unwrap_graph_candidate(graph)
    if live_graph is None:
        return []

    library = getattr(unreal, "BlueprintEditorLibrary", None)
    if library is not None and hasattr(library, "get_graph_nodes"):
        try:
            nodes = library.get_graph_nodes(live_graph)
        except Exception:
            nodes = None
        if nodes is not None:
            try:
                node_list = list(nodes)
            except Exception:
                node_list = []
            if node_list:
                return node_list

    if hasattr(live_graph, "get_nodes"):
        try:
            nodes = live_graph.get_nodes()
        except Exception:
            nodes = None
        if nodes is not None:
            try:
                node_list = list(nodes)
            except Exception:
                node_list = []
            if node_list:
                return node_list

    for property_name in ("nodes", "Nodes"):
        try:
            nodes = live_graph.get_editor_property(property_name)
        except Exception:
            nodes = None
        if nodes is not None:
            try:
                node_list = list(nodes)
            except Exception:
                node_list = []
            if node_list:
                return node_list

    for attribute_name in ("nodes", "Nodes"):
        try:
            nodes = getattr(live_graph, attribute_name)
        except Exception:
            nodes = None
        if nodes is not None:
            try:
                node_list = list(nodes)
            except Exception:
                node_list = []
            if node_list:
                return node_list

    return []


def _find_live_node_by_guid(graph: Any, node_guid: str):
    wanted = _normalize_guid_text(node_guid)
    if not wanted:
        return None

    for node in _get_live_graph_nodes(graph):
        current_guid = ""
        for property_name in ("node_guid", "NodeGuid"):
            try:
                current_guid = node.get_editor_property(property_name)
                break
            except Exception:
                current_guid = ""
        if not current_guid:
            for attribute_name in ("node_guid", "NodeGuid"):
                try:
                    current_guid = getattr(node, attribute_name)
                    if current_guid:
                        break
                except Exception:
                    current_guid = ""
        if _normalize_guid_text(current_guid) == wanted:
            return node

    return None


def _safe_int_position(value: Any) -> Optional[int]:
    try:
        return int(round(float(value)))
    except Exception:
        return None


def _get_live_node_class_name(node: Any) -> str:
    try:
        node_class = node.get_class()
    except Exception:
        node_class = None
    if node_class is not None:
        try:
            return str(node_class.get_name() or "")
        except Exception:
            pass
    try:
        return str(type(node).__name__)
    except Exception:
        return ""


def _get_live_node_position(node: Any) -> Tuple[Optional[int], Optional[int]]:
    for name in ("node_pos_x", "NodePosX"):
        try:
            pos_x = _safe_int_position(node.get_editor_property(name))
            break
        except Exception:
            pos_x = None
        try:
            pos_x = _safe_int_position(getattr(node, name))
            break
        except Exception:
            pos_x = None

    for name in ("node_pos_y", "NodePosY"):
        try:
            pos_y = _safe_int_position(node.get_editor_property(name))
            break
        except Exception:
            pos_y = None
        try:
            pos_y = _safe_int_position(getattr(node, name))
            break
        except Exception:
            pos_y = None

    return pos_x, pos_y


def _find_live_node_by_fallback(
    graph: Any,
    source_node: Dict[str, Any],
    defaults: Dict[str, Any],
    pin_aliases: Optional[Dict[str, Any]] = None,
):
    expected_class = str(source_node.get("node_class", "") or "")
    expected_x = _safe_int_position(source_node.get("pos_x"))
    expected_y = _safe_int_position(source_node.get("pos_y"))
    expected_pin_candidates: List[List[str]] = []
    seen_pin_groups: Set[str] = set()
    for pin_name in defaults.keys():
        raw_name = str(pin_name).strip()
        if not raw_name:
            continue

        candidates: List[str] = []
        seen_candidates: Set[str] = set()

        def _add_candidate(name: Any) -> None:
            text = str(name or "").strip()
            if not text:
                return
            key = text.lower()
            if key in seen_candidates:
                return
            seen_candidates.add(key)
            candidates.append(text)

        _add_candidate(raw_name)
        if isinstance(pin_aliases, dict):
            _add_candidate(pin_aliases.get(raw_name))
            for alias_key, alias_value in pin_aliases.items():
                if str(alias_key or "").strip().lower() == raw_name.lower():
                    _add_candidate(alias_value)

        if not candidates:
            continue

        group_key = "|".join(name.lower() for name in candidates)
        if group_key in seen_pin_groups:
            continue
        seen_pin_groups.add(group_key)
        expected_pin_candidates.append(candidates)

    position_tolerance = 64

    if not expected_class:
        return None

    candidates: List[Tuple[int, Any]] = []
    for live_node in _get_live_graph_nodes(graph):
        if _get_live_node_class_name(live_node) != expected_class:
            continue

        if expected_pin_candidates:
            missing_pin = False
            for pin_candidates in expected_pin_candidates:
                pin_found = False
                for pin_name in pin_candidates:
                    try:
                        pin = live_node.find_pin(pin_name)
                    except Exception:
                        pin = None
                    if pin is not None:
                        pin_found = True
                        break
                if not pin_found:
                    missing_pin = True
                    break
            if missing_pin:
                continue

        score = 0
        if expected_x is not None and expected_y is not None:
            live_x, live_y = _get_live_node_position(live_node)
            if live_x is None or live_y is None:
                continue
            delta_x = abs(live_x - expected_x)
            delta_y = abs(live_y - expected_y)
            if delta_x > position_tolerance or delta_y > position_tolerance:
                continue
            score = delta_x + delta_y

        candidates.append((score, live_node))

    if len(candidates) == 1:
        return candidates[0][1]

    if not candidates:
        return None

    candidates.sort(key=lambda item: item[0])
    best_score = candidates[0][0]
    best_matches = [node for score, node in candidates if score == best_score]
    if len(best_matches) == 1:
        return best_matches[0]

    return None


def _resolve_default_object_for_pin(value: Any):
    if not isinstance(value, str) or not value.startswith("/"):
        return None

    load_object = getattr(unreal, "load_object", None)
    if load_object is not None:
        try:
            loaded = load_object(None, value)
        except Exception:
            loaded = None
        if loaded is not None:
            return loaded

    load_class = getattr(unreal, "load_class", None)
    if load_class is not None:
        try:
            loaded = load_class(None, value)
        except Exception:
            loaded = None
        if loaded is not None:
            return loaded

    load_asset = getattr(unreal, "load_asset", None)
    if load_asset is not None:
        try:
            loaded = load_asset(value)
        except Exception:
            loaded = None
        if loaded is not None:
            return loaded

    editor_asset_library = getattr(unreal, "EditorAssetLibrary", None)
    if editor_asset_library is not None and hasattr(editor_asset_library, "load_asset"):
        try:
            loaded = editor_asset_library.load_asset(value)
        except Exception:
            loaded = None
        if loaded is not None:
            return loaded

    return None


def _stringify_pin_default(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value)


def _iter_live_node_pins(node: Any) -> List[Any]:
    if node is None:
        return []

    for property_name in ("pins", "Pins"):
        try:
            pins = node.get_editor_property(property_name)
        except Exception:
            pins = None
        if pins is not None:
            try:
                return list(pins)
            except Exception:
                pass

    for attribute_name in ("pins", "Pins"):
        try:
            pins = getattr(node, attribute_name)
        except Exception:
            pins = None
        if pins is not None:
            try:
                return list(pins)
            except Exception:
                pass

    return []


def _get_live_pin_guid_text(pin: Any) -> str:
    if pin is None:
        return ""

    for property_name in ("pin_id", "PinId"):
        try:
            value = pin.get_editor_property(property_name)
        except Exception:
            value = None
        normalized = _normalize_guid_text(value)
        if normalized:
            return normalized

    for attribute_name in ("pin_id", "PinId"):
        try:
            value = getattr(pin, attribute_name)
        except Exception:
            value = None
        normalized = _normalize_guid_text(value)
        if normalized:
            return normalized

    return ""


def _resolve_live_pin(
    node: Any,
    pin_name: str,
    pin_aliases: Optional[Dict[str, Any]] = None,
    pin_ids: Optional[Dict[str, Any]] = None,
) -> Tuple[Optional[Any], str]:
    if node is None or not hasattr(node, "find_pin"):
        return None, "Node does not support pin lookup"

    requested = str(pin_name or "").strip()
    if not requested:
        return None, "Pin name is empty"

    candidate_names: List[str] = []
    seen_names: Set[str] = set()

    def _add_candidate(name: Any) -> None:
        text = str(name or "").strip()
        if not text:
            return
        key = text.lower()
        if key in seen_names:
            return
        seen_names.add(key)
        candidate_names.append(text)

    _add_candidate(requested)

    if isinstance(pin_aliases, dict):
        alias_value = pin_aliases.get(requested)
        _add_candidate(alias_value)
        for key, value in pin_aliases.items():
            if str(key or "").strip().lower() == requested.lower():
                _add_candidate(value)

    for candidate in candidate_names:
        try:
            pin = node.find_pin(candidate)
        except Exception:
            pin = None
        if pin is not None:
            return pin, ""

    if isinstance(pin_ids, dict):
        requested_pin_id = pin_ids.get(requested)
        if requested_pin_id is None:
            for key, value in pin_ids.items():
                if str(key or "").strip().lower() == requested.lower():
                    requested_pin_id = value
                    break

        wanted_pin_guid = _normalize_guid_text(requested_pin_id)
        if wanted_pin_guid:
            for live_pin in _iter_live_node_pins(node):
                if _get_live_pin_guid_text(live_pin) == wanted_pin_guid:
                    return live_pin, ""

    attempted = ", ".join(candidate_names) if candidate_names else requested
    return None, f"Pin not found: {requested} (tried: {attempted})"


def _set_live_pin_default(
    node: Any,
    pin_name: str,
    value: Any,
    pin_aliases: Optional[Dict[str, Any]] = None,
    pin_ids: Optional[Dict[str, Any]] = None,
) -> Tuple[bool, str]:
    if node is None or not hasattr(node, "find_pin"):
        return False, f"Node does not support pin lookup: {pin_name}"

    pin, resolve_error = _resolve_live_pin(node, pin_name, pin_aliases=pin_aliases, pin_ids=pin_ids)
    if pin is None:
        return False, resolve_error or f"Pin not found: {pin_name}"

    if hasattr(node, "modify"):
        try:
            node.modify()
        except Exception:
            pass

    try:
        schema = pin.get_schema() if hasattr(pin, "get_schema") else None
    except Exception:
        schema = None

    default_object = _resolve_default_object_for_pin(value)
    try:
        if default_object is not None:
            applied = False
            if schema is not None and hasattr(schema, "try_set_default_object"):
                try:
                    schema_result = schema.try_set_default_object(pin, default_object, False)
                    applied = True if schema_result is None else bool(schema_result)
                except Exception:
                    applied = False
            if not applied:
                pin.default_object = default_object
                pin.default_value = default_object.get_path_name()
            return True, ""

        default_value = _stringify_pin_default(value)
        applied = False
        if schema is not None and hasattr(schema, "try_set_default_value"):
            try:
                schema_result = schema.try_set_default_value(pin, default_value, False)
                applied = True if schema_result is None else bool(schema_result)
            except Exception:
                applied = False
        if not applied:
            pin.default_value = default_value

        # Prevent autogenerated defaults from re-overriding explicit imported values.
        for property_name in ("autogenerated_default_value", "AutogeneratedDefaultValue"):
            try:
                pin.set_editor_property(property_name, "")
                break
            except Exception:
                try:
                    setattr(pin, property_name, "")
                    break
                except Exception:
                    pass
        return True, ""
    except Exception as exc:
        return False, str(exc)


def _log_repair_warning(message: str) -> None:
    if not _HAS_UNREAL:
        return
    try:
        unreal.log_warning(f"[ExportBpy] {message}")
    except Exception:
        pass


def _is_force_blend_callsite_node(node_payload: Dict[str, Any], live_node: Any) -> bool:
    try:
        source_class = str(node_payload.get("node_class", "") or "")
    except Exception:
        source_class = ""
    if source_class and source_class != "K2Node_CallFunction":
        return False

    for pin_name in ("ForceBlend", "StateMachineState"):
        try:
            if live_node.find_pin(pin_name) is None:
                return False
        except Exception:
            return False
    return True


def _repair_imported_blueprint_pin_defaults(
    asset_path: str,
    payload: Dict[str, Any],
    stage_label: str = "repair",
) -> Tuple[bool, str]:
    graphs = payload.get("graphs", [])
    if not isinstance(graphs, list):
        return True, ""

    blueprint = _load_blueprint_asset_for_repair(asset_path)
    if blueprint is None:
        return False, f"Unable to load blueprint for pin repair: {asset_path}"

    failures: List[str] = []
    skipped_nodes = 0
    visited_graphs = 0
    nodes_with_defaults = 0
    force_blend_attempts = 0
    force_blend_applied = 0
    force_blend_failures: List[str] = []
    force_blend_skipped_nodes: List[str] = []
    graph_debug_samples: List[str] = []
    for graph in graphs:
        if not isinstance(graph, dict):
            continue

        strand_name = str(graph.get("name", "") or "")
        graph_nodes = graph.get("nodes", [])
        if not strand_name or not isinstance(graph_nodes, list):
            continue
        visited_graphs += 1

        live_graph = _find_live_graph(blueprint, strand_name)
        if live_graph is None:
            failures.append(f"{strand_name}: graph not found after import")
            continue
        if len(graph_debug_samples) < 5:
            try:
                graph_type_name = type(live_graph).__name__
            except Exception:
                graph_type_name = "UnknownType"
            try:
                graph_runtime_name = live_graph.get_name() if hasattr(live_graph, "get_name") else ""
            except Exception:
                graph_runtime_name = ""
            live_graph_node_count = len(_get_live_graph_nodes(live_graph))
            graph_debug_samples.append(
                f"{strand_name}=>{graph_runtime_name or '<no-name>'} "
                f"type={graph_type_name} nodes={live_graph_node_count}"
            )

        for node in graph_nodes:
            if not isinstance(node, dict):
                continue

            node_guid = str(node.get("node_guid", "") or "").strip()
            defaults = node.get("defaults")
            if not node_guid or not isinstance(defaults, dict) or not defaults:
                continue
            nodes_with_defaults += 1

            pin_aliases = node.get("pin_aliases")
            if not isinstance(pin_aliases, dict):
                pin_aliases = {}
            pin_ids = node.get("pin_ids")
            if not isinstance(pin_ids, dict):
                pin_ids = {}

            live_node = _find_live_node_by_guid(live_graph, node_guid)
            if live_node is None:
                live_node = _find_live_node_by_fallback(
                    live_graph,
                    node,
                    defaults,
                    pin_aliases=pin_aliases,
                )
                if live_node is None:
                    skipped_nodes += 1
                    if "ForceBlend" in defaults:
                        force_blend_skipped_nodes.append(f"{strand_name}:{node_guid}")
                    continue

            for pin_name, value in defaults.items():
                normalized_pin_name = str(pin_name).strip()
                if not normalized_pin_name:
                    continue

                is_force_blend_target = (
                    normalized_pin_name.lower() == "forceblend"
                    and _is_force_blend_callsite_node(node, live_node)
                )
                if is_force_blend_target:
                    force_blend_attempts += 1

                ok, error = _set_live_pin_default(
                    live_node,
                    normalized_pin_name,
                    value,
                    pin_aliases=pin_aliases,
                    pin_ids=pin_ids,
                )
                if not ok:
                    if is_force_blend_target:
                        force_blend_failures.append(
                            f"{strand_name}:{getattr(live_node, 'get_name', lambda: 'UnknownNode')()}"
                        )
                    _log_repair_warning(
                        f"Pin repair skipped in {strand_name}: "
                        f"{getattr(live_node, 'get_name', lambda: 'UnknownNode')()}.{normalized_pin_name}: {error}"
                    )
                elif is_force_blend_target:
                    force_blend_applied += 1

    _log_repair_warning(
        f"Pin repair [{stage_label}] summary: graphs={visited_graphs}, "
        f"nodes_with_defaults={nodes_with_defaults}, skipped_nodes={skipped_nodes}, "
        f"force_blend_attempts={force_blend_attempts}, force_blend_applied={force_blend_applied}"
    )
    if graph_debug_samples:
        _log_repair_warning(
            f"Pin repair [{stage_label}] graph samples: {'; '.join(graph_debug_samples)}"
        )
    if force_blend_skipped_nodes:
        sample = ", ".join(force_blend_skipped_nodes[:5])
        _log_repair_warning(f"Pin repair [{stage_label}] ForceBlend skipped samples: {sample}")

    if force_blend_attempts:
        _log_repair_warning(
            f"Pin repair [{stage_label}] ForceBlend attempts={force_blend_attempts}, "
            f"applied={force_blend_applied}, failed={force_blend_attempts - force_blend_applied}"
        )
        if force_blend_failures:
            sample = ", ".join(force_blend_failures[:3])
            _log_repair_warning(f"Pin repair [{stage_label}] ForceBlend failed samples: {sample}")

    return (len(failures) == 0), " | ".join(failures)


def _compile_blueprint_with_bridge(asset_path: str) -> Tuple[bool, str]:
    blueprint = _load_blueprint_asset_for_repair(asset_path)
    if blueprint is None:
        return False, f"Unable to load blueprint for compile: {asset_path}"

    library = getattr(unreal, "BlueprintEditorLibrary", None)
    if library is None or not hasattr(library, "compile_blueprint"):
        return False, "BlueprintEditorLibrary.compile_blueprint is unavailable"

    try:
        library.compile_blueprint(blueprint)
    except Exception as exc:
        return False, str(exc)

    try:
        status = str(blueprint.get_editor_property("status"))
    except Exception:
        status = ""
    if "ERROR" in status.upper():
        return False, f"Blueprint compile reported status {status}"

    return True, ""


def _save_asset_if_possible(asset_path: str) -> bool:
    if not _HAS_UNREAL or not hasattr(unreal, "EditorAssetLibrary"):
        return False

    try:
        return bool(unreal.EditorAssetLibrary.save_asset(asset_path, False))
    except Exception:
        return False


def _restore_create_delegate_bindings_with_bridge(json_str: str, asset_path: str) -> Tuple[bool, str]:
    try:
        import unreal

        if (
            hasattr(unreal, "BPDirectImporter")
            and hasattr(unreal.BPDirectImporter, "restore_create_delegates_from_json_detailed")
        ):
            result = unreal.BPDirectImporter.restore_create_delegates_from_json_detailed(
                json_str,
                asset_path,
            )
        elif hasattr(unreal, "call_function"):
            result = unreal.call_function(
                "BPDirectImporter",
                "RestoreCreateDelegatesFromJsonDetailed",
                json_str,
                asset_path,
            )
        else:
            return False, (
                "Unreal Python bridge cannot call "
                "BPDirectImporter.RestoreCreateDelegatesFromJsonDetailed"
            )

        success: Optional[bool] = None
        error_text = ""
        if isinstance(result, str):
            parsed = _parse_import_result_json(result)
            if parsed is not None:
                return parsed
        if isinstance(result, tuple):
            if result:
                first = result[0]
                if isinstance(first, bool):
                    success = first
                elif first is not None:
                    success = bool(first)
            if len(result) > 1 and result[1] is not None:
                error_text = str(result[1])
        elif isinstance(result, str):
            success = True
            error_text = result
        elif isinstance(result, bool):
            success = result
        elif result is None:
            success = False
        else:
            success = bool(result)

        if success is None:
            return False, error_text or "C++ delegate restore did not return a success flag"

        if not success:
            return False, error_text or "C++ delegate restore failed without an error message"

        return True, error_text
    except Exception as exc:
        return False, str(exc)


def _validate_imported_defaults_with_bridge(
    json_str: str,
    asset_path: str,
) -> Tuple[Dict[str, Any], str]:
    try:
        import unreal

        if (
            hasattr(unreal, "BPDirectImporter")
            and hasattr(unreal.BPDirectImporter, "validate_imported_blueprint_against_json_detailed")
        ):
            result = unreal.BPDirectImporter.validate_imported_blueprint_against_json_detailed(
                json_str,
                asset_path,
            )
        elif hasattr(unreal, "call_function"):
            result = unreal.call_function(
                "BPDirectImporter",
                "ValidateImportedBlueprintAgainstJsonDetailed",
                json_str,
                asset_path,
            )
        else:
            return {}, (
                "Unreal Python bridge cannot call "
                "BPDirectImporter.ValidateImportedBlueprintAgainstJsonDetailed"
            )

        if isinstance(result, str):
            try:
                payload = json.loads(result)
            except Exception:
                payload = None
            if isinstance(payload, dict):
                error_text = str(payload.get("error", "") or "")
                return payload, error_text
            return {}, "C++ default validation did not return a JSON payload"

        if isinstance(result, tuple):
            if result:
                success = bool(result[0])
                error_text = str(result[1]) if len(result) > 1 and result[1] is not None else ""
                return {"success": success}, error_text
            return {}, "C++ default validation returned an empty tuple"

        if isinstance(result, bool):
            return {"success": bool(result)}, ""

        return {}, "C++ default validation returned an unsupported payload type"
    except Exception as exc:
        return {}, str(exc)


def _call_cpp_importer(json_str: str, asset_path: str, compile_blueprint: bool = True) -> Tuple[bool, str]:
    try:
        import unreal

        if hasattr(unreal, "BPDirectImporter") and hasattr(unreal.BPDirectImporter, "import_blueprint_from_json_detailed"):
            result = unreal.BPDirectImporter.import_blueprint_from_json_detailed(
                json_str, asset_path, compile_blueprint
            )
        elif hasattr(unreal, "call_function"):
            result = unreal.call_function(
                "BPDirectImporter", "ImportBlueprintFromJson", json_str, asset_path, compile_blueprint
            )
        elif hasattr(unreal, "BPDirectImporter"):
            result = unreal.BPDirectImporter.import_blueprint_from_json(
                json_str, asset_path, compile_blueprint
            )
        else:
            return False, "Unreal Python bridge cannot call BPDirectImporter.ImportBlueprintFromJson"

        success: Optional[bool] = None
        error_text = ""
        if isinstance(result, str):
            parsed = _parse_import_result_json(result)
            if parsed is not None:
                return parsed
        if isinstance(result, tuple):
            if result:
                first = result[0]
                if isinstance(first, bool):
                    success = first
                elif first is not None:
                    success = bool(first)
            if len(result) > 1 and result[1] is not None:
                error_text = str(result[1])
        elif isinstance(result, str):
            # On UE 5.7 the Python binding for `bool Foo(..., FString& OutError)`
            # can collapse to only the out string: success -> "", failure -> None.
            success = True
            error_text = result
        elif isinstance(result, bool):
            success = result
        elif result is None:
            success = False
        else:
            success = bool(result)

        if success is None:
            return False, error_text or "C++ importer did not return a success flag"

        if success is False:
            return False, error_text or "C++ importer returned failure without an error message"

        if hasattr(unreal, "EditorAssetLibrary") and not unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            return False, error_text or f"C++ importer reported success but asset does not exist: {asset_path}"

        return success, error_text
    except Exception as exc:
        return False, str(exc)


def _parse_import_result_json(result_text: str) -> Optional[Tuple[bool, str]]:
    try:
        payload = json.loads(result_text)
    except Exception:
        return None
    if not isinstance(payload, dict) or "success" not in payload:
        return None
    return bool(payload.get("success", False)), str(payload.get("error", ""))
