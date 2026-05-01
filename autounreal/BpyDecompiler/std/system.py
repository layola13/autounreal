from __future__ import annotations

from typing import Any

from . import ue



def console_variable_int(name: str) -> Any:
    return ue.KismetSystemLibrary_GetConsoleVariableIntValue(VariableName=name)


def console_variable_bool(name: str) -> Any:
    return ue.KismetSystemLibrary_GetConsoleVariableBoolValue(VariableName=name)


def console_variable_float(name: str) -> Any:
    return ue.KismetSystemLibrary_GetConsoleVariableFloatValue(VariableName=name)


def console_variable_string(name: str) -> Any:
    return ue.KismetSystemLibrary_GetConsoleVariableStringValue(VariableName=name)

def __getattr__(name: str) -> Any:
    prefixed = f"KismetSystemLibrary_{name}"
    if hasattr(ue, prefixed):
        return getattr(ue, prefixed)
    return getattr(ue, name)
