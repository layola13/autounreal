# Copyright sonygodx@gmail.com. All Rights Reserved.
"""
ue_bp_dsl/mixins/replication.py — Replication variable declarations.
"""

from __future__ import annotations
from typing import List


def apply(vars: List[dict]) -> None:
    """Append standard replication variables to the variable list."""
    vars.extend([
        {"name": "bReplicates",          "type": "bool",  "default": "true"},
        {"name": "bReplicateMovement",   "type": "bool",  "default": "true"},
        {"name": "NetUpdateFrequency",   "type": "float", "default": "100.0"},
        {"name": "MinNetUpdateFrequency","type": "float", "default": "33.0"},
    ])
