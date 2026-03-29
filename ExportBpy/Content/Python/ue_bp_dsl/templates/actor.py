# Copyright sonygodx@gmail.com. All Rights Reserved.
"""
ue_bp_dsl/templates/actor.py — Generic Actor Blueprint template.
"""

from __future__ import annotations
from typing import Any, Dict


def build(
    name:   str = "BP_Actor",
    parent: str = "/Script/Engine.Actor",
) -> Dict[str, Any]:
    """Return a blueprint metadata dict for a generic Actor Blueprint."""
    return {
        "name":       name,
        "parent":     parent,
        "variables":  [],
        "components": [
            {"type": "SceneComponent",      "name": "DefaultSceneRoot"},
            {"type": "StaticMeshComponent", "name": "StaticMesh"},
        ],
        "graphs": [],
    }
