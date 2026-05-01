from __future__ import annotations

from typing import Any

from . import ue


def __getattr__(name: str) -> Any:
    prefixed = f"GameplayStatics_{name}"
    if hasattr(ue, prefixed):
        return getattr(ue, prefixed)
    return getattr(ue, name)
