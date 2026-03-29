# Copyright sonygodx@gmailcom. All Rights Reserved.
# -*- coding: utf-8 -*-

from __future__ import annotations

import os
import sys
from typing import Dict, List, Optional, Tuple

from ue_bp_dsl.core import Blueprint

import bp_importer
import asset_importer

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


def import_asset_metas(asset_meta_dir: str) -> Dict:
    """
    单独导入一个目录下所有 *__asset__.meta.py（不涉及蓝图编译）。

    返回 {"succeeded": [...], "failed": [...]} 字典。
    """
    ensure_plugin_python_path()
    return asset_importer.import_asset_meta_dir(asset_meta_dir)


def _default_compiled_package_dir(source_dir: str) -> str:
    package_name = os.path.basename(os.path.abspath(source_dir))
    project_dir = os.path.abspath(os.path.join(source_dir, "..", ".."))
    return os.path.join(project_dir, "ExportedBlueprints", "bpy", package_name)
