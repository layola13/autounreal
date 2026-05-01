from __future__ import annotations

from typing import Any

from . import ue


def size_xy(value: Any) -> Any:
    return ue.vsize_xy(A=value)


def rotate(value: Any, rotation: Any) -> Any:
    return ue.KismetMathLibrary_LessLess_VectorRotator(A=value, B=rotation)
