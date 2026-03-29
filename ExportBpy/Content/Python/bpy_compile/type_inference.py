# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-

from __future__ import annotations

import ast
from typing import Optional

from .errors import CompileError


BOOL_TYPES = {"bool", "boolean"}
INT_TYPES = {"int", "integer"}
FLOAT_TYPES = {"float", "double"}
STRING_TYPES = {"str", "string"}
VECTOR_TYPES = {"vector", "fvector"}


def normalize_type_name(type_name: Optional[str]) -> Optional[str]:
    if type_name is None:
        return None
    text = str(type_name).strip()
    if not text:
        return None
    return text


def canonical_kind(type_name: Optional[str]) -> str:
    text = (normalize_type_name(type_name) or "").lower()
    if text in BOOL_TYPES:
        return "bool"
    if text in INT_TYPES:
        return "int"
    if text in FLOAT_TYPES:
        return "float"
    if text in STRING_TYPES:
        return "string"
    if text in VECTOR_TYPES:
        return "vector"
    if text.startswith("struct/"):
        return "struct"
    if text.startswith("object/") or text.startswith("/script/") or text.startswith("/game/"):
        return "object"
    if text.startswith("byte/") or text.startswith("enum:"):
        return "enum"
    return text or "unknown"


def type_from_constant(value: object) -> Optional[str]:
    if isinstance(value, bool):
        return "bool"
    if isinstance(value, int) and not isinstance(value, bool):
        return "int"
    if isinstance(value, float):
        return "float"
    if isinstance(value, str):
        return "string"
    return None


def parse_annotation(annotation: Optional[ast.expr]) -> Optional[str]:
    if annotation is None:
        return None
    if isinstance(annotation, ast.Constant) and isinstance(annotation.value, str):
        return normalize_type_name(annotation.value)
    if isinstance(annotation, ast.Name):
        return normalize_type_name(annotation.id)
    if isinstance(annotation, ast.Attribute):
        parts = []
        current: Optional[ast.AST] = annotation
        while isinstance(current, ast.Attribute):
            parts.append(current.attr)
            current = current.value
        if isinstance(current, ast.Name):
            parts.append(current.id)
            parts.reverse()
            return normalize_type_name(".".join(parts))
    return None


BINOP_MAP = {
    ("+", "float"): "KismetMathLibrary::Add_DoubleDouble",
    ("-", "float"): "KismetMathLibrary::Subtract_DoubleDouble",
    ("*", "float"): "KismetMathLibrary::Multiply_DoubleDouble",
    ("/", "float"): "KismetMathLibrary::Divide_DoubleDouble",
    ("+", "int"): "KismetMathLibrary::Add_IntInt",
    ("-", "int"): "KismetMathLibrary::Subtract_IntInt",
    ("*", "int"): "KismetMathLibrary::Multiply_IntInt",
    ("/", "int"): "KismetMathLibrary::Divide_IntInt",
    ("+", "vector"): "KismetMathLibrary::Add_VectorVector",
    ("-", "vector"): "KismetMathLibrary::Subtract_VectorVector",
    ("*vector_float", "vector"): "KismetMathLibrary::Multiply_VectorFloat",
    ("*vector_vector", "vector"): "KismetMathLibrary::Multiply_VectorVector",
    ("+", "string"): "KismetStringLibrary::Concat_StrStr",
}


COMPARE_MAP = {
    ("==", "float"): "KismetMathLibrary::EqualEqual_DoubleDouble",
    ("!=", "float"): "KismetMathLibrary::NotEqual_DoubleDouble",
    (">", "float"): "KismetMathLibrary::Greater_DoubleDouble",
    (">=", "float"): "KismetMathLibrary::GreaterEqual_DoubleDouble",
    ("<", "float"): "KismetMathLibrary::Less_DoubleDouble",
    ("<=", "float"): "KismetMathLibrary::LessEqual_DoubleDouble",
    ("==", "int"): "KismetMathLibrary::EqualEqual_IntInt",
    ("!=", "int"): "KismetMathLibrary::NotEqual_IntInt",
    (">", "int"): "KismetMathLibrary::Greater_IntInt",
    (">=", "int"): "KismetMathLibrary::GreaterEqual_IntInt",
    ("<", "int"): "KismetMathLibrary::Less_IntInt",
    ("<=", "int"): "KismetMathLibrary::LessEqual_IntInt",
    ("==", "bool"): "KismetMathLibrary::EqualEqual_BoolBool",
    ("!=", "bool"): "KismetMathLibrary::NotEqual_BoolBool",
    ("==", "string"): "KismetStringLibrary::EqualEqual_StrStr",
}


BOOL_OP_MAP = {
    "and": "KismetMathLibrary::BooleanAND",
    "or": "KismetMathLibrary::BooleanOR",
    "not": "KismetMathLibrary::Not_PreBool",
}


def infer_binop_function(
    operator_token: str,
    *,
    left_type: Optional[str],
    right_type: Optional[str],
    expected_type: Optional[str],
    file_path: str,
    line: int,
    expr_text: str,
    graph_name: str = "",
) -> tuple[str, Optional[str]]:
    expected_kind = canonical_kind(expected_type)
    left_kind = canonical_kind(left_type)
    right_kind = canonical_kind(right_type)

    if expected_kind == "vector" or left_kind == "vector" or right_kind == "vector":
        if operator_token in {"+", "-"}:
            return BINOP_MAP[(operator_token, "vector")], "Vector"
        if operator_token == "*":
            if left_kind == "vector" and right_kind in {"float", "int"}:
                return BINOP_MAP[("*vector_float", "vector")], "Vector"
            if right_kind == "vector" and left_kind in {"float", "int"}:
                return BINOP_MAP[("*vector_float", "vector")], "Vector"
            if left_kind == "vector" and right_kind == "vector":
                return BINOP_MAP[("*vector_vector", "vector")], "Vector"

    for kind in (expected_kind, left_kind, right_kind):
        key = (operator_token, kind)
        if key in BINOP_MAP:
            if kind == "float":
                return BINOP_MAP[key], "float"
            if kind == "int":
                return BINOP_MAP[key], "int"
            if kind == "string":
                return BINOP_MAP[key], "string"

    raise CompileError(
        message=(
            f"BinOp '{operator_token}' 无法确定类型: left={left_type or 'unknown'}, "
            f"right={right_type or 'unknown'}, expected={expected_type or 'unknown'}, expr={expr_text}"
        ),
        file_path=file_path,
        line=line,
        graph_name=graph_name,
    )


def infer_compare_function(
    operator_token: str,
    *,
    left_type: Optional[str],
    right_type: Optional[str],
    file_path: str,
    line: int,
    expr_text: str,
    graph_name: str = "",
) -> str:
    left_kind = canonical_kind(left_type)
    right_kind = canonical_kind(right_type)
    for kind in (left_kind, right_kind, "float"):
        key = (operator_token, kind)
        if key in COMPARE_MAP:
            return COMPARE_MAP[key]

    raise CompileError(
        message=(
            f"Compare '{operator_token}' 无法确定类型: left={left_type or 'unknown'}, "
            f"right={right_type or 'unknown'}, expr={expr_text}"
        ),
        file_path=file_path,
        line=line,
        graph_name=graph_name,
    )
