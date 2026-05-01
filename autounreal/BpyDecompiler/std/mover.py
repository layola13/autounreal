from __future__ import annotations

from typing import Any

from . import ue


def velocity(component: Any) -> Any:
    return ue.MoverComponent_GetVelocity(self=component)


def target_orientation(component: Any) -> Any:
    return ue.MoverComponent_GetTargetOrientation(self=component)


def movement_intent(component: Any) -> Any:
    return ue.MoverComponent_GetMovementIntent(self=component)
