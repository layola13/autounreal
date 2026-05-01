from __future__ import annotations

import ast
from typing import Any


def literal(node: ast.AST) -> Any:
    try:
        return ast.literal_eval(node)
    except Exception:
        return None


def call_name(node: ast.AST) -> str | None:
    if isinstance(node, ast.Name):
        return node.id
    if isinstance(node, ast.Attribute):
        base = call_name(node.value)
        return f"{base}.{node.attr}" if base else node.attr
    if isinstance(node, ast.Call):
        return call_name(node.func)
    return None


def attr_chain(node: ast.AST) -> list[str] | None:
    if isinstance(node, ast.Name):
        return [node.id]
    if isinstance(node, ast.Attribute):
        chain = attr_chain(node.value)
        if chain is None:
            return None
        return [*chain, node.attr]
    if isinstance(node, ast.Subscript):
        chain = attr_chain(node.value)
        key = literal(node.slice)
        if chain is None or not isinstance(key, str):
            return None
        return [*chain, key]
    return None


def clean_pin(pin: str) -> str:
    if pin == "exec":
        return "execute"
    if pin.endswith("_"):
        return pin[:-1]
    return pin


def keyword_dict(call: ast.Call) -> dict[str, Any]:
    return {kw.arg: literal(kw.value) for kw in call.keywords if kw.arg}


def positional(call: ast.Call) -> list[Any]:
    return [literal(arg) for arg in call.args]
