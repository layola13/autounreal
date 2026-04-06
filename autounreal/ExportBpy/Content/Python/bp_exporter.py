# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-
"""
bp_exporter.py — Python-side wrappers for ExportBpy.

目录导出最终委托给 C++ UBPDirectExporter::ExportBlueprintToPy。
"""

from __future__ import annotations

import os
from typing import Tuple

try:
    import unreal  # type: ignore
    _HAS_UNREAL = True
except ImportError:
    _HAS_UNREAL = False


def export_directory(asset_path: str, output_dir: str) -> Tuple[bool, str]:
    if not _HAS_UNREAL:
        return False, "bp_exporter 需要在 Unreal Python 环境中运行"

    result = unreal.BPDirectExporter.export_blueprint_to_py(asset_path, output_dir)
    ok, err = _normalize_result(result)
    exported_dir = os.path.join(output_dir, _asset_name(asset_path))

    try:
        import bp_validator

        valid, errors = bp_validator.validate_path(exported_dir)
        if ok and not valid:
            return False, "\n".join(errors)
        if not ok:
            if not err and valid:
                return True, ""
            return False, err or "\n".join(errors)
    except Exception as exc:
        return False, f"导出成功，但校验失败: {exc}"

    return ok, err


def export_bpy_file(asset_path: str, output_path: str = "") -> Tuple[bool, str]:
    if not _HAS_UNREAL:
        return False, "bp_exporter 需要在 Unreal Python 环境中运行"
    result = unreal.BPDirectExporter.export_blueprint_to_bpy_file(asset_path, output_path)
    return _normalize_result(result)


def read_blueprint_json(asset_path: str) -> str:
    if not _HAS_UNREAL:
        raise RuntimeError("bp_exporter 需要在 Unreal Python 环境中运行")
    return unreal.BPDirectExporter.read_blueprint_to_json(asset_path)


def _normalize_result(result) -> Tuple[bool, str]:
    if isinstance(result, tuple):
        ok = bool(result[0]) if result else False
        err = str(result[1]) if len(result) > 1 else ""
        return ok, err
    return bool(result), ""


def _asset_name(asset_path: str) -> str:
    token = asset_path.replace("\\", "/").split("/")[-1]
    if "." in token:
        token = token.split(".")[-1] or token.split(".")[0]
    if token.endswith("_C"):
        token = token[:-2]
    return token
