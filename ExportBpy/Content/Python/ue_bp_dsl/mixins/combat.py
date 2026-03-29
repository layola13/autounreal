# Copyright sonygodx@gmail.com. All Rights Reserved.
"""
ue_bp_dsl/mixins/combat.py — Combat system variable declarations.
"""

from __future__ import annotations
from typing import List


def apply(vars: List[dict]) -> None:
    """Append standard combat variables to the variable list."""
    vars.extend([
        {"name": "AttackDamage",    "type": "float",  "default": "25.0"},
        {"name": "AttackRate",      "type": "float",  "default": "1.0"},
        {"name": "bIsAttacking",    "type": "bool",   "default": "false"},
        {"name": "bCanAttack",      "type": "bool",   "default": "true"},
        {"name": "ComboCount",      "type": "int",    "default": "0"},
        {"name": "MaxComboCount",   "type": "int",    "default": "3"},
    ])
