from __future__ import annotations

from typing import Any

from . import ue


def rotation(actor: Any | None = None) -> Any:
    if actor is None:
        return ue.K2_GetActorRotation()
    return ue.K2_GetActorRotation(self=actor)


def forward_vector(actor: Any | None = None) -> Any:
    if actor is None:
        return ue.GetActorForwardVector()
    return ue.GetActorForwardVector(self=actor)

def control_rotation() -> Any:
    return ue.GetControlRotation()


def controller(actor: Any | None = None) -> Any:
    if actor is None:
        return ue.GetController()
    return ue.GetController(self=actor)


def input_action_value(action: Any) -> Any:
    return ue.GetInputActionValue(InputAction=action)


def add_controller_yaw_input(value: Any) -> Any:
    return ue.AddControllerYawInput(Val=value)


def add_controller_pitch_input(value: Any) -> Any:
    return ue.AddControllerPitchInput(Val=value)


def transform(actor: Any | None = None) -> Any:
    if actor is None:
        return ue.GetTransform()
    return ue.GetTransform(self=actor)


def location(actor: Any | None = None) -> Any:
    if actor is None:
        return ue.K2_GetActorLocation()
    return ue.K2_GetActorLocation(self=actor)

