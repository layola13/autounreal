"""
Synthesize upper-level Python statements from CFG blocks and pin expressions.
"""

from __future__ import annotations

from typing import Dict, List, Optional

from .cfg_walker import CFGBlock, build_cfg
from .dataflow import PinExprMap, _literal, _snake, resolve_dataflow
from .func_reverse import get_function_spec, get_python_name
from .node_patterns import (
    NT_CALL_FUNCTION,
    NT_CUSTOM_EVENT,
    NT_DYNAMIC_CAST,
    NT_EVENT,
    NT_EXEC_SEQUENCE,
    NT_FUNCTION_ENTRY,
    NT_FUNCTION_RESULT,
    NT_IF_THEN_ELSE,
    NT_MACRO_INSTANCE,
    NT_MESSAGE,
    NT_SET_FIELDS,
    NT_SWITCH_ENUM,
    NT_SWITCH_INT,
    NT_TIMELINE,
    NT_VAR_SET,
    get_data_inputs,
)
from .reader import GraphInfo, NodeInfo


def synthesize(gi: GraphInfo) -> List[str]:
    node_map = {node.var_name: node for node in gi.nodes}
    pin_expr = resolve_dataflow(gi)
    cfg = build_cfg(gi)
    lines: List[str] = []
    _emit_block(cfg, node_map, pin_expr, gi, lines, indent=1)
    return lines


def _emit_block(
    blk: CFGBlock,
    node_map: Dict[str, NodeInfo],
    pin_expr: PinExprMap,
    gi: GraphInfo,
    lines: List[str],
    indent: int,
):
    pad = "    " * indent
    child_idx = 0

    for node in blk.nodes:
        nt = node.node_type
        vn = node.var_name

        if nt in (NT_FUNCTION_ENTRY, NT_EVENT, NT_CUSTOM_EVENT):
            continue

        if nt == NT_IF_THEN_ELSE:
            cond_expr = _get_data_expr(vn, "condition", gi, pin_expr, "condition")
            lines.append(f"{pad}if {cond_expr}:")
            branch_blk = _find_child_for_node(blk, vn, child_idx)
            if branch_blk and branch_blk.children:
                true_blk = branch_blk.children[0] if len(branch_blk.children) > 0 else None
                false_blk = branch_blk.children[1] if len(branch_blk.children) > 1 else None
                if true_blk and true_blk.nodes:
                    _emit_block(true_blk, node_map, pin_expr, gi, lines, indent + 1)
                else:
                    lines.append(f"{pad}    pass")
                if false_blk and false_blk.nodes:
                    lines.append(f"{pad}else:")
                    _emit_block(false_blk, node_map, pin_expr, gi, lines, indent + 1)
            child_idx += 1
            continue

        if nt == NT_DYNAMIC_CAST:
            cast_class = node.class_name or node.name or "Object"
            local_var = _snake(cast_class)
            cast_expr = pin_expr.get(f"{vn}:__cast_expr__", f'cast(obj, "{cast_class}")')
            lines.append(f'{pad}{local_var}: "{cast_class}" = {cast_expr}')
            cast_blk = _find_child_for_node(blk, vn, child_idx)
            if cast_blk and cast_blk.children:
                succ_blk = cast_blk.children[0] if cast_blk.children else None
                fail_blk = cast_blk.children[1] if len(cast_blk.children) > 1 else None
                lines.append(f"{pad}if {local_var} is not None:")
                if succ_blk and succ_blk.nodes:
                    _emit_block(succ_blk, node_map, pin_expr, gi, lines, indent + 1)
                else:
                    lines.append(f"{pad}    pass")
                if fail_blk and fail_blk.nodes:
                    lines.append(f"{pad}else:")
                    _emit_block(fail_blk, node_map, pin_expr, gi, lines, indent + 1)
            child_idx += 1
            continue

        if nt == NT_EXEC_SEQUENCE:
            seq_blk = _find_child_for_node(blk, vn, child_idx)
            if seq_blk:
                for arm in seq_blk.children:
                    _emit_block(arm, node_map, pin_expr, gi, lines, indent)
            child_idx += 1
            continue

        if nt in (NT_SWITCH_ENUM, NT_SWITCH_INT):
            sel_expr = _get_data_expr(vn, "selection", gi, pin_expr, "val")
            sw_blk = _find_child_for_node(blk, vn, child_idx)
            if sw_blk:
                for index, (arm, label) in enumerate(zip(sw_blk.children, sw_blk.labels)):
                    keyword = "if" if index == 0 else "elif"
                    lines.append(f"{pad}{keyword} {sel_expr} == {label!r}:")
                    if arm.nodes:
                        _emit_block(arm, node_map, pin_expr, gi, lines, indent + 1)
                    else:
                        lines.append(f"{pad}    pass")
            child_idx += 1
            continue

        if nt == NT_VAR_SET:
            lhs = pin_expr.get(f"{vn}:__assign_lhs__", f"self.{node.name or vn}")
            rhs = pin_expr.get(f"{vn}:__assign_rhs__", "None")
            lines.append(f"{pad}{lhs} = {rhs}")
            continue

        if nt == NT_SET_FIELDS:
            struct_src = pin_expr.get(f"{vn}:__struct_src__", "struct_val")
            for key, value in pin_expr.items():
                if key.startswith(f"{vn}:__field__"):
                    field_name = key[len(f"{vn}:__field__") :]
                    lines.append(f"{pad}{struct_src}.{field_name} = {value}")
            continue

        if nt == NT_CALL_FUNCTION:
            ue_ref = node.name or ""
            py_name = get_python_name(ue_ref) or _snake(ue_ref.split("::")[-1]) or vn
            spec = get_function_spec(ue_ref)
            is_pure = spec.pure if spec else False
            inputs = get_data_inputs(vn, gi)
            param_map = spec.params if spec else {}
            reverse_param = {value: key for key, value in param_map.items()}
            args = []
            seen_dp = set()
            for sv, sp, dp in inputs:
                kw = reverse_param.get(dp, _snake(dp))
                val = pin_expr.get(f"{sv}:{sp}") or _get_data_expr(vn, dp, gi, pin_expr, dp)
                args.append(f"{kw}={val}")
                seen_dp.add(dp)
            for pn, pv in node.defaults.items():
                if pn not in seen_dp:
                    kw = reverse_param.get(pn, _snake(pn))
                    args.append(f"{kw}={_literal(pv)}")
            call = f"{py_name}({', '.join(args)})"
            step = call
            if not is_pure:
                lines.append(f"{pad}{step}")
            continue

        if nt == NT_FUNCTION_RESULT:
            inputs = get_data_inputs(vn, gi)
            ret_parts = []
            for sv, sp, dp in inputs:
                val = pin_expr.get(f"{sv}:{sp}") or sp
                ret_parts.append(f"{_snake(dp)}={val}")
            if ret_parts:
                lines.append(f"{pad}return result({', '.join(ret_parts)})")
            else:
                lines.append(f"{pad}return")
            continue

        if nt in (NT_MACRO_INSTANCE, NT_TIMELINE):
            label = node.name or vn
            lines.append(f"{pad}# {nt}: {label} (not expanded)")
            continue

        if nt == NT_MESSAGE:
            fn_name = node.name or vn
            inputs = get_data_inputs(vn, gi)
            args = []
            for sv, sp, dp in inputs:
                if dp in ("self", "execute"):
                    continue
                val = pin_expr.get(f"{sv}:{sp}") or sp
                args.append(f"{_snake(dp)}={val}")
            lines.append(f"{pad}{_snake(fn_name)}({', '.join(args)})")
            continue

    return lines


def _find_child_for_node(blk: CFGBlock, var_name: str, child_idx: int) -> Optional[CFGBlock]:
    del var_name
    if child_idx < len(blk.children):
        return blk.children[child_idx]
    return None


def _get_data_expr(var_name: str, pin_name: str, gi: GraphInfo, pin_expr: PinExprMap, fallback: str) -> str:
    key = f"{var_name}:{pin_name}"
    if key in pin_expr:
        return pin_expr[key]
    for conn in gi.connections:
        if conn.dst_var == var_name and conn.dst_pin == pin_name:
            src_key = f"{conn.src_var}:{conn.src_pin}"
            if src_key in pin_expr:
                return pin_expr[src_key]
    node = next((item for item in gi.nodes if item.var_name == var_name), None)
    if node and pin_name in node.defaults:
        return _literal(node.defaults[pin_name])
    return fallback
