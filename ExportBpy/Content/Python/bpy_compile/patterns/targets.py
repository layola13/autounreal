# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-

from __future__ import annotations

import ast
from typing import Optional, Tuple


def extract_self_member_field_target(node: ast.AST) -> Optional[Tuple[str, str]]:
    if not isinstance(node, ast.Attribute):
        return None
    if not isinstance(node.value, ast.Attribute):
        return None
    if not isinstance(node.value.value, ast.Name) or node.value.value.id != "self":
        return None
    return node.value.attr, node.attr


def extract_object_attribute_target(node: ast.AST) -> Optional[Tuple[ast.expr, str]]:
    if not isinstance(node, ast.Attribute):
        return None
    if isinstance(node.value, ast.Name) and node.value.id == "self":
        return None
    if extract_self_member_field_target(node) is not None:
        return None
    return node.value, node.attr
