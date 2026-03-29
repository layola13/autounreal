# Copyright sonygodx@gmail.com. All Rights Reserved.
"""
ue_bp_dsl/templates/anim_bp.py — Animation Blueprint template.
"""

from __future__ import annotations
from typing import Any, Dict

from ue_bp_dsl.mixins import animation


def build(
    name:   str = "ABP_Character",
    parent: str = "/Script/Engine.AnimInstance",
) -> Dict[str, Any]:
    """Return a blueprint metadata dict for an Animation Blueprint."""
    variables: list = []
    animation.apply(variables)

    return {
        "name":       name,
        "parent":     parent,
        "variables":  variables,
        "components": [],
        "graphs": [],
    }
