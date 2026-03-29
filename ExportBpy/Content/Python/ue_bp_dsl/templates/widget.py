# Copyright sonygodx@gmail.com. All Rights Reserved.
"""
ue_bp_dsl/templates/widget.py — UMG Widget Blueprint template.
"""

from __future__ import annotations
from typing import Any, Dict


def build(
    name:   str = "WBP_Widget",
    parent: str = "/Script/UMG.UserWidget",
) -> Dict[str, Any]:
    """Return a blueprint metadata dict for a UMG Widget Blueprint."""
    return {
        "name":       name,
        "parent":     parent,
        "variables":  [
            {"name": "bIsVisible", "type": "bool",  "default": "true"},
            {"name": "Opacity",    "type": "float", "default": "1.0"},
        ],
        "components": [],
        "graphs": [],
    }
