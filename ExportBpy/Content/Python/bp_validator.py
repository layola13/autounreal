# Copyright sonygodx@gmail.com. All Rights Reserved.
"""
bp_validator.py — Validates Python DSL exports before import.
"""

from __future__ import annotations

import ast
import os
import py_compile
from typing import List, Tuple


GRAPH_PREFIXES = ("evt_", "fn_", "macro_", "tl_")
MAIN_BP_FILE = "__bp__.bp.py"


def _is_graph_dsl_file(file_name: str) -> bool:
    if file_name == MAIN_BP_FILE or file_name.endswith("_meta.py"):
        return False
    if not file_name.endswith(".bp.py"):
        return False
    return any(file_name.startswith(prefix) for prefix in GRAPH_PREFIXES)


def _is_allowed_auxiliary_bp_file(directory: str, file_name: str) -> bool:
    if not file_name.endswith(".bp.py") or file_name == MAIN_BP_FILE:
        return False
    if _is_graph_dsl_file(file_name):
        return False

    expected_name = os.path.basename(os.path.normpath(directory)) + ".bp.py"
    return file_name == expected_name


def _is_directory_entry_file(path: str) -> bool:
    file_name = os.path.basename(path)
    if file_name == MAIN_BP_FILE:
        return True
    if not file_name.endswith(".bp.py") or file_name.endswith("_meta.py"):
        return False
    if _is_graph_dsl_file(file_name):
        return False
    expected_name = os.path.basename(os.path.dirname(os.path.normpath(path))) + ".bp.py"
    return file_name == expected_name


def validate_path(path: str) -> Tuple[bool, List[str]]:
    if os.path.isdir(path):
        return validate(path)
    if os.path.isfile(path):
        return validate_file(path)
    return False, [f"路径不存在: {path}"]


def validate(directory: str) -> Tuple[bool, List[str]]:
    """
    校验目录模式导出。
    """
    errors: List[str] = []

    if not os.path.isdir(directory):
        return False, [f"Directory does not exist: {directory}"]

    main_path = os.path.join(directory, MAIN_BP_FILE)
    if not os.path.isfile(main_path):
        errors.append(f"Missing {MAIN_BP_FILE} in directory.")
        return False, errors

    _check_python_file(main_path, errors)

    graph_files = []
    for fname in sorted(os.listdir(directory)):
        if not _is_graph_dsl_file(fname):
            if fname.endswith(".bp.py") and fname != MAIN_BP_FILE:
                fpath = os.path.join(directory, fname)
                if _is_allowed_auxiliary_bp_file(directory, fname):
                    _check_python_file(fpath, errors)
                else:
                    errors.append(f"Unknown graph file prefix: {fname}")
            continue

        fpath = os.path.join(directory, fname)
        graph_files.append(fpath)
        _check_python_file(fpath, errors)
        _check_register_function(fpath, errors)

    try:
        from bp_importer import _exec_directory_dsl

        bp = _exec_directory_dsl(directory)
        if not hasattr(bp, "to_dict"):
            errors.append(f"{os.path.basename(main_path)} did not produce a Blueprint-like object.")
        else:
            payload = bp.to_dict()
            if not isinstance(payload, dict):
                errors.append("Blueprint.to_dict() did not return a dict.")
    except Exception as exc:
        errors.append(f"Failed to execute {os.path.basename(main_path)}: {exc}")

    return len(errors) == 0, errors


def validate_file(path: str) -> Tuple[bool, List[str]]:
    """
    兼容旧单文件模式校验。
    """
    errors: List[str] = []

    if not os.path.isfile(path):
        return False, [f"File does not exist: {path}"]
    if not path.endswith(".bp.py"):
        return False, [f"Only .bp.py files are supported: {path}"]
    if _is_directory_entry_file(path):
        return validate(os.path.dirname(path))

    _check_python_file(path, errors)

    try:
        from bp_importer import _exec_file_dsl

        bp = _exec_file_dsl(path)
        if not hasattr(bp, "to_dict"):
            errors.append(f"{os.path.basename(path)} did not create a Blueprint object.")
    except Exception as exc:
        errors.append(f"Failed to execute {os.path.basename(path)}: {exc}")

    return len(errors) == 0, errors


def _check_python_file(path: str, errors: List[str]) -> None:
    try:
        py_compile.compile(path, doraise=True)
    except py_compile.PyCompileError as exc:
        errors.append(f"Syntax error in {os.path.basename(path)}: {exc.msg}")
        return
    except Exception as exc:
        errors.append(f"Cannot read {os.path.basename(path)}: {exc}")
        return

    try:
        with open(path, "r", encoding="utf-8") as handle:
            ast.parse(handle.read(), filename=path)
    except SyntaxError as exc:
        errors.append(f"Syntax error in {os.path.basename(path)}: {exc}")
    except Exception as exc:
        errors.append(f"Cannot parse {os.path.basename(path)}: {exc}")


def _check_register_function(path: str, errors: List[str]) -> None:
    try:
        with open(path, "r", encoding="utf-8") as handle:
            tree = ast.parse(handle.read(), filename=path)
    except Exception:
        return

    register_defs = [
        node for node in tree.body
        if isinstance(node, ast.FunctionDef) and node.name == "register"
    ]
    if not register_defs:
        errors.append(f"{os.path.basename(path)} is missing register(bp).")
        return

    register = register_defs[0]
    arg_count = len(register.args.args)
    if arg_count != 1 or register.args.args[0].arg != "bp":
        errors.append(f"{os.path.basename(path)} register() must have exactly one argument named 'bp'.")
