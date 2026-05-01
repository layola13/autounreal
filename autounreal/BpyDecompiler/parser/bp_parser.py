from __future__ import annotations

import ast
from pathlib import Path
from typing import Any

from ..ir import BlueprintIR, Diagnostic, RootCstIR
from .ast_utils import keyword_dict, literal, positional
from .graph_parser import parse_graph_file


def parse_blueprint_package(source_dir: str | Path) -> BlueprintIR:
    root = Path(source_dir).resolve()
    root_file = root / "__bp__.bp.py"
    if not root_file.is_file():
        raise FileNotFoundError(f"ExportBpy package missing __bp__.bp.py: {root}")

    module = ast.parse(root_file.read_text(encoding="utf-8"), filename=str(root_file))
    bp = BlueprintIR(
        source_dir=root,
        name=root.name,
        path="",
        parent="",
        bp_type="Normal",
    )

    for node in module.body:
        if isinstance(node, ast.Assign):
            _parse_assignment(node, bp)
        elif isinstance(node, ast.Expr) and isinstance(node.value, ast.Call):
            _parse_root_call(node.value, bp)

    if not bp.path:
        bp.diagnostics.append(Diagnostic("warning", "Blueprint(...) assignment not found in root file"))

    for stem in bp.graph_stems:
        graph_path = root / f"{stem}.bp.py"
        if not graph_path.is_file():
            bp.diagnostics.append(Diagnostic("warning", f"Graph module not found: {stem}"))
            continue
        try:
            bp.graphs.append(parse_graph_file(graph_path))
        except Exception as exc:
            bp.diagnostics.append(Diagnostic("error", f"Failed to parse graph {stem}: {exc}", graph=stem))

    if not bp.graph_stems:
        for graph_path in sorted(root.glob("*.bp.py")):
            if graph_path.name == "__bp__.bp.py":
                continue
            try:
                bp.graphs.append(parse_graph_file(graph_path))
            except Exception as exc:
                bp.diagnostics.append(Diagnostic("error", f"Failed to parse graph {graph_path.stem}: {exc}", graph=graph_path.stem))

    _capture_root_cst(root_file, bp)
    seen_cst_files = set()
    for graph in bp.graphs:
        cst = graph.meta.get("_cst") if isinstance(graph.meta, dict) else None
        if cst is not None:
            bp.roundtrip_cst.graphs.append(cst)
            seen_cst_files.add(cst.file_name)
    for graph_path in sorted(root.glob("other_*.bp.py")):
        if graph_path.name in seen_cst_files:
            continue
        try:
            graph = parse_graph_file(graph_path)
        except Exception as exc:
            bp.diagnostics.append(Diagnostic("error", f"Failed to parse sidecar graph {graph_path.stem}: {exc}", graph=graph_path.stem))
            continue
        cst = graph.meta.get("_cst") if isinstance(graph.meta, dict) else None
        if cst is not None:
            bp.roundtrip_cst.graphs.append(cst)
            seen_cst_files.add(cst.file_name)
    return bp


def _parse_assignment(node: ast.Assign, bp: BlueprintIR) -> None:
    if len(node.targets) != 1:
        return
    target = node.targets[0]
    if not isinstance(target, ast.Name):
        return

    if target.id == "_GRAPH_MODULES" and isinstance(node.value, ast.List):
        for item in node.value.elts:
            if isinstance(item, ast.Call) and item.args:
                stem = literal(item.args[0])
                if isinstance(stem, str):
                    bp.graph_stems.append(stem)
        return

    if target.id == "bp" and isinstance(node.value, ast.Call):
        if getattr(node.value.func, "id", None) != "Blueprint":
            return
        kwargs = keyword_dict(node.value)
        bp.path = str(kwargs.get("path") or "")
        bp.parent = str(kwargs.get("parent") or "")
        bp.bp_type = str(kwargs.get("bp_type") or "Normal")


def _parse_root_call(call: ast.Call, bp: BlueprintIR) -> None:
    func = call.func
    if not isinstance(func, ast.Attribute):
        return
    if getattr(func.value, "id", None) != "bp":
        return
    method = func.attr
    args = positional(call)
    kwargs = keyword_dict(call)
    if method == "var" and len(args) >= 2:
        bp.variables.append({"name": args[0], "type": args[1], **kwargs})
    elif method == "default" and len(args) >= 2:
        bp.defaults.append((str(args[0]), args[1]))
    elif method == "component" and args:
        bp.components.append({"name": args[0], **kwargs})
    elif method == "interface" and args:
        bp.interfaces.append(str(args[0]))



def _capture_root_cst(root_file: Path, bp: BlueprintIR) -> None:
    source = root_file.read_text(encoding="utf-8")
    lines = source.splitlines()
    root = RootCstIR()
    section: str | None = None
    index = 0
    while index < len(lines):
        line = lines[index]
        stripped = line.strip()
        if stripped.startswith("_load_graph_module("):
            stem = stripped.split("(", 1)[1].split(")", 1)[0].strip().strip('"\'')
            root.graph_modules.append(stem)
        elif stripped == "bp = Blueprint(":
            block = [line]
            index += 1
            while index < len(lines):
                block.append(lines[index])
                if lines[index].strip() == ")":
                    break
                index += 1
            root.blueprint_call = "\n".join(block)
        elif line.startswith("# ── Variables"):
            section = "variables"
        elif line.startswith("# ── Class Defaults"):
            section = "defaults"
        elif line.startswith("# ── Inherited Component Defaults"):
            section = "inherited_component_defaults"
        elif line.startswith("# ── Components"):
            section = "components"
        elif line.startswith("# ── Interfaces"):
            section = "interfaces"
        elif line.startswith("# ── Event Dispatchers"):
            section = "event_dispatchers"
        elif stripped.startswith("bp.") and section and not stripped.startswith("bp.build("):
            getattr(root, section).append(line)
        index += 1
    bp.roundtrip_cst.root = root
