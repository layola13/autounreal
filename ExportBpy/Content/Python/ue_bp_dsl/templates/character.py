# Copyright sonygodx@gmail.com. All Rights Reserved.
"""
ue_bp_dsl/templates/character.py — Character Blueprint template.

Produces a __bp__.py-equivalent dict for a standard third-person character.

Usage::

    from ue_bp_dsl.templates.character import build
    bp_dict = build(name="BP_MyCharacter")
"""

from __future__ import annotations
from typing import Any, Dict

from ue_bp_dsl.mixins import health, combat, movement, replication, animation


def build(
    name:   str = "BP_Character",
    parent: str = "/Script/Engine.Character",
) -> Dict[str, Any]:
    """
    Return a blueprint metadata dict for a standard Character Blueprint.

    :param name:   Asset name (no path)
    :param parent: Unreal class gate for the parent class
    """
    variables: list = []
    health.apply(variables)
    combat.apply(variables)
    movement.apply(variables)
    replication.apply(variables)
    animation.apply(variables)

    return {
        "name":       name,
        "parent":     parent,
        "variables":  variables,
        "components": [
            {"type": "CapsuleComponent",          "name": "CapsuleComponent"},
            {"type": "ArrowComponent",             "name": "ArrowComponent"},
            {"type": "SkeletalMeshComponent",      "name": "Mesh"},
            {"type": "CharacterMovementComponent", "name": "CharacterMovement"},
            {"type": "SpringArmComponent",         "name": "CameraBoom"},
            {"type": "CameraComponent",            "name": "FollowCamera"},
        ],
        "graphs": [],
    }
