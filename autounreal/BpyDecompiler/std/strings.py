from __future__ import annotations

from typing import Any

from . import ue



def concat(a: Any = '', b: Any = '') -> Any:
    return ue.KismetStringLibrary_Concat_StrStr(A=a, B=b)


def to_string_int(value: int) -> Any:
    return ue.KismetStringLibrary_Conv_IntToString(InInt=value)

def __getattr__(name: str) -> Any:
    prefixed = f"KismetStringLibrary_{name}"
    if hasattr(ue, prefixed):
        return getattr(ue, prefixed)
    return getattr(ue, name)
