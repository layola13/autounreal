from __future__ import annotations

import ast
import py_compile
from dataclasses import dataclass
from pathlib import Path


@dataclass(slots=True)
class CheckIssue:
    path: Path
    line: int
    message: str


def check_upper_dir(root: str | Path) -> list[CheckIssue]:
    base = Path(root).resolve()
    issues: list[CheckIssue] = []
    for path in sorted(base.glob("*.py")):
        try:
            py_compile.compile(str(path), doraise=True)
        except Exception as exc:
            issues.append(CheckIssue(path, 0, f"py_compile failed: {exc}"))
            continue
        tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
        _check_block(path, tree.body, issues)
        _check_output_functions_return(path, tree, issues)
    return issues


def _check_block(path: Path, body: list[ast.stmt], issues: list[CheckIssue]) -> None:
    terminated = False
    for stmt in body:
        if terminated and _is_real_statement(stmt):
            issues.append(CheckIssue(path, getattr(stmt, "lineno", 0), "unreachable statement after return/raise/break/continue"))
        _check_nested(path, stmt, issues)
        if isinstance(stmt, (ast.Return, ast.Raise, ast.Break, ast.Continue)):
            terminated = True


def _check_nested(path: Path, stmt: ast.stmt, issues: list[CheckIssue]) -> None:
    if isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef, ast.If, ast.For, ast.AsyncFor, ast.While, ast.With, ast.AsyncWith, ast.Try, ast.Match)):
        for field_name in ("body", "orelse", "finalbody"):
            value = getattr(stmt, field_name, None)
            if isinstance(value, list):
                _check_block(path, value, issues)
        if isinstance(stmt, ast.Try):
            for handler in stmt.handlers:
                _check_block(path, handler.body, issues)
        if isinstance(stmt, ast.Match):
            for case in stmt.cases:
                _check_block(path, case.body, issues)


def _is_real_statement(stmt: ast.stmt) -> bool:
    if isinstance(stmt, ast.Pass):
        return False
    if isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.Constant) and isinstance(stmt.value.value, str):
        return False
    return True


def _check_output_functions_return(path: Path, tree: ast.Module, issues: list[CheckIssue]) -> None:
    for stmt in tree.body:
        if not isinstance(stmt, ast.FunctionDef):
            continue
        if not _function_has_outputs(stmt):
            continue
        if not _block_guarantees_return(stmt.body):
            issues.append(CheckIssue(path, stmt.lineno, "output function can fall through without return"))


def _function_has_outputs(fn: ast.FunctionDef) -> bool:
    for decorator in fn.decorator_list:
        call = decorator if isinstance(decorator, ast.Call) else None
        if call is None:
            continue
        name = getattr(call.func, "id", "") or getattr(call.func, "attr", "")
        if name not in {"bp_function", "function"}:
            continue
        if name == "function" and not (isinstance(call.func, ast.Attribute) and isinstance(call.func.value, ast.Name) and call.func.value.id == "std"):
            continue
        for keyword in call.keywords:
            if keyword.arg == "outputs" and isinstance(keyword.value, (ast.List, ast.Tuple)) and keyword.value.elts:
                return True
    return False


def _block_guarantees_return(body: list[ast.stmt]) -> bool:
    for stmt in body:
        if isinstance(stmt, (ast.Return, ast.Raise)):
            return True
        if isinstance(stmt, ast.If):
            if stmt.orelse and _block_guarantees_return(stmt.body) and _block_guarantees_return(stmt.orelse):
                return True
        if isinstance(stmt, ast.Match):
            if stmt.cases and any(_is_wildcard_case(case) for case in stmt.cases):
                if all(_block_guarantees_return(case.body) for case in stmt.cases):
                    return True
        if isinstance(stmt, ast.Try):
            handlers_return = stmt.handlers and all(_block_guarantees_return(handler.body) for handler in stmt.handlers)
            if handlers_return and _block_guarantees_return(stmt.body) and (not stmt.orelse or _block_guarantees_return(stmt.orelse)):
                return True
    return False


def _is_wildcard_case(case: ast.match_case) -> bool:
    return isinstance(case.pattern, ast.MatchAs) and case.pattern.name is None and case.pattern.pattern is None
