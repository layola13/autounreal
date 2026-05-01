from __future__ import annotations

import json
import re
import shutil
from pathlib import Path
from typing import Iterable

from ..ir import BlueprintIR, GraphIR
from ..lifter.graph_lifter import LiftedGraph


def emit_upper_package(bp: BlueprintIR, lifted_graphs: Iterable[LiftedGraph], output_dir: str | Path) -> Path:
    lifted_list = list(lifted_graphs)
    out = Path(output_dir).resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)
    _write_dynamic_function_specs(bp, out)
    _write_blueprint_meta(bp, out)
    (out / "README.md").write_text(_readme(bp), encoding="utf-8", newline="\n")
    (out / "__upper__.py").write_text(_render_upper(bp), encoding="utf-8", newline="\n")
    _write_roundtrip_manifest(bp, out)
    local_modules = {item.graph.graph_name: Path(_graph_file_name(item.graph)).stem for item in lifted_list if item.graph.kind == "function"}
    for lifted in lifted_list:
        file_name = _graph_file_name(lifted.graph)
        (out / file_name).write_text(_render_graph(lifted, local_modules), encoding="utf-8", newline="\n")
    diagnostics = _render_diagnostics(bp, lifted_list)
    if diagnostics:
        (out / "DECOMPILE_NOTES.md").write_text(diagnostics, encoding="utf-8", newline="\n")
    return out


def _readme(bp: BlueprintIR) -> str:
    return (
        f"# {bp.name} upper Python\n\n"
        "This package is the lossless compile-back representation used by py -> bpy. "
        "It intentionally keeps ExportBpy/std decorators and graph sidecar modules so roundtrip output stays byte-stable.\n\n"
        "For human review, inheritance, class defaults, and component-style source, open the sibling *_human/blueprint.py output instead.\n"
    )


def _write_roundtrip_manifest(bp: BlueprintIR, out: Path) -> None:
    cst = bp.roundtrip_cst
    graph_meta_by_file = {graph.source_path.name: _jsonable_graph_meta(graph) for graph in bp.graphs}
    payload = {
        "format": "BpyDecompiler.ExportBpyCst.v2",
        "source_name": bp.name,
        "root": {
            "graph_modules": cst.root.graph_modules,
            "blueprint_call": cst.root.blueprint_call,
            "variables": cst.root.variables,
            "defaults": cst.root.defaults,
            "inherited_component_defaults": cst.root.inherited_component_defaults,
            "components": cst.root.components,
            "interfaces": cst.root.interfaces,
            "event_dispatchers": cst.root.event_dispatchers,
        },
        "graphs": [
            {
                "file_name": graph.file_name,
                "context_expr": graph.context_expr,
                "is_sidecar": graph.is_sidecar,
                "has_sidecar_loader": graph.has_sidecar_loader,
                "blueprint_call": graph.blueprint_call,
                "footer": graph.footer,
                "connections": graph.connections,
                "meta": graph_meta_by_file.get(graph.file_name, {}),
                "nodes": graph.nodes,
                "data_edges": graph.data_edges,
                "exec_edges": graph.exec_edges,
            }
            for graph in cst.graphs
        ],
    }
    (out / "__roundtrip_bpy__.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )



def _jsonable_graph_meta(graph) -> dict:
    meta = getattr(graph, "meta", {}) or {}
    return {key: value for key, value in meta.items() if key != "_cst"}

def _write_blueprint_meta(bp: BlueprintIR, out: Path) -> None:
    payload = {
        "path": bp.path,
        "parent": bp.parent,
        "bp_type": bp.bp_type,
        "variables": bp.variables,
        "defaults": [[key, value] for key, value in bp.defaults],
        "components": bp.components,
        "interfaces": bp.interfaces,
    }
    (out / "__blueprint_meta__.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def _collect_dynamic_function_specs(bp: BlueprintIR) -> dict[str, dict[str, object]]:
    specs: dict[str, dict[str, object]] = {}
    for graph in bp.graphs:
        for node in graph.nodes.values():
            if node.kind == "call" and node.target:
                target_ref = str(node.target)
                py_name = _callable_name(target_ref, node.name)
                node_class = str(node.kwargs.get("node_class") or "K2Node_CallFunction")
                extra_props = {}
            elif node.kind == "node" and node.kwargs.get("type") and node.kwargs.get("type") not in {"EdGraphNode_Comment", "Knot", "MakeArray", "Sequence"}:
                target_ref = str(node.kwargs.get("type"))
                py_name = _node_callable_name(node, target_ref)
                node_class = target_ref
                extra_props = dict(node.props)
                if target_ref == "MacroInstance" and isinstance(node.kwargs.get("target_type"), str):
                    extra_props.setdefault("MacroGraph", node.kwargs.get("target_type"))
            elif node.kind == "message" and node.target:
                target_ref = str(node.target)
                py_name = _safe_param(target_ref.replace("::", "_"))
                node_class = "K2Node_Message"
                extra_props = dict(node.props)
            else:
                continue
            if not py_name or py_name in specs:
                continue
            input_pins = sorted({edge.dst.pin for edge in graph.edges if not edge.is_exec and edge.dst.node == node.name and edge.dst.pin not in {"execute", "self"}})
            output_pins = sorted({edge.src.pin for edge in graph.edges if not edge.is_exec and edge.src.node == node.name})
            if not output_pins:
                output_pins = ["ReturnValue"]
            specs[py_name] = {
                "ue_ref": target_ref,
                "params": { _safe_param(pin): pin for pin in input_pins },
                "returns": output_pins,
                "return_types": [_output_type(graph, node.name, pin) for pin in output_pins],
                "pure": _node_is_pure_call(graph, node.name),
                "param_order": [_safe_param(pin) for pin in input_pins],
                "node_class": node_class,
                "extra_props": extra_props,
            }
    return specs




def _node_callable_name(node, target_ref: str) -> str:
    if target_ref == "MacroInstance":
        name = node.kwargs.get("name") or node.props.get("Name")
        if isinstance(name, str) and name:
            return _safe_param(name)
    return _safe_param(target_ref)


def _node_is_pure_call(graph: GraphIR, node_name: str) -> bool:
    if graph.pure:
        return True
    return node_name not in {edge.dst.node for edge in graph.edges if edge.is_exec} and node_name not in {edge.src.node for edge in graph.edges if edge.is_exec}
def _output_type(graph: GraphIR, node_name: str, pin: str) -> str:
    output_types = graph.meta.get("output_pin_types") if isinstance(graph.meta, dict) else None
    if isinstance(output_types, dict):
        value = output_types.get(f"{node_name}.{pin}")
        if isinstance(value, str) and value:
            return value
    return "unknown"
def _callable_name(target: str, fallback: str) -> str:
    if "::" in target:
        return _safe_param(target.replace("::", "_"))
    return _safe_param(target or fallback)


def _write_dynamic_function_specs(bp: BlueprintIR, out: Path) -> None:
    specs = _collect_dynamic_function_specs(bp)
    payload = {
        name: {
            "ue_ref": spec["ue_ref"],
            "params": spec["params"],
            "returns": list(spec["returns"]),
            "return_types": list(spec["return_types"]),
            "pure": bool(spec["pure"]),
            "node_class": spec["node_class"],
            "extra_props": spec.get("extra_props", {}),
            "param_order": list(spec["param_order"]),
        }
        for name, spec in sorted(specs.items())
    }
    (out / "__dynamic_function_specs__.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def _render_dynamic_function_map(bp: BlueprintIR) -> list[str]:
    if not _collect_dynamic_function_specs(bp):
        return []
    return [
        "",
        "# Compile-back support data lives outside the readable Python body.",
        "from Plugins.autounreal.autounreal.BpyDecompiler.runtime_compile_patches import load_dynamic_function_specs as _load_dynamic_function_specs",
        "_load_dynamic_function_specs(__file__)",
    ]
def _render_upper(bp: BlueprintIR) -> str:
    lines = [
        "from Plugins.autounreal.autounreal.BpyDecompiler.runtime_compile_patches import apply as _apply_bpydecompiler_compile_patches",
        "from Plugins.autounreal.autounreal.BpyDecompiler.runtime_compile_patches import load_blueprint_meta as _load_blueprint_meta",
        "from Plugins.autounreal.autounreal.BpyDecompiler.runtime_compile_patches import load_dynamic_function_specs as _load_dynamic_function_specs",
        "",
        "_apply_bpydecompiler_compile_patches()",
        "_load_dynamic_function_specs(__file__)",
        "",
        "bp = _load_blueprint_meta(__file__)",
        "",
        "bp.build()",
        "",
    ]
    return "\n".join(lines)


def _render_graph(lifted: LiftedGraph, local_modules: dict[str, str]) -> str:
    graph = lifted.graph
    body = _ensure_statement_blocks(lifted.lines or ["return None"])
    lines = [
        "from Plugins.autounreal.autounreal.BpyDecompiler import std",
    ]
    lines.extend(_local_import_lines(lifted, local_modules))
    lines.extend(["", ""])
    lines.extend(_decorator_lines(graph))
    lines.extend(_graph_def_lines(graph))
    for line in body:
        for physical_line in _wrap_long_line(line):
            lines.append("    " + physical_line)
    lines.append("")
    return "\n".join(lines)


def _local_import_lines(lifted: LiftedGraph, local_modules: dict[str, str]) -> list[str]:
    used = _used_local_functions(lifted.lines, local_modules)
    if not used:
        return []
    lines = [""]
    for function_name in used:
        module_name = local_modules[function_name]
        alias = _safe_param(function_name)
        lines.extend([
            f"def {alias}(*args, **kwargs):",
            f"    from .{module_name} import graph as _target",
            "    return _target(*args, **kwargs)",
            "",
        ])
    return lines[:-1]


def _used_local_functions(lines: list[str], local_modules: dict[str, str]) -> list[str]:
    source = "\n".join(lines)
    used = []
    for function_name in sorted(local_modules, key=len, reverse=True):
        if re.search(rf"(?<![\w.]){re.escape(function_name)}\(", source):
            used.append(function_name)
    return used


def _wrap_long_line(line: str, limit: int = 160) -> list[str]:
    if len(line) <= limit:
        return [line]
    indent = line[: len(line) - len(line.lstrip(" "))]
    stripped = line[len(indent):]
    if stripped.endswith(":"):
        if_split = _split_if_condition(stripped)
        if if_split is None:
            return [line]
        result = [indent + "if ("]
        result.extend(_wrap_nested_item(indent + "    ", if_split, limit))
        result.append(indent + "):")
        return result
    literal_split = _split_assignment_literal(stripped)
    if literal_split is not None:
        prefix, opener, items, closer = literal_split
        result = [indent + prefix + opener]
        for item in items:
            result.extend(_wrap_nested_item(indent + "    ", item + ",", limit))
        result[-1] = result[-1].rstrip(",")
        result.append(indent + closer)
        return result
    bool_split = _split_return_boolop(stripped)
    if bool_split is not None:
        op, parts = bool_split
        result = [indent + "return ("]
        for index, part in enumerate(parts):
            suffix = f" {op}" if index < len(parts) - 1 else ""
            result.extend(_with_suffix(_wrap_nested_item(indent + "    ", part, limit), suffix))
        result.append(indent + ")")
        return result
    compare_split = _split_return_compare(stripped)
    if compare_split is not None:
        left, op, right = compare_split
        result = [indent + "return ("]
        result.extend(_with_suffix(_wrap_nested_item(indent + "    ", left, limit), f" {op}"))
        result.extend(_wrap_nested_item(indent + "    ", right, limit))
        result.append(indent + ")")
        return result
    split = _split_outer_call(stripped)
    if split is None:
        return [line]
    head, args, tail = split
    if not args:
        return [line]
    result = [indent + head + "("]
    for arg in args:
        result.extend(_wrap_nested_item(indent + "    ", arg + ",", limit))
    result[-1] = result[-1].rstrip(",")
    result.append(indent + ")" + tail)
    return result


def _with_suffix(lines: list[str], suffix: str) -> list[str]:
    if suffix and lines:
        lines = list(lines)
        lines[-1] = lines[-1] + suffix
    return lines


def _split_if_condition(text: str) -> str | None:
    if not text.startswith("if ") or not text.endswith(":"):
        return None
    condition = text[3:-1].strip()
    if condition.startswith("(") and condition.endswith(")") and _matching_close_paren(condition, 0) == len(condition) - 1:
        condition = condition[1:-1].strip()
    return condition


def _split_return_compare(text: str) -> tuple[str, str, str] | None:
    if not text.startswith("return "):
        return None
    expr = text[len("return "):].strip()
    while expr.startswith("(") and expr.endswith(")") and _matching_close_paren(expr, 0) == len(expr) - 1:
        expr = expr[1:-1].strip()
    split = _split_top_level_compare(expr)
    if split is None:
        return None
    left, op, right = split
    if len(left) < 80 and len(right) < 80:
        return None
    return left, op, right


def _split_top_level_ifexp(text: str) -> tuple[str, str, str] | None:
    depth = 0
    quote = ""
    escape = False
    if_index: int | None = None
    else_index: int | None = None
    index = 0
    while index < len(text):
        char = text[index]
        if quote:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == quote:
                quote = ""
            index += 1
            continue
        if char in {'"', "'"}:
            quote = char
            index += 1
            continue
        if char in "([{":
            depth += 1
            index += 1
            continue
        if char in ")]}":
            depth -= 1
            index += 1
            continue
        if depth == 0 and text.startswith(" if ", index):
            if_index = index
            index += 4
            continue
        if depth == 0 and text.startswith(" else ", index):
            else_index = index
            break
        index += 1
    if if_index is None or else_index is None or else_index <= if_index:
        return None
    body = text[:if_index].strip()
    test = text[if_index + 4:else_index].strip()
    orelse = text[else_index + 6:].strip()
    if not body or not test or not orelse:
        return None
    return body, test, orelse


def _split_top_level_compare(text: str) -> tuple[str, str, str] | None:
    operators = (" not in ", " is not ", " in ", " is ", ">=", "<=", "!=", "==", ">", "<")
    depth = 0
    quote = ""
    escape = False
    index = 0
    while index < len(text):
        char = text[index]
        if quote:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == quote:
                quote = ""
            index += 1
            continue
        if char in {'"', "'"}:
            quote = char
            index += 1
            continue
        if char in "([{":
            depth += 1
            index += 1
            continue
        if char in ")]}":
            depth -= 1
            index += 1
            continue
        if depth == 0:
            for op in operators:
                if text.startswith(op, index):
                    left = text[:index].strip()
                    right = text[index + len(op):].strip()
                    if not left or not right:
                        return None
                    return left, op.strip(), right
        index += 1
    return None


def _split_nested_boolop(text: str) -> tuple[str, list[str]] | None:
    expr = text.strip()
    while expr.startswith("(") and expr.endswith(")") and _matching_close_paren(expr, 0) == len(expr) - 1:
        expr = expr[1:-1].strip()
    for op in ("and", "or"):
        parts = _split_top_level_boolop(expr, op)
        if len(parts) > 1:
            return op, parts
    return None


def _split_return_boolop(text: str) -> tuple[str, list[str]] | None:
    if not text.startswith("return "):
        return None
    expr = text[len("return "):].strip()
    while expr.startswith("(") and expr.endswith(")") and _matching_close_paren(expr, 0) == len(expr) - 1:
        expr = expr[1:-1].strip()
    for op in ("and", "or"):
        parts = _split_top_level_boolop(expr, op)
        if len(parts) > 1:
            return op, parts
    return None


def _split_top_level_boolop(text: str, op: str) -> list[str]:
    parts: list[str] = []
    start = 0
    depth = 0
    quote = ""
    escape = False
    token = f" {op} "
    index = 0
    while index < len(text):
        char = text[index]
        if quote:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == quote:
                quote = ""
            index += 1
            continue
        if char in {'"', "'"}:
            quote = char
            index += 1
            continue
        if char in "([{":
            depth += 1
            index += 1
            continue
        if char in ")]}":
            depth -= 1
            index += 1
            continue
        if depth == 0 and text.startswith(token, index):
            parts.append(text[start:index].strip())
            index += len(token)
            start = index
            continue
        index += 1
    if parts:
        parts.append(text[start:].strip())
    return [part for part in parts if part]


def _wrap_nested_item(indent: str, text: str, limit: int, depth: int = 0) -> list[str]:
    line = indent + text
    stripped = text.rstrip(",")
    comma = "," if text.endswith(",") else ""
    if len(line) <= limit or depth > 4:
        return [line]
    string_split = _split_long_keyword_string(indent, stripped, comma, limit)
    if string_split is not None:
        return string_split
    literal = _split_keyword_literal(stripped)
    if literal is not None:
        prefix, opener, items, closer = literal
        result = [indent + prefix + opener]
        for item in items:
            result.extend(_wrap_nested_item(indent + "    ", item + ",", limit, depth + 1))
        result[-1] = result[-1].rstrip(",")
        result.append(indent + closer + comma)
        return result
    bool_parts = _split_nested_boolop(stripped)
    if bool_parts is not None:
        op, parts = bool_parts
        result = [indent + "("]
        for index, part in enumerate(parts):
            suffix = f" {op}" if index < len(parts) - 1 else ""
            result.extend(_with_suffix(_wrap_nested_item(indent + "    ", part, limit, depth + 1), suffix))
        result.append(indent + ")" + comma)
        return result
    compare = _split_top_level_compare(stripped)
    if compare is not None:
        left, op, right = compare
        if len(left) > 80 or len(right) > 80:
            result = [indent + "("]
            result.extend(_with_suffix(_wrap_nested_item(indent + "    ", left, limit, depth + 1), f" {op}"))
            result.extend(_wrap_nested_item(indent + "    ", right, limit, depth + 1))
            result.append(indent + ")" + comma)
            return result
    ifexp = _split_top_level_ifexp(stripped)
    if ifexp is not None:
        body, test, orelse = ifexp
        if len(test) > 80:
            result = [indent + "("]
            result.extend(_with_suffix(_wrap_nested_item(indent + "    ", body, limit, depth + 1), " if"))
            result.extend(_with_suffix(_wrap_nested_item(indent + "    ", test, limit, depth + 1), " else"))
            result.extend(_wrap_nested_item(indent + "    ", orelse, limit, depth + 1))
            result.append(indent + ")" + comma)
            return result
    assignment = _split_keyword_expr(stripped)
    if assignment is not None:
        key, value = assignment
        split = _split_outer_call(value)
        if split is not None:
            head, args, tail = split
            result = [indent + key + "=" + head + "("]
            for arg in args:
                result.extend(_wrap_nested_item(indent + "    ", arg + ",", limit, depth + 1))
            result[-1] = result[-1].rstrip(",")
            result.append(indent + ")" + tail + comma)
            return result
    split = _split_outer_call(stripped)
    if split is None:
        return [line]
    head, args, tail = split
    if not args:
        return [line]
    result = [indent + head + "("]
    for arg in args:
        result.extend(_wrap_nested_item(indent + "    ", arg + ",", limit, depth + 1))
    result[-1] = result[-1].rstrip(",")
    result.append(indent + ")" + tail + comma)
    return result


def _split_keyword_expr(text: str) -> tuple[str, str] | None:
    if "=" not in text:
        return None
    key, value = text.split("=", 1)
    if not key.strip().isidentifier():
        return None
    return key, value.strip()


def _split_long_keyword_string(indent: str, text: str, comma: str, limit: int) -> list[str] | None:
    import ast as _ast
    split = _split_keyword_expr(text)
    if split is None:
        return None
    key, value = split
    if not (len(indent + text) > limit and len(value) >= 80):
        return None
    try:
        parsed = _ast.literal_eval(value)
    except Exception:
        return None
    if not isinstance(parsed, str) or len(parsed) < 80:
        return None
    chunks = _string_chunks(parsed, max(24, limit - len(indent) - 8))
    result = [indent + key + "=("]
    for chunk in chunks:
        result.append(indent + "    " + repr(chunk))
    result.append(indent + ")" + comma)
    return result


def _string_chunks(text: str, max_len: int) -> list[str]:
    chunks: list[str] = []
    current = text
    while len(current) > max_len:
        split_at = current.rfind(",", 0, max_len)
        if split_at < max_len // 2:
            split_at = max_len
        else:
            split_at += 1
        chunks.append(current[:split_at])
        current = current[split_at:]
    if current:
        chunks.append(current)
    return chunks


def _split_keyword_literal(text: str) -> tuple[str, str, list[str], str] | None:
    if "=" not in text:
        return None
    key, value = text.split("=", 1)
    if not key.strip().isidentifier():
        return None
    value = value.strip()
    if value.startswith("[") and value.endswith("]"):
        return key + "=", "[", _split_top_level_commas(value[1:-1]), "]"
    if value.startswith("{") and value.endswith("}"):
        return key + "=", "{", _split_top_level_commas(value[1:-1]), "}"
    return None


def _split_assignment_literal(text: str) -> tuple[str, str, list[str], str] | None:
    for prefix in ("return ",):
        if text.startswith(prefix):
            rest = text[len(prefix):]
            if rest.startswith("{") and rest.endswith("}"):
                return prefix, "{", _split_top_level_commas(rest[1:-1]), "}"
            if rest.startswith("[") and rest.endswith("]"):
                return prefix, "[", _split_top_level_commas(rest[1:-1]), "]"
    if "= {" in text and text.endswith("}"):
        head, rest = text.split("= ", 1)
        if rest.startswith("{"):
            return head + "= ", "{", _split_top_level_commas(rest[1:-1]), "}"
    if "= [" in text and text.endswith("]"):
        head, rest = text.split("= ", 1)
        if rest.startswith("["):
            return head + "= ", "[", _split_top_level_commas(rest[1:-1]), "]"
    return None


def _split_outer_call(text: str) -> tuple[str, list[str], str] | None:
    open_index = text.find("(")
    if open_index < 0:
        return None
    close_index = _matching_close_paren(text, open_index)
    if close_index < 0:
        return None
    tail = text[close_index + 1 :]
    if tail and not tail.startswith((".", "[")):
        return None
    inner = text[open_index + 1 : close_index]
    args = _split_top_level_commas(inner)
    if not args:
        return None
    return text[:open_index], args, tail


def _matching_close_paren(text: str, open_index: int) -> int:
    depth = 0
    quote = ""
    escape = False
    for index, char in enumerate(text[open_index:], start=open_index):
        if quote:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == quote:
                quote = ""
            continue
        if char in {'"', "'"}:
            quote = char
            continue
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
            if depth == 0:
                return index
    return -1


def _split_top_level_commas(text: str) -> list[str]:
    parts: list[str] = []
    start = 0
    depth = 0
    quote = ""
    escape = False
    for index, char in enumerate(text):
        if quote:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == quote:
                quote = ""
            continue
        if char in {'"', "'"}:
            quote = char
            continue
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        elif char == "," and depth == 0:
            parts.append(text[start:index].strip())
            start = index + 1
    tail = text[start:].strip()
    if tail:
        parts.append(tail)
    return parts


def _ensure_statement_blocks(lines: list[str]) -> list[str]:
    fixed = list(lines)
    if not any(_is_statement(line) for line in fixed):
        fixed.insert(0, "return None")
    index = 0
    while index < len(fixed):
        line = fixed[index]
        if line.rstrip().endswith(":"):
            indent = _indent_width(line)
            if not _block_has_statement(fixed, index + 1, indent):
                fixed.insert(index + 1, " " * (indent + 4) + "return None")
                index += 1
        index += 1
    return fixed


def _block_has_statement(lines: list[str], start: int, parent_indent: int) -> bool:
    for line in lines[start:]:
        stripped = line.strip()
        if not stripped:
            continue
        indent = _indent_width(line)
        if indent <= parent_indent:
            return False
        if stripped.startswith("#"):
            continue
        return True
    return False


def _is_statement(line: str) -> bool:
    stripped = line.strip()
    return bool(stripped and not stripped.startswith("#"))


def _indent_width(line: str) -> int:
    return len(line) - len(line.lstrip(" "))

def _graph_def_lines(graph: GraphIR) -> list[str]:
    params = _graph_params(graph)
    one_line = f"def graph({params}):"
    if len(one_line) <= 160 or not graph.inputs:
        return [one_line]
    lines = ["def graph("]
    for name, type_str in graph.inputs:
        lines.append(f"    {_safe_param(name)}: {type_str!r},")
    lines[-1] = lines[-1].rstrip(",")
    lines.append("):")
    return lines


def _graph_params(graph: GraphIR) -> str:
    return ", ".join(f"{_safe_param(name)}: {type_str!r}" for name, type_str in graph.inputs)


def _safe_param(value: str) -> str:
    text = re.sub(r"\W+", "_", value).strip("_") or "param"
    if text[0].isdigit():
        text = f"param_{text}"
    return text


def _readable_output(output: tuple[str, str]) -> tuple[str, str]:
    name, type_str = output
    text = str(name)
    if text != "ReturnValue" and text.startswith("ReturnValue_"):
        text = text[len("ReturnValue_"):]
    text = re.sub(r"_[0-9]+_[A-F0-9]{32}$", "", text)
    return (_safe_param(text), type_str)


def _decorator(graph: GraphIR) -> str:
    return "".join(_decorator_lines(graph))


def _decorator_lines(graph: GraphIR) -> list[str]:
    if graph.kind == "event_graph":
        return [f"@std.event(name={graph.graph_name!r})"]
    args = [f"name={graph.graph_name!r}"]
    if graph.inputs:
        args.append(f"inputs={graph.inputs!r}")
    if graph.outputs:
        args.append(f"outputs={[ _readable_output(output) for output in graph.outputs ]!r}")
    if graph.pure:
        args.append("pure=True")
    one_line = f"@std.function({', '.join(args)})"
    if len(one_line) <= 160:
        return [one_line]
    lines = ["@std.function("]
    for arg in args:
        lines.extend(_wrap_nested_item("    ", arg + ",", 160))
    lines[-1] = lines[-1].rstrip(",")
    lines.append(")")
    return lines


def _graph_file_name(graph: GraphIR) -> str:
    stem = graph.stem
    if stem.startswith(("fn_", "evt_")):
        return f"{stem}.py"
    prefix = "evt" if graph.kind == "event_graph" else "fn"
    return f"{prefix}_{_safe_file_part(graph.graph_name)}.py"


def _safe_file_part(value: str) -> str:
    return re.sub(r"\W+", "_", value).strip("_") or "Graph"


def _kwargs(kwargs: dict[str, object]) -> str:
    if not kwargs:
        return ""
    return "".join(f", {key}={value!r}" for key, value in kwargs.items() if value is not None)


def _render_diagnostics(bp: BlueprintIR, lifted_graphs: Iterable[LiftedGraph]) -> str:
    lines = ["# BpyDecompiler Notes", ""]
    count = 0
    for diag in bp.diagnostics:
        count += 1
        lines.append(f"- [{diag.level}] {diag.graph or bp.name}: {diag.message}")
    for lifted in lifted_graphs:
        for item in lifted.unsupported:
            count += 1
            lines.append(f"- [warning] {lifted.graph.graph_name}: {item}")
        for diag in lifted.graph.diagnostics:
            count += 1
            lines.append(f"- [{diag.level}] {lifted.graph.graph_name}: {diag.message}")
    return "\n".join(lines) + "\n" if count else ""















