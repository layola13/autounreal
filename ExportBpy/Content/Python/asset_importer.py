# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-
"""
asset_importer.py — 将 __asset__.meta.py 中的属性重新写入 Unreal 独立资产

支持的资产类型（不限于）：
  - InputAction
  - InputMappingContext
  - ChooserTable
  - PoseSearchDatabase

用法：
    import asset_importer

    # 导入单个 meta 文件
    ok, err = asset_importer.import_asset_meta(
        "F:/project/thirdperson57/ExportedBlueprints/assets/IA_Jump__asset__.meta.py"
    )

    # 批量导入目录下所有 meta 文件
    results = asset_importer.import_asset_meta_dir(
        "F:/project/thirdperson57/ExportedBlueprints/assets"
    )
"""

from __future__ import annotations

import ast
import json
import os
from typing import Any, Dict, Optional, Tuple

try:
    import unreal  # type: ignore
    _HAS_UNREAL = True
except ImportError:
    _HAS_UNREAL = False


def import_asset_meta(meta_path: str) -> Tuple[bool, str]:
    """
    读取一个 *__asset__.meta.py 文件，将其 META["properties"] 写回到
    META["asset"] 指定的 Unreal 资产。

    参数
    ----
    meta_path : str
        __asset__.meta.py 文件的绝对路径

    返回
    ----
    (ok, error_message)
    """
    if not os.path.isfile(meta_path):
        return False, f"meta 文件不存在: {meta_path}"

    meta = _load_meta_dict(meta_path)
    if meta is None:
        return False, f"无法解析 META 字典: {meta_path}"

    asset_path = meta.get("asset", "")
    if not asset_path:
        return False, f"META 缺少 'asset' 字段: {meta_path}"

    properties: Dict[str, Any] = meta.get("properties", {})
    if not properties:
        # 没有属性需要写入，视为成功
        return True, ""

    return _apply_properties(asset_path, properties)


def import_asset_meta_dir(dir_path: str) -> Dict[str, Any]:
    """
    批量导入目录下所有 *__asset__.meta.py 文件。

    返回 {"succeeded": [...], "failed": [...]} 字典。
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


# ── Internal helpers ──────────────────────────────────────────────────────────

def _load_meta_dict(meta_path: str) -> Optional[Dict[str, Any]]:
    """静态解析 __asset__.meta.py，提取 META 字典（不执行代码）。"""
    with open(meta_path, "r", encoding="utf-8") as fh:
        tree = ast.parse(fh.read(), filename=meta_path)

    for stmt in tree.body:
        if not isinstance(stmt, ast.Assign):
            continue
        for target in stmt.targets:
            if isinstance(target, ast.Name) and target.id == "META":
                value = ast.literal_eval(stmt.value)
                return value if isinstance(value, dict) else None
    return None


def _apply_properties(asset_path: str, properties: Dict[str, Any]) -> Tuple[bool, str]:
    """将 properties 字典 JSON 序列化后调用 C++ ImportStandaloneAssetFromJson。"""
    try:
        props_json = json.dumps(properties, ensure_ascii=False)
    except Exception as exc:
        return False, f"序列化 properties 失败: {exc}"

    if not _HAS_UNREAL:
        # dry-run
        print(f"[asset_importer] dry-run — asset: {asset_path}")
        print(props_json[:1000])
        return True, ""

    result = unreal.BPDirectImporter.import_standalone_asset_from_json(
        asset_path, props_json
    )
    return _normalize_result(result)


def _normalize_result(result) -> Tuple[bool, str]:
    if isinstance(result, tuple):
        ok = bool(result[0]) if result else False
        err = str(result[1]) if len(result) > 1 else ""
        return ok, err
    return bool(result), ""
