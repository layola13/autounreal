# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-
"""
asset_exporter.py — 导出独立 UE 资产。

兼容两种导出格式：
  1. 旧格式：<AssetName>__asset__.meta.py
  2. 新格式：package_dir/__bp__.bp.py + package_dir/asset_meta.py
"""

from __future__ import annotations

import ast
import json
import os
from typing import Any, Dict, Tuple

try:
    import unreal  # type: ignore

    _HAS_UNREAL = True
except ImportError:
    _HAS_UNREAL = False


MAIN_BP_FILE = "__bp__.bp.py"
DEFAULT_META_FILE = "asset_meta.py"


def export_asset(asset_path: str, output_dir: str) -> Tuple[bool, str]:
    """
    导出一个独立资产到旧的 __asset__.meta.py 格式。
    """
    if not _HAS_UNREAL:
        return False, "asset_exporter 需要在 Unreal Python 环境中运行"

    normalized_asset_path = _normalize_asset_path(asset_path)
    normalized_output_dir = os.path.abspath(output_dir)
    result = unreal.BPDirectExporter.export_standalone_asset_to_py(
        normalized_asset_path, normalized_output_dir
    )
    return _normalize_result(result)


def export_asset_bpy_package(asset_path: str, output_path: str) -> Dict[str, Any]:
    """
    导出一个独立资产到新的 bpy package 目录格式。
    """
    normalized_asset_path = _normalize_asset_path(asset_path)
    package_dir = _resolve_package_dir(output_path, normalized_asset_path)
    os.makedirs(package_dir, exist_ok=True)

    ok, err = export_asset(normalized_asset_path, package_dir)
    if not ok:
        return {
            "success": False,
            "error": err,
            "asset_path": normalized_asset_path,
            "package_dir": package_dir,
        }

    legacy_meta_path = os.path.join(
        package_dir, f"{_asset_name_from_path(normalized_asset_path)}__asset__.meta.py"
    )
    meta = _load_meta_dict(legacy_meta_path)
    if meta is None:
        return {
            "success": False,
            "error": f"无法解析导出的 META: {legacy_meta_path}",
            "asset_path": normalized_asset_path,
            "package_dir": package_dir,
        }

    package_meta_path = os.path.join(package_dir, DEFAULT_META_FILE)
    package_main_path = os.path.join(package_dir, MAIN_BP_FILE)

    _write_py_literal(package_meta_path, "META", meta)
    _write_py_literal(package_main_path, "bp", _build_package_descriptor(meta))

    if os.path.isfile(legacy_meta_path):
        os.remove(legacy_meta_path)

    return {
        "success": True,
        "error": "",
        "asset_path": meta.get("asset", normalized_asset_path),
        "asset_class": meta.get("asset_class", ""),
        "package_dir": package_dir,
        "output_path": package_main_path,
        "meta_path": package_meta_path,
        "format": "bpy_directory",
        "producer": "ExportBpy",
    }


def export_assets(asset_paths: list, output_dir: str) -> dict:
    """
    批量导出多个资产到旧的 flat meta 格式。
    """
    succeeded, failed = [], []
    for path in asset_paths:
        ok, err = export_asset(path, output_dir)
        if ok:
            succeeded.append(path)
        else:
            failed.append({"asset": path, "error": err})
    return {"succeeded": succeeded, "failed": failed}


def _normalize_asset_path(asset_path: str) -> str:
    normalized = (asset_path or "").strip()
    if normalized.startswith("/") and "." not in normalized:
        asset_name = normalized.rsplit("/", 1)[-1]
        if asset_name:
            return f"{normalized}.{asset_name}"
    return normalized


def _asset_name_from_path(asset_path: str) -> str:
    normalized = _normalize_asset_path(asset_path)
    if "." in normalized:
        return normalized.rsplit(".", 1)[-1]
    return normalized.rsplit("/", 1)[-1]


def _default_package_dir_name(asset_path: str) -> str:
    normalized = _normalize_asset_path(asset_path)
    return (
        normalized.replace("\\", "_")
        .replace("/", "_")
        .replace(":", "_")
        .replace("*", "_")
        .replace("?", "_")
        .replace("\"", "_")
        .replace("<", "_")
        .replace(">", "_")
        .replace("|", "_")
    )


def _resolve_package_dir(output_path: str, asset_path: str) -> str:
    normalized_output = os.path.abspath(output_path)
    file_name = os.path.basename(normalized_output)

    if file_name.lower() == MAIN_BP_FILE.lower():
        return os.path.dirname(normalized_output)

    if os.path.splitext(normalized_output)[1].lower() == ".py":
        return os.path.dirname(normalized_output)

    if os.path.splitext(normalized_output)[1]:
        raise ValueError(f"standalone asset bpy 导出路径必须是目录或 {MAIN_BP_FILE}: {output_path}")

    if os.path.basename(normalized_output) == _default_package_dir_name(asset_path):
        return normalized_output

    return os.path.join(normalized_output, _default_package_dir_name(asset_path))


def _load_meta_dict(meta_path: str) -> Dict[str, Any] | None:
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


def _build_package_descriptor(meta: Dict[str, Any]) -> Dict[str, Any]:
    return {
        "kind": "standalone_asset",
        "asset": meta.get("asset", ""),
        "asset_class": meta.get("asset_class", ""),
        "meta_file": DEFAULT_META_FILE,
    }


def _write_py_literal(path: str, variable_name: str, payload: Dict[str, Any]) -> None:
    content = "# Auto-generated by ExportBpy\n\n"
    content += f"{variable_name} = "
    content += json.dumps(payload, ensure_ascii=False, indent=2)
    content += "\n"
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(content)


def _normalize_result(result) -> Tuple[bool, str]:
    success = None
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
        return False, error_text or "C++ exporter did not return a success flag"
    if success is False:
        return False, error_text or "C++ exporter returned failure without an error message"

    return True, error_text
