# Copyright sonygodx@gmail.com. All Rights Reserved.
"""
ue_bp_dsl/mixins/animation.py — Animation variable declarations.
"""

from __future__ import annotations
from typing import List


def apply(vars: List[dict]) -> None:
    """Append standard animation state variables to the variable list."""
    vars.extend([
        {"name": "bIsMoving",       "type": "bool",   "default": "false"},
        {"name": "bIsInAir",        "type": "bool",   "default": "false"},
        {"name": "Speed",           "type": "float",  "default": "0.0"},
        {"name": "Direction",       "type": "float",  "default": "0.0"},
        {"name": "AimPitch",        "type": "float",  "default": "0.0"},
        {"name": "AimYaw",          "type": "float",  "default": "0.0"},
        {"name": "bIsAiming",       "type": "bool",   "default": "false"},
        {"name": "bIsRolling",      "type": "bool",   "default": "false"},
    ])
