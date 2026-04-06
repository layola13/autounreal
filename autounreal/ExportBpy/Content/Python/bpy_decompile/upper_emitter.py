"""
upper_emitter.py — 将反编译结果写出到 UpperBlueprints/<BPName>/ 目录。

输出文件：
  __upper__.py          — 骨架声明（Blueprint + var 声明）
  fn_<GraphName>.py     — 函数图
  evt_<GraphName>.py    — 事件图
"""
from __future__ import annotations
import os
import re
from typing import List
from .reader import BPPackage, GraphInfo, NodeInfo
from .synth import synthesize
from .node_patterns import NT_FUNCTION_ENTRY, NT_EVENT, NT_CUSTOM_EVENT


def emit_package(pkg: BPPackage, output_dir: str) -> None:
    os.makedirs(output_dir, exist_ok=True)
    _write_upper(pkg, output_dir)
    for gi in pkg.graphs:
        _write_graph(gi, output_dir)


def _write_upper(pkg: BPPackage, output_dir: str) -> None:
    lines: List[str] = [
        "from ue_bp_dsl import Blueprint",
        "",
        "bp = Blueprint(",
        f'    path="{pkg.bp_path}",',
        f'    parent="{pkg.parent_class}",',
        f'    bp_type="{pkg.bp_type}",',
        ")",
        "",
    ]

    if pkg.components:
        for comp in pkg.components:
            c_name = comp.get("name", "")
            c_class = comp.get("class_name", "")
            c_parent = comp.get("parent", "")
            c_attach = comp.get("attach_to_name", "")
            c_properties = comp.get("properties", {}) or {}
            arg = f'"{c_name}", class_name="{c_class}"'
            if c_parent:
                arg += f', parent="{c_parent}"'
            if c_attach:
                arg += f', attach_to_name="{c_attach}"'
            if c_properties:
                arg += f", properties={c_properties!r}"
            lines.append(f"bp.component({arg})")
        lines.append("")

    if pkg.variables:
        for var in pkg.variables:
            v_name = var.get("name", "")
            v_type = var.get("type", "unknown")
            v_container = var.get("container", "single")
            v_default = var.get("default", "")
            arg = f'"{v_name}", "{v_type}", container="{v_container}"'
            if v_default:
                default_escaped = v_default.replace("\\", "\\\\").replace('"', '\\"')
                arg += f', default="{default_escaped}"'
            lines.append(f"bp.var({arg})")
        lines.append("")

    lines.append("bp.build()")
    lines.append("")

    gate = os.path.join(output_dir, "__upper__.py")
    _write_lines(gate, lines)


def _write_graph(gi: GraphInfo, output_dir: str) -> None:
    is_event = gi.graph_kind == "event_graph"
    prefix = "evt_" if is_event else "fn_"
    safe_name = re.sub(r"[^a-zA-Z0-9_]", "_", gi.graph_name).strip("_") or "Graph"
    fname = f"{prefix}{safe_name}.py"

    # 找入口节点提取签名
    entry = _find_entry(gi)
    func_name = gi.graph_name
    params: List[str] = ["self"]
    if entry:
        func_name = entry.name or gi.graph_name
        # 提取输入 pins 作为参数（来自 node_props 或 DSL 输入边）
        for k, v in entry.node_props.items():
            if k.startswith("input_") or k.startswith("param_"):
                params.append(k)
        # 尝试从 GraphInfo.node_pos / pin_alias 找参数
        # 退回：直接扫连线，找从 entry 出去的数据边
        for c in gi.connections:
            if c.src_var == entry.var_name and c.src_pin not in ("then", "execute"):
                param_name = c.src_pin
                if param_name not in params:
                    params.append(param_name)

    lines: List[str] = [
        "from bpy_compile.prelude import bp_function",
        "",
    ]

    # 函数装饰器
    inputs_repr = ""
    if len(params) > 1:
        pairs = [(p, "unknown") for p in params[1:]]
        inputs_repr = f", inputs={repr(pairs)}"

    pure_flag = "False" if is_event else "False"
    lines += [
        "@bp_function(",
        f'    name="{func_name}",',
    ]
    if inputs_repr:
        lines.append(f"    inputs={repr([(p, 'unknown') for p in params[1:]])},")
    lines += [
        f"    pure={pure_flag},",
        ")",
        f"def graph({', '.join(params)}):",
    ]

    # 正文语句
    body = synthesize(gi)
    if not body:
        lines.append("    pass")
    else:
        lines.extend(body)

    lines.append("")

    gate = os.path.join(output_dir, fname)
    _write_lines(gate, lines)


def _find_entry(gi: GraphInfo):
    for n in gi.nodes:
        if n.node_type in (NT_FUNCTION_ENTRY, NT_EVENT, NT_CUSTOM_EVENT):
            return n
    return None


def _write_lines(gate: str, lines: List[str]) -> None:
    with open(gate, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
