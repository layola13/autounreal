# Copyright sonygodx@gmail.com. All Rights Reserved.
"""
ue_bp_dsl/mixins/movement.py — Movement variable declarations.
"""

from __future__ import annotations
from typing import List


def apply(vars: List[dict]) -> None:
    """Append standard movement variables to the variable list."""
    vars.extend([
        {"name": "WalkSpeed",       "type": "float",  "default": "600.0"},
        {"name": "SprintSpeed",     "type": "float",  "default": "1000.0"},
        {"name": "CrouchSpeed",     "type": "float",  "default": "300.0"},
        {"name": "bIsSprinting",    "type": "bool",   "default": "false"},
        {"name": "bIsCrouching",    "type": "bool",   "default": "false"},
        {"name": "bCanMove",        "type": "bool",   "default": "true"},
        {"name": "JumpZVelocity",   "type": "float",  "default": "600.0"},
    ])
