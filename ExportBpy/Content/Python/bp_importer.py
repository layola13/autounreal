# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-
"""
bp_importer.py — 从 Python DSL 导入蓝图到 Unreal

支持两种输入：
  1. 目录模式（推荐）：目录内包含 __bp__.bp.py + evt_/fn_/macro_/tl_*.bp.py + *_meta.py
  2. 单文件模式：单个低层 .bp.py 文件
"""

from __future__ import annotations

import ast
import importlib.util
import json
import os
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

    if os.path.basename(py_path) == MAIN_BP_FILE:
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
        }

    ok, err = _call_cpp_importer(json_str, asset_path, compile_blueprint=compile_blueprint)
    return {
        "success": ok,
        "error": err,
        "asset_path": asset_path,
        "import_mode": "bpy_directory",
        "compiled": bool(ok and compile_blueprint),
    }


def _exec_directory_dsl(dir_path: str):
    main_path = os.path.join(dir_path, MAIN_BP_FILE)
    if not os.path.isfile(main_path):
        raise FileNotFoundError(f"目录缺少 {MAIN_BP_FILE}: {dir_path}")

    package_name = f"_exportbpy_pkg_{uuid.uuid4().hex}"
    spec = importlib.util.spec_from_file_location(package_name, main_path)
    if spec is None or spec.loader is None:
        raise ImportError(f"无法为 {main_path} 创建 import spec")

    module = importlib.util.module_from_spec(spec)
    sys.modules[package_name] = module

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

        return bp
    finally:
        stale_keys = [key for key in sys.modules if key == package_name or key.startswith(package_name + ".")]
        for key in stale_keys:
            sys.modules.pop(key, None)


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
    }


def _exec_file_dsl(py_path: str):
    """
    在隔离命名空间里执行单文件 DSL。
    """
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

    return bp


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

    if not isinstance(meta, dict):
        return

    graph.metadata = meta
    node_guid_map = meta.get("node_guid", {})
    node_pos_map = meta.get("node_pos", {})
    node_props_map = meta.get("node_props", {})
    pin_alias_map = meta.get("pin_alias", {})
    pin_id_map = meta.get("pin_id", {})

    node_by_name = {
        node.readable_name: node
        for node in graph.nodes
        if getattr(node, "readable_name", "")
    }

    for readable_name, node in node_by_name.items():
        if readable_name in node_guid_map:
            node.node_guid = str(node_guid_map[readable_name])

        if readable_name in node_pos_map:
            pos_value = node_pos_map[readable_name]
            if isinstance(pos_value, (list, tuple)) and len(pos_value) >= 2:
                node.pos_x = pos_value[0]
                node.pos_y = pos_value[1]

        if readable_name in node_props_map and isinstance(node_props_map[readable_name], dict):
            node.extra_props.update(node_props_map[readable_name])

    for alias_key, full_pin_name in pin_alias_map.items():
        if "." not in alias_key:
            continue
        readable_name, pin_name = alias_key.split(".", 1)
        node = node_by_name.get(readable_name)
        if node is not None:
            node.pin_aliases[pin_name] = str(full_pin_name)

    for pin_key, pin_id in pin_id_map.items():
        if "." not in pin_key:
            continue
        readable_name, pin_name = pin_key.split(".", 1)
        node = node_by_name.get(readable_name)
        if node is not None:
            node.pin_ids[pin_name] = str(pin_id)


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


def _call_cpp_importer(json_str: str, asset_path: str, compile_blueprint: bool = True) -> Tuple[bool, str]:
    try:
        import unreal

        if hasattr(unreal, "BPDirectImporter"):
            result = unreal.BPDirectImporter.import_blueprint_from_json(
                json_str, asset_path, compile_blueprint
            )
        else:
            result = unreal.call_function(
                "BPDirectImporter", "ImportBlueprintFromJson", json_str, asset_path, compile_blueprint
            )

        success: Optional[bool] = None
        error_text = ""
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
