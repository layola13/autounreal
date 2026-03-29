"""
func_reverse.py — 基于 FUNCTION_MAP 构建反向索引：ue_ref -> python_name
"""
from __future__ import annotations
from typing import Dict, Optional
from bpy_compile.maps.function_map import FUNCTION_MAP, FunctionSpec

# 构建反向索引：ue_ref -> python_name（首次出现的 key 优先，避免重复键问题）
_REVERSE: Dict[str, str] = {}
for _py_name, _spec in FUNCTION_MAP.items():
    if _spec.ue_ref not in _REVERSE:
        _REVERSE[_spec.ue_ref] = _py_name


def get_python_name(ue_ref: str) -> Optional[str]:
    """将 CallFunction 节点的 ue_ref 还原为 Upper Python 函数名。"""
    return _REVERSE.get(ue_ref)


def get_function_spec(ue_ref: str) -> Optional[FunctionSpec]:
    """通过 ue_ref 取回 FunctionSpec，找不到返回 None。"""
    py_name = get_python_name(ue_ref)
    if py_name is None:
        return None
    return FUNCTION_MAP.get(py_name)
