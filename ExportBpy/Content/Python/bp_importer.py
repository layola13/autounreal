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

    ok, err = _call_cpp_importer(json_str, asset_path, compile_blueprint=False)
    if not ok and err:
        err = _describe_missing_connection_nodes(payload, err)
    bridge_asset_path = _normalize_bridge_blueprint_path(asset_path)
    repair_ok = ok
    repair_err = ""
    compiled_ok = not compile_blueprint
    compile_err = ""

    if ok:
        repair_ok, repair_err = _repair_imported_blueprint_pin_defaults(bridge_asset_path, payload)
        if repair_ok and compile_blueprint:
            compiled_ok, compile_err = _compile_blueprint_with_bridge(bridge_asset_path)
        if repair_ok:
            _save_asset_if_possible(bridge_asset_path)

    success = bool(ok and repair_ok and (compiled_ok if compile_blueprint else True))
    error_parts = [part for part in (err, repair_err, compile_err) if part]
    return {
        "success": success,
        "error": " | ".join(error_parts),
        "asset_path": asset_path,
        "import_mode": "bpy_directory",
        "compiled": bool(success and compile_blueprint),
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

    graph.metadata = meta if isinstance(meta, dict) else {}
    node_guid_map = graph.metadata.get("node_guid", {})
    node_pos_map = graph.metadata.get("node_pos", {})
    node_props_map = graph.metadata.get("node_props", {})
    pin_alias_map = graph.metadata.get("pin_alias", {})
    pin_id_map = graph.metadata.get("pin_id", {})

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
            node.pin_aliases[pin_name] = str(full_pin_name)

    for pin_key, pin_id in pin_id_map.items():
        if "." not in pin_key:
            continue
        readable_name, pin_name = pin_key.split(".", 1)
        node = node_by_name.get(readable_name)
        if node is not None:
            node.pin_ids[pin_name] = str(pin_id)


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


def _load_blueprint_asset_for_repair(asset_path: str):
    if not _HAS_UNREAL:
        return None

    candidates = [_normalize_bridge_blueprint_path(asset_path), asset_path]
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


def _normalize_guid_text(value: Any) -> str:
    text = str(value or "").strip()
    return text.replace("{", "").replace("}", "").replace("-", "").upper()


def _find_live_graph(blueprint: Any, graph_name: str):
    library = getattr(unreal, "BlueprintEditorLibrary", None)
    if library is None or blueprint is None or not graph_name:
        return None

    lowered = graph_name.lower()
    if lowered == "eventgraph" and hasattr(library, "find_event_graph"):
        try:
            graph = library.find_event_graph(blueprint)
        except Exception:
            graph = None
        if graph is not None:
            return graph

    if hasattr(library, "find_graph"):
        try:
            return library.find_graph(blueprint, graph_name)
        except Exception:
            return None

    return None


def _get_live_graph_nodes(graph: Any) -> List[Any]:
    if graph is None:
        return []

    for property_name in ("nodes", "Nodes"):
        try:
            nodes = graph.get_editor_property(property_name)
        except Exception:
            nodes = None
        if nodes is not None:
            try:
                return list(nodes)
            except Exception:
                pass

    for attribute_name in ("nodes", "Nodes"):
        try:
            nodes = getattr(graph, attribute_name)
        except Exception:
            nodes = None
        if nodes is not None:
            try:
                return list(nodes)
            except Exception:
                pass

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


def _find_live_node_by_fallback(graph: Any, source_node: Dict[str, Any], defaults: Dict[str, Any]):
    expected_class = str(source_node.get("node_class", "") or "")
    expected_x = _safe_int_position(source_node.get("pos_x"))
    expected_y = _safe_int_position(source_node.get("pos_y"))
    expected_pins = {str(pin_name).strip() for pin_name in defaults.keys() if str(pin_name).strip()}
    position_tolerance = 64

    if not expected_class:
        return None

    candidates: List[Tuple[int, Any]] = []
    for live_node in _get_live_graph_nodes(graph):
        if _get_live_node_class_name(live_node) != expected_class:
            continue

        if expected_pins:
            missing_pin = False
            for pin_name in expected_pins:
                try:
                    pin = live_node.find_pin(pin_name)
                except Exception:
                    pin = None
                if pin is None:
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
        return "True" if value else "False"
    return str(value)


def _set_live_pin_default(node: Any, pin_name: str, value: Any) -> Tuple[bool, str]:
    if node is None or not hasattr(node, "find_pin"):
        return False, f"Node does not support pin lookup: {pin_name}"

    try:
        pin = node.find_pin(pin_name)
    except Exception as exc:
        return False, str(exc)

    if pin is None:
        return False, f"Pin not found: {pin_name}"

    try:
        schema = pin.get_schema() if hasattr(pin, "get_schema") else None
    except Exception:
        schema = None

    default_object = _resolve_default_object_for_pin(value)
    try:
        if default_object is not None:
            if schema is not None and hasattr(schema, "try_set_default_object"):
                schema.try_set_default_object(pin, default_object, False)
            else:
                pin.default_object = default_object
                pin.default_value = default_object.get_path_name()
            return True, ""

        default_value = _stringify_pin_default(value)
        if schema is not None and hasattr(schema, "try_set_default_value"):
            schema.try_set_default_value(pin, default_value, False)
        else:
            pin.default_value = default_value
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


def _repair_imported_blueprint_pin_defaults(asset_path: str, payload: Dict[str, Any]) -> Tuple[bool, str]:
    graphs = payload.get("graphs", [])
    if not isinstance(graphs, list):
        return True, ""

    blueprint = _load_blueprint_asset_for_repair(asset_path)
    if blueprint is None:
        return False, f"Unable to load blueprint for pin repair: {asset_path}"

    failures: List[str] = []
    skipped_nodes = 0
    for graph in graphs:
        if not isinstance(graph, dict):
            continue

        strand_name = str(graph.get("name", "") or "")
        graph_nodes = graph.get("nodes", [])
        if not strand_name or not isinstance(graph_nodes, list):
            continue

        live_graph = _find_live_graph(blueprint, strand_name)
        if live_graph is None:
            failures.append(f"{strand_name}: graph not found after import")
            continue

        for node in graph_nodes:
            if not isinstance(node, dict):
                continue

            node_guid = str(node.get("node_guid", "") or "").strip()
            defaults = node.get("defaults")
            if not node_guid or not isinstance(defaults, dict) or not defaults:
                continue

            live_node = _find_live_node_by_guid(live_graph, node_guid)
            if live_node is None:
                live_node = _find_live_node_by_fallback(live_graph, node, defaults)
                if live_node is None:
                    skipped_nodes += 1
                    continue

            for pin_name, value in defaults.items():
                normalized_pin_name = str(pin_name).strip()
                if not normalized_pin_name:
                    continue
                ok, error = _set_live_pin_default(live_node, normalized_pin_name, value)
                if not ok:
                    _log_repair_warning(
                        f"Pin repair skipped in {strand_name}: "
                        f"{getattr(live_node, 'get_name', lambda: 'UnknownNode')()}.{normalized_pin_name}: {error}"
                    )

    if skipped_nodes and os.getenv("EXPORTBPY_LOG_PIN_REPAIR_SKIPS", "").lower() in ("1", "true", "yes", "on"):
        _log_repair_warning(
            f"Pin repair skipped on {skipped_nodes} node(s); enable only for importer debugging"
        )

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
