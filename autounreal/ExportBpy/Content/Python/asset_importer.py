# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-
"""
asset_importer.py — 导入独立 UE 资产。

兼容两种输入格式：
  1. 旧格式：__asset__.meta.py
  2. 新格式：package_dir/__bp__.bp.py + package_dir/asset_meta.py
"""

from __future__ import annotations

import ast
import copy
import json
import os
from typing import Any, Dict, Optional, Tuple

try:
    import unreal  # type: ignore

    _HAS_UNREAL = True
except ImportError:
    _HAS_UNREAL = False


MAIN_BP_FILE = "__bp__.bp.py"
DEFAULT_META_FILE = "asset_meta.py"


def import_asset_meta(meta_path: str, target_path: Optional[str] = None) -> Tuple[bool, str]:
    """
    读取一个 *__asset__.meta.py 或 asset_meta.py，并写回到目标资产。
    """
    if not os.path.isfile(meta_path):
        return False, f"meta 文件不存在: {meta_path}"

    meta = _load_meta_dict(meta_path)
    if meta is None:
        return False, f"无法解析 META 字典: {meta_path}"

    details = import_asset_meta_dict(meta, target_path=target_path)
    return bool(details.get("success")), str(details.get("error", ""))


def import_asset_meta_dict(meta: Dict[str, Any], target_path: Optional[str] = None) -> Dict[str, Any]:
    normalized_target_path = _normalize_asset_path(target_path or meta.get("asset", ""))
    if not normalized_target_path:
        return {"success": False, "error": "META 缺少目标资产路径"}

    rewritten_meta = _rewrite_meta_for_target(meta, normalized_target_path)
    ok, err = _apply_asset_meta(normalized_target_path, rewritten_meta)
    return {
        "success": ok,
        "error": err,
        "asset_path": normalized_target_path,
        "import_mode": "standalone_asset_meta",
        "compiled": False,
    }


def import_asset_package(package_path: str, target_path: Optional[str] = None) -> Dict[str, Any]:
    """
    导入一个 standalone asset bpy package。
    """
    package_dir, main_path = _resolve_package_inputs(package_path)
    if not package_dir or not os.path.isfile(main_path):
        return {
            "success": False,
            "error": f"找不到 standalone asset package: {package_path}",
            "import_mode": "standalone_asset_package",
            "compiled": False,
        }

    descriptor = _load_python_literal(main_path, "bp")
    if not isinstance(descriptor, dict) or descriptor.get("kind") != "standalone_asset":
        return {
            "success": False,
            "error": f"不是 standalone asset package: {main_path}",
            "import_mode": "standalone_asset_package",
            "compiled": False,
        }

    meta_file = descriptor.get("meta_file", DEFAULT_META_FILE)
    meta_path = os.path.join(package_dir, meta_file)
    if not os.path.isfile(meta_path):
        return {
            "success": False,
            "error": f"standalone asset package 缺少 meta 文件: {meta_path}",
            "import_mode": "standalone_asset_package",
            "compiled": False,
        }

    meta = _load_meta_dict(meta_path)
    if meta is None:
        return {
            "success": False,
            "error": f"无法解析 META 字典: {meta_path}",
            "import_mode": "standalone_asset_package",
            "compiled": False,
        }

    details = import_asset_meta_dict(meta, target_path=target_path)
    details["import_mode"] = "standalone_asset_package"
    details["package_dir"] = package_dir
    details["meta_path"] = meta_path
    return details


def import_asset_meta_dir(dir_path: str) -> Dict[str, Any]:
    """
    批量导入目录下所有 *__asset__.meta.py 文件。
    """
    if not os.path.isdir(dir_path):
        return {"succeeded": [], "failed": [{"asset": dir_path, "error": "目录不存在"}]}

    succeeded, failed = [], []
    for fname in sorted(os.listdir(dir_path)):
        if not fname.endswith("__asset__.meta.py"):
            continue
        fpath = os.path.join(dir_path, fname)
        ok, err = import_asset_meta(fpath)
        if ok:
            succeeded.append(fpath)
        else:
            failed.append({"file": fpath, "error": err})

    return {"succeeded": succeeded, "failed": failed}


def _resolve_package_inputs(package_path: str) -> Tuple[str, str]:
    normalized_path = os.path.abspath(package_path)
    if os.path.isdir(normalized_path):
        return normalized_path, os.path.join(normalized_path, MAIN_BP_FILE)

    if os.path.isfile(normalized_path) and os.path.basename(normalized_path) == MAIN_BP_FILE:
        return os.path.dirname(normalized_path), normalized_path

    return "", ""


def _normalize_asset_path(asset_path: str) -> str:
    normalized = (asset_path or "").strip()
    if normalized.startswith("/") and "." not in normalized:
        asset_name = normalized.rsplit("/", 1)[-1]
        if asset_name:
            return f"{normalized}.{asset_name}"
    return normalized


def _load_meta_dict(meta_path: str) -> Optional[Dict[str, Any]]:
    return _load_python_literal(meta_path, "META")


def _load_python_literal(py_path: str, variable_name: str) -> Optional[Dict[str, Any]]:
    with open(py_path, "r", encoding="utf-8") as handle:
        tree = ast.parse(handle.read(), filename=py_path)

    for stmt in tree.body:
        if not isinstance(stmt, ast.Assign):
            continue
        for target in stmt.targets:
            if isinstance(target, ast.Name) and target.id == variable_name:
                value = ast.literal_eval(stmt.value)
                return value if isinstance(value, dict) else None
    return None


def _rewrite_meta_for_target(meta: Dict[str, Any], target_asset_path: str) -> Dict[str, Any]:
    rewritten = copy.deepcopy(meta)
    source_asset_path = _normalize_asset_path(rewritten.get("asset", ""))
    target_asset_path = _normalize_asset_path(target_asset_path)

    rewritten["kind"] = rewritten.get("kind", "standalone_asset")
    rewritten["asset"] = target_asset_path

    if target_asset_path.startswith("/") and "." in target_asset_path:
        rewritten["package"] = target_asset_path.rsplit(".", 1)[0]
        rewritten["outer"] = target_asset_path.rsplit(".", 1)[0]

    if source_asset_path and source_asset_path != target_asset_path:
        rewritten = _deep_replace_asset_path(rewritten, source_asset_path, target_asset_path)
        rewritten["asset"] = target_asset_path
        if target_asset_path.startswith("/") and "." in target_asset_path:
            rewritten["package"] = target_asset_path.rsplit(".", 1)[0]
            rewritten["outer"] = target_asset_path.rsplit(".", 1)[0]

    return rewritten


def _deep_replace_asset_path(value: Any, source_asset_path: str, target_asset_path: str) -> Any:
    if isinstance(value, dict):
        return {
            key: _deep_replace_asset_path(item, source_asset_path, target_asset_path)
            for key, item in value.items()
        }
    if isinstance(value, list):
        return [
            _deep_replace_asset_path(item, source_asset_path, target_asset_path)
            for item in value
        ]
    if isinstance(value, str):
        return value.replace(source_asset_path, target_asset_path)
    return value


def _apply_asset_meta(asset_path: str, meta: Dict[str, Any]) -> Tuple[bool, str]:
    try:
        meta_json = json.dumps(meta, ensure_ascii=False)
    except Exception as exc:
        return False, f"序列化 META 失败: {exc}"

    if not _HAS_UNREAL:
        print(f"[asset_importer] dry-run — asset: {asset_path}")
        print(meta_json[:2000])
        return True, ""

    try:
        importer = unreal.BPDirectImporter
        if hasattr(importer, "import_standalone_asset_from_json_detailed"):
            detailed_result = importer.import_standalone_asset_from_json_detailed(
                asset_path, meta_json
            )
            if isinstance(detailed_result, str):
                parsed = _parse_detailed_result_json(detailed_result)
                if parsed is not None:
                    return parsed
        result = importer.import_standalone_asset_from_json(asset_path, meta_json)
    except Exception as exc:
        return False, str(exc)

    return _normalize_result(result, asset_path)


def _parse_detailed_result_json(result_text: str) -> Optional[Tuple[bool, str]]:
    try:
        payload = json.loads(result_text)
    except Exception:
        return None
    if not isinstance(payload, dict) or "success" not in payload:
        return None
    return bool(payload.get("success", False)), str(payload.get("error", ""))


def _normalize_result(result, asset_path: str) -> Tuple[bool, str]:
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

    if hasattr(unreal, "EditorAssetLibrary") and not unreal.EditorAssetLibrary.does_asset_exist(
        asset_path.rsplit(".", 1)[0]
    ):
        return False, error_text or f"C++ importer reported success but asset does not exist: {asset_path}"

    return True, error_text
