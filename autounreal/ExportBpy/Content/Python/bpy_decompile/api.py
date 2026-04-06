"""
api.py — bpy_decompile 统一入口。

用法：
    from bpy_decompile.api import decompile_blueprint

    ok, err = decompile_blueprint(
        exported_dir="...ExportedBlueprints/bpy/AC_TraversalLogic",
        output_dir="...UpperBlueprints/AC_TraversalLogic",
    )
"""
from __future__ import annotations
import traceback
from typing import Tuple
from .reader import read_package
from .upper_emitter import emit_package


def decompile_blueprint(exported_dir: str, output_dir: str) -> Tuple[bool, str]:
    """
    将 ExportedBlueprints/bpy/<BPName>/ 目录反编译为 UpperBlueprints/<BPName>/。

    Parameters
    ----------
    exported_dir : str
        ExportBpy 导出的 DSL 包目录（含 __bp__.bp.py、fn_*.bp.py、*_meta.py）。
    output_dir : str
        写出目标目录，通常为 UpperBlueprints/<BPName>/。

    Returns
    -------
    (ok, err) : (bool, str)
        ok=True 表示成功；失败时 err 含错误上下文。
    """
    try:
        pkg = read_package(exported_dir)
        emit_package(pkg, output_dir)
        return True, ""
    except Exception:
        return False, traceback.format_exc()
