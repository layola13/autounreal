# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-

from __future__ import annotations

import ast
from typing import Optional


def match_cast_none_compare(node: ast.AST) -> Optional[str]:
    if not isinstance(node, ast.Compare):
        return None
    if len(node.ops) != 1 or len(node.comparators) != 1:
        return None
    if not isinstance(node.left, ast.Name):
        return None
    comparator = node.comparators[0]
    if not isinstance(comparator, ast.Constant) or comparator.value is not None:
        return None
    if not isinstance(node.ops[0], ast.Is):
        return None
    return node.left.id
