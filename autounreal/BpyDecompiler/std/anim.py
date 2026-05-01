from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

@dataclass
class AnimNode:
    node_type: str
    inputs: dict[str, Any] = field(default_factory=dict)
    settings: Any = None

def make_anim_node(node_type: str, **kwargs: Any) -> AnimNode:
    settings = kwargs.pop("settings", None)
    return AnimNode(node_type=node_type, inputs=kwargs, settings=settings)
