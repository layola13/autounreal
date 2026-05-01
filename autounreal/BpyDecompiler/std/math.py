from __future__ import annotations

from typing import Any

from . import ue



def map_range_clamped(value: Any, in_range: tuple[float, float], out_range: tuple[float, float]) -> Any:
    return ue.KismetMathLibrary_MapRangeClamped(
        Value=value,
        InRangeA=in_range[0],
        InRangeB=in_range[1],
        OutRangeA=out_range[0],
        OutRangeB=out_range[1],
    )


def normalized_delta_rotator(a: Any, b: Any) -> Any:
    return ue.KismetMathLibrary_NormalizedDeltaRotator(A=a, B=b)


def clamp(value: Any, min_value: Any, max_value: Any) -> Any:
    return ue.KismetMathLibrary_Clamp(Value=value, Min=min_value, Max=max_value)


def add_int(a: int, b: int) -> Any:
    return ue.KismetMathLibrary_Add_IntInt(A=a, B=b)


def subtract_int(a: int, b: int) -> Any:
    return ue.KismetMathLibrary_Subtract_IntInt(A=a, B=b)

def __getattr__(name: str) -> Any:
    prefixed = f"KismetMathLibrary_{name}"
    if hasattr(ue, prefixed):
        return getattr(ue, prefixed)
    return getattr(ue, name)
