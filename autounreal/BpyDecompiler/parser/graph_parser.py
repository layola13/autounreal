from __future__ import annotations

import ast
import importlib.util
from pathlib import Path
from typing import Any

from ..ir import EdgeIR, GraphCstIR, GraphIR, NodeIR, PinRefIR
from .ast_utils import attr_chain, clean_pin, keyword_dict, literal, positional


def parse_graph_file(path: str | Path) -> GraphIR:
    graph_path = Path(path).resolve()
    source = graph_path.read_text(encoding="utf-8")
    module = ast.parse(source, filename=str(graph_path))
    meta, meta_text = _load_meta(graph_path)
    graph = GraphIR(
        stem=graph_path.name[:-6] if graph_path.name.endswith(".bp.py") else graph_path.stem,
        graph_name=_default_graph_name(graph_path),
        kind="function",
        source_path=graph_path,
        meta=meta,
    )

    with_item = _find_graph_with(module)
    if with_item is not None:
        _parse_graph_context(with_item, graph)
        _capture_graph_cst(source, with_item, graph)

    for stmt in _graph_body(module):
        if isinstance(stmt, ast.Assign):
            _parse_node_assign(stmt, graph)
        elif isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.BinOp):
            _parse_edge(stmt.value, graph)
        elif isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.Call):
            _parse_pin_default(stmt.value, graph)

    _merge_meta_props(graph)
    return graph


def _capture_graph_cst(source: str, with_stmt: ast.With, graph: GraphIR) -> None:
    context_expr = ast.get_source_segment(source, with_stmt.items[0].context_expr) or "bp.function(\"Graph\")"
    cst = GraphCstIR(
        file_name=graph.source_path.name,
        context_expr=context_expr,
        is_sidecar=graph.source_path.name.startswith("other_"),
        has_sidecar_loader="def _load_sidecar_graph" in source,
        meta_text=str(graph.meta.get("_meta_text") or ""),
        blueprint_call=_find_nested_blueprint_call(source, with_stmt),
        footer=_find_sidecar_footer(source) if graph.source_path.name.startswith("other_") else [],
        connections=_find_sidecar_connections(source, with_stmt) if graph.source_path.name.startswith("other_") else [],
    )
    mode = "nodes"
    for stmt in with_stmt.body:
        segment = ast.get_source_segment(source, stmt)
        if not segment:
            continue
        line = segment.strip()
        if not line or line.startswith("#"):
            continue
        if isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.BinOp):
            src = _pin_ref(stmt.value.left)
            dst = _pin_ref(stmt.value.right)
            is_exec = _cst_edge_is_exec(line, src, dst)
            if is_exec:
                cst.exec_edges.append(line)
                mode = "exec"
            else:
                cst.data_edges.append(line)
                mode = "data"
        elif mode == "nodes":
            cst.nodes.append(line)
        elif isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.Call):
            cst.nodes.append(line)
    graph.meta.setdefault("_cst", cst)


def _find_sidecar_connections(source: str, with_stmt: ast.With) -> list[str]:
    lines = source.splitlines()
    start = getattr(with_stmt, "lineno", 1) - 1
    end = getattr(with_stmt, "end_lineno", len(lines))
    body = lines[start:end]
    connections: list[str] = []
    in_connections = False
    for line in body:
        stripped = line.strip()
        if stripped == "# Connections":
            in_connections = True
            continue
        if in_connections:
            if not stripped:
                continue
            if stripped.startswith("#"):
                break
            connections.append(stripped)
    return connections


def _find_sidecar_footer(source: str) -> list[str]:
    lines = source.splitlines()
    footer: list[str] = []
    in_footer = False
    for line in lines:
        stripped = line.strip()
        if stripped == 'graph = bp.to_dict()["graphs"][0]':
            in_footer = True
        if in_footer:
            if stripped == "GRAPH = _build_graph()":
                break
            if stripped:
                footer.append(line)
    return footer


def _find_nested_blueprint_call(source: str, with_stmt: ast.With) -> str:
    for node in ast.walk(ast.parse(source)):
        if isinstance(node, ast.Assign) and len(node.targets) == 1:
            target = node.targets[0]
            if isinstance(target, ast.Name) and target.id == "bp":
                segment = ast.get_source_segment(source, node)
                if segment:
                    return segment.strip()
    return ""


def _cst_edge_is_exec(line: str, src: PinRefIR | None, dst: PinRefIR | None) -> bool:
    if src and src.pin.lower().startswith("then_"):
        return True
    if ".case(" in line or "cast_failed" in line:
        return True
    return bool(src and dst and (_is_exec_pin(src.pin) or _is_exec_pin(dst.pin)))


def _load_meta(graph_path: Path) -> tuple[dict[str, Any], str]:
    meta_path = graph_path.with_name(graph_path.name[:-6] + "_meta.py")
    if not meta_path.is_file():
        return {}, ""
    meta_text = meta_path.read_text(encoding="utf-8")
    spec = importlib.util.spec_from_file_location(f"_bpy_decompile_meta_{graph_path.stem}", meta_path)
    if spec is None or spec.loader is None:
        return {"_meta_text": meta_text}, meta_text
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    value = getattr(module, "META", {})
    meta = value if isinstance(value, dict) else {}
    meta["_meta_text"] = meta_text
    return meta, meta_text


def _find_graph_with(module: ast.Module) -> ast.With | None:
    for stmt in ast.walk(module):
        if isinstance(stmt, ast.With):
            for item in stmt.items:
                call = item.context_expr
                if isinstance(call, ast.Call) and isinstance(call.func, ast.Attribute):
                    if getattr(call.func.value, "id", None) == "bp" and call.func.attr in {"function", "event_graph", "macro"}:
                        return stmt
    return None


def _graph_body(module: ast.Module) -> list[ast.stmt]:
    with_stmt = _find_graph_with(module)
    return list(with_stmt.body) if with_stmt is not None else []


def _parse_graph_context(with_stmt: ast.With, graph: GraphIR) -> None:
    call = with_stmt.items[0].context_expr
    if not isinstance(call, ast.Call) or not isinstance(call.func, ast.Attribute):
        return
    method = call.func.attr
    args = positional(call)
    kwargs = keyword_dict(call)
    graph.kind = "event_graph" if method == "event_graph" else "function"
    if args and isinstance(args[0], str):
        graph.graph_name = args[0]
    graph.context_kwargs = dict(kwargs)
    graph.inputs = list(kwargs.get("inputs") or [])
    graph.outputs = list(kwargs.get("outputs") or [])
    graph.pure = bool(kwargs.get("pure") or False)


def _parse_node_assign(stmt: ast.Assign, graph: GraphIR) -> None:
    if len(stmt.targets) != 1 or not isinstance(stmt.targets[0], ast.Name):
        return
    name = stmt.targets[0].id
    if not isinstance(stmt.value, ast.Call) or not isinstance(stmt.value.func, ast.Attribute):
        return
    func = stmt.value.func
    if getattr(func.value, "id", None) != "g":
        return
    method = func.attr
    args = positional(stmt.value)
    kwargs = keyword_dict(stmt.value)
    kind = method
    target = str(args[0]) if args and args[0] is not None else None
    if method == "entry":
        graph.entry = name
    graph.nodes[name] = NodeIR(name=name, kind=kind, target=target, args=args, kwargs=kwargs)


def _parse_pin_default(call: ast.Call, graph: GraphIR) -> None:
    if not isinstance(call.func, ast.Attribute) or call.func.attr != "pin":
        return
    owner = call.func.value
    if not isinstance(owner, ast.Name) or owner.id not in graph.nodes:
        return
    args = positional(call)
    if len(args) >= 2 and isinstance(args[0], str):
        graph.nodes[owner.id].defaults[clean_pin(args[0])] = args[1]


def _parse_edge(binop: ast.BinOp, graph: GraphIR) -> None:
    if not isinstance(binop.op, ast.RShift):
        return
    src = _pin_ref(binop.left)
    dst = _pin_ref(binop.right)
    if src and dst:
        is_exec = (
            src.node in graph.nodes and graph.nodes[src.node].kind in {"switch_enum", "switch_int"}
            or src.pin.lower().startswith("then_")
            or src.pin.lower() in {"cast_failed", "cast failed"}
            or _is_exec_pin(src.pin)
            or _is_exec_pin(dst.pin)
        )
        graph.edges.append(EdgeIR(src=src, dst=dst, is_exec=is_exec))


def _pin_ref(node: ast.AST) -> PinRefIR | None:
    if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == "case":
        owner = node.func.value
        if isinstance(owner, ast.Name) and node.args:
            value = literal(node.args[0])
            return PinRefIR(node=owner.id, pin=str(value))
    chain = attr_chain(node)
    if not chain or len(chain) < 2:
        return None
    return PinRefIR(node=chain[0], pin=clean_pin(chain[-1]))


def _is_exec_pin(pin: str) -> bool:
    return pin in {"execute", "then", "true", "false", "true_", "false_", "exec"}


def _merge_meta_props(graph: GraphIR) -> None:
    props = graph.meta.get("node_props") if isinstance(graph.meta, dict) else None
    if isinstance(props, dict):
        for node_name, node_props in props.items():
            if node_name in graph.nodes and isinstance(node_props, dict):
                graph.nodes[node_name].props.update(node_props)


def _default_graph_name(path: Path) -> str:
    name = path.name[:-6] if path.name.endswith(".bp.py") else path.stem
    if name.startswith("fn_"):
        return name[3:]
    if name.startswith("evt_"):
        return name[4:]
    if name.startswith("macro_"):
        return name[6:]
    return name

