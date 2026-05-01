from __future__ import annotations

from dataclasses import dataclass

@dataclass(frozen=True)
class InputActionState:
    action: str
    Triggered: bool = True
    Started: bool = True
    Completed: bool = True
    Canceled: bool = False

def InputAction(action: str) -> InputActionState:
    return InputActionState(action)

@dataclass(frozen=True)
class InputKeyState:
    key: str
    Pressed: bool = True
    Released: bool = True

def InputKey(key: str) -> InputKeyState:
    return InputKeyState(key)
