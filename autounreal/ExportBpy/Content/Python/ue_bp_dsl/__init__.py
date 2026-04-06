# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-
"""
ue_bp_dsl/__init__.py — 公开 API 导出

在 .bp.py 脚本里只需：
    from ue_bp_dsl import *
"""

from .core import (
    # 顶层对象
    Blueprint,
    GraphContext,
    NodeProxy,
    PinRef,
    # DSL 辅助函数
    float_track,
    vector_track,
    color_track,
    event_track,
    key,
    asset,
    soft_ref,
    class_ref,
    vec3,
    struct,
    enum,
)

__all__ = [
    "Blueprint",
    "GraphContext",
    "NodeProxy",
    "PinRef",
    "float_track",
    "vector_track",
    "color_track",
    "event_track",
    "key",
    "asset",
    "soft_ref",
    "class_ref",
    "vec3",
    "struct",
    "enum",
]
