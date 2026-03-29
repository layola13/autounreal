# Copyright sonygodx@gmail.com. All Rights Reserved.
"""
ue_bp_dsl/mixins/health.py — Health system variable declarations.

Apply this mixin in a __bp__.py to add standard health/damage bookkeeping:

    from ue_bp_dsl.mixins.health import apply
    apply(vars)
"""

from __future__ import annotations
from typing import List


def apply(vars: List[dict]) -> None:
    """
    Append standard health variables to the variable list.

    Each entry matches the UBPDirectImporter variable JSON schema:
        {"name": str, "type": str, "default": str}
    """
    vars.extend([
        {"name": "MaxHealth",     "type": "float",  "default": "100.0"},
        {"name": "CurrentHealth", "type": "float",  "default": "100.0"},
        {"name": "bIsDead",       "type": "bool",   "default": "false"},
        {"name": "DamageMultiplier", "type": "float", "default": "1.0"},
    ])
