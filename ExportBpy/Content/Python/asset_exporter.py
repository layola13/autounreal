# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-
"""
asset_exporter.py — 导出独立 UE 资产属性到 __asset__.meta.py

支持的资产类型（不限于）：
  - InputAction          (/Game/Input/Actions/IA_*)
  - InputMappingContext  (/Game/Input/IMC_*)
  - ChooserTable         (/Game/Choosers/*)
  - PoseSearchDatabase   (/Game/PoseSearch/*)

用法：
    import asset_exporter
    ok, err = asset_exporter.export_asset(
        "/Game/Input/Actions/IA_Jump.IA_Jump",
        "F:/project/thirdperson57/ExportedBlueprints/assets",
    )
"""

from __future__ import annotations

import os
from typing import Tuple

try:
    import unreal  # type: ignore
    _HAS_UNREAL = True
except ImportError:
    _HAS_UNREAL = False


def export_asset(asset_path: str, output_dir: str) -> Tuple[bool, str]:
    """
    导出一个独立资产的非默认属性到 <AssetName>__asset__.meta.py。

    参数
    ----
    asset_path : str
        Unreal 软对象路径，例如 /Game/Input/Actions/IA_Jump.IA_Jump
    output_dir : str
        输出目录的绝对路径

    返回
    ----
    (ok, error_message)
    """
    if not _HAS_UNREAL:
        return False, "asset_exporter 需要在 Unreal Python 环境中运行"

    result = unreal.BPDirectExporter.export_standalone_asset_to_py(asset_path, output_dir)
    return _normalize_result(result)


def export_assets(asset_paths: list, output_dir: str) -> dict:
    """
    批量导出多个资产。

    返回 {"succeeded": [...], "failed": [...]} 字典。
    """
    succeeded, failed = [], []
    for path in asset_paths:
        ok, err = export_asset(path, output_dir)
        if ok:
            succeeded.append(path)
        else:
            failed.append({"asset": path, "error": err})
    return {"succeeded": succeeded, "failed": failed}


def _normalize_result(result) -> Tuple[bool, str]:
    if isinstance(result, tuple):
        ok = bool(result[0]) if result else False
        err = str(result[1]) if len(result) > 1 else ""
        return ok, err
    return bool(result), ""
