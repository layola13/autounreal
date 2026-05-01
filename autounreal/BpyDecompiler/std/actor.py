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
