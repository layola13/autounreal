# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-

from __future__ import annotations

import ast
import importlib.util
import os
import sys
import uuid
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

from ue_bp_dsl.core import Blueprint

from .errors import CompileError
from .meta_resolver import MetaResolver
from .type_inference import parse_annotation


@dataclass
class GraphSpec:
    kind: str
    graph_name: str
    file_path: str
    func_def: ast.FunctionDef
    inputs: List[Tuple[str, str]]
    outputs: List[Tuple[str, str]]
    pure: bool
    source: str
    meta: Optional[Dict[str, Any]]
    entry: Optional[str] = None


@dataclass
class LoadedUpperPackage:
    source_dir: str
    blueprint: Blueprint
    graphs: List[GraphSpec]
    reference_dir: str
    meta_resolver: MetaResolver


def load_upper_package(source_dir: str, reference_dir: Optional[str] = None) -> LoadedUpperPackage:
    source_dir = os.path.abspath(source_dir)
    if not os.path.isdir(source_dir):
        raise CompileError(f"源码目录不存在: {source_dir}")

    upper_path = os.path.join(source_dir, "__upper__.py")
    if not os.path.isfile(upper_path):
        raise CompileError(f"源码包缺少 __upper__.py: {source_dir}")

    blueprint = _exec_upper_file(upper_path)
    resolved_reference = os.path.abspath(reference_dir) if reference_dir else _default_reference_dir(source_dir)
    resolver = MetaResolver(reference_dir=resolved_reference if os.path.isdir(resolved_reference) else "")

    graph_specs: List[GraphSpec] = []
    for file_name in sorted(os.listdir(source_dir)):
        if not file_name.endswith(".py") or file_name in {"__upper__.py", "__init__.py"}:
            continue
        if file_name.endswith("_meta.py"):
            continue
        if not (file_name.startswith("fn_") or file_name.startswith("evt_")):
            continue

        file_path = os.path.join(source_dir, file_name)
        graph_specs.append(_parse_graph_file(file_path, resolver))

    return LoadedUpperPackage(
        source_dir=source_dir,
        blueprint=blueprint,
        graphs=graph_specs,
        reference_dir=resolved_reference if os.path.isdir(resolved_reference) else "",
        meta_resolver=resolver,
    )


def _exec_upper_file(path: str) -> Blueprint:
    package_name = f"_upper_pkg_{uuid.uuid4().hex}"
    spec = importlib.util.spec_from_file_location(package_name, path)
    if spec is None or spec.loader is None:
        raise CompileError(f"无法创建 import spec: {path}")

    module = importlib.util.module_from_spec(spec)
    sys.modules[package_name] = module
    try:
        spec.loader.exec_module(module)
        bp = getattr(module, "bp", None)
        if not isinstance(bp, Blueprint):
            raise CompileError(f"__upper__.py 未定义 Blueprint 实例 'bp': {path}")
        return bp
    finally:
        sys.modules.pop(package_name, None)


def _default_reference_dir(source_dir: str) -> str:
    package_name = os.path.basename(os.path.abspath(source_dir))
    project_dir = os.path.abspath(os.path.join(source_dir, "..", ".."))
    return os.path.join(project_dir, "ExportedBlueprints", "bpy", package_name)


def _parse_graph_file(path: str, resolver: MetaResolver) -> GraphSpec:
    with open(path, "r", encoding="utf-8") as handle:
        source = handle.read()

    tree = ast.parse(source, filename=path)
    func_def: Optional[ast.FunctionDef] = None
    graph_info: Optional[Dict[str, Any]] = None
    for stmt in tree.body:
        if isinstance(stmt, ast.FunctionDef):
            info = _parse_graph_decorator(stmt, path)
            if info is not None:
                func_def = stmt
                graph_info = info
                break

    if func_def is None or graph_info is None:
        raise CompileError("graph 文件缺少 @bp_function / @bp_event 装饰的 graph() 函数", file_path=path)

    inputs: List[Tuple[str, str]] = []
    if graph_info["kind"] == "function":
        for arg in func_def.args.args:
            if arg.arg == "self":
                continue
            annotation = parse_annotation(arg.annotation)
            if annotation is None:
                raise CompileError(
                    f"函数参数缺少类型注解: {arg.arg}",
                    file_path=path,
                    line=arg.lineno,
                    column=arg.col_offset,
                    graph_name=str(graph_info["name"]),
                )
            inputs.append((arg.arg, annotation))

    outputs = [(str(name), str(type_str)) for name, type_str in graph_info.get("outputs", [])]

    return GraphSpec(
        kind=graph_info["kind"],
        graph_name=graph_info["name"],
        file_path=path,
        func_def=func_def,
        inputs=inputs,
        outputs=outputs,
        pure=bool(graph_info.get("pure", False)),
        source=source,
        meta=resolver.load_graph_meta(path),
        entry=graph_info.get("entry"),
    )


def _parse_graph_decorator(func_def: ast.FunctionDef, file_path: str) -> Optional[Dict[str, Any]]:
    for decorator in func_def.decorator_list:
        if not isinstance(decorator, ast.Call) or not isinstance(decorator.func, ast.Name):
            continue

        if decorator.func.id == "bp_function":
            info: Dict[str, Any] = {
                "kind": "function",
                "name": _default_graph_name(file_path),
                "pure": False,
                "outputs": [],
            }
            for keyword in decorator.keywords:
                if keyword.arg in {"name", "pure", "outputs"}:
                    info[keyword.arg] = ast.literal_eval(keyword.value)
            return info

        if decorator.func.id == "bp_event":
            info = {
                "kind": "event_graph",
                "name": "EventGraph",
                "entry": None,
            }
            for keyword in decorator.keywords:
                if keyword.arg in {"name", "entry"}:
                    info[keyword.arg] = ast.literal_eval(keyword.value)
            return info

    return None


def _default_graph_name(file_path: str) -> str:
    base = os.path.splitext(os.path.basename(file_path))[0]
    if base.startswith("fn_"):
        return base[3:]
    if base.startswith("evt_"):
        return base[4:]
    return base
