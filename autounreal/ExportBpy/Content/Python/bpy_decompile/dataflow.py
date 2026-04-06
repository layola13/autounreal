"""
Resolve data-flow edges into upper-level Python expressions.
"""

from __future__ import annotations

from typing import Dict, Set

from .func_reverse import get_function_spec, get_python_name
from .node_patterns import (
    NT_BREAK_STRUCT,
    NT_CALL_FUNCTION,
    NT_KNOT,
    NT_MAKE_STRUCT,
    NT_SELECT,
    NT_SELF,
    get_data_inputs,
)
from .reader import GraphInfo, NodeInfo

PinExprMap = Dict[str, str]


def resolve_dataflow(gi: GraphInfo) -> PinExprMap:
    node_map: Dict[str, NodeInfo] = {node.var_name: node for node in gi.nodes}
    pin_expr: PinExprMap = {}
    visiting: Set[str] = set()

    def resolve(var_name: str, pin_name: str) -> str:
        key = f"{var_name}:{pin_name}"
        if key in pin_expr:
            return pin_expr[key]
        if key in visiting:
            return key
        visiting.add(key)

        node = node_map.get(var_name)
        if node is None:
            visiting.discard(key)
            return key

        result = _resolve_node_pin(node, pin_name, gi, node_map, pin_expr, resolve)
        pin_expr[key] = result
        visiting.discard(key)
        return result

    for node in gi.nodes:
        _resolve_node_pin(node, "", gi, node_map, pin_expr, resolve)

    return pin_expr


def _resolve_node_pin(
    node: NodeInfo,
    pin_name: str,
    gi: GraphInfo,
    node_map: Dict[str, NodeInfo],
    pin_expr: PinExprMap,
    resolve,
) -> str:
    del node_map
    nt = node.node_type
    vn = node.var_name

    if nt == NT_SELF:
        expr = "self"
        pin_expr[f"{vn}:self"] = expr
        return expr

    if nt == "K2Node_VariableGet":
        field = node.name or vn
        inputs = get_data_inputs(vn, gi)
        self_src = next((resolve(sv, sp) for sv, sp, dp in inputs if dp == "self"), None)
        expr = f"{self_src}.{field}" if self_src and self_src != "self" else f"self.{field}"
        pin_expr[f"{vn}:value"] = expr
        pin_expr[f"{vn}:{field}"] = expr
        return expr

    if nt == NT_BREAK_STRUCT:
        inputs = get_data_inputs(vn, gi)
        struct_src = next((resolve(sv, sp) for sv, sp, dp in inputs if dp in ("struct", "value", "input")), vn)
        if pin_name:
            expr = f"{struct_src}.{pin_name}"
            pin_expr[f"{vn}:{pin_name}"] = expr
            return expr
        return struct_src

    if nt == NT_MAKE_STRUCT:
        struct_type = node.class_name or node.name or "Struct"
        inputs = get_data_inputs(vn, gi)
        args = [f"{dp}={resolve(sv, sp)}" for sv, sp, dp in inputs]
        for pn, pv in node.defaults.items():
            if not any(dp == pn for _, _, dp in inputs):
                args.append(f"{pn}={_literal(pv)}")
        expr = f"make_{_snake(struct_type)}({', '.join(args)})"
        pin_expr[f"{vn}:return_value"] = expr
        pin_expr[f"{vn}:ReturnValue"] = expr
        return expr

    if nt == NT_SELECT:
        inputs = get_data_inputs(vn, gi)
        cond = next((resolve(sv, sp) for sv, sp, dp in inputs if dp in ("index", "condition")), "cond")
        opt_a = next((resolve(sv, sp) for sv, sp, dp in inputs if dp in ("option_0", "a", "true")), "None")
        opt_b = next((resolve(sv, sp) for sv, sp, dp in inputs if dp in ("option_1", "b", "false")), "None")
        expr = f"select({cond}, {opt_a}, {opt_b})"
        pin_expr[f"{vn}:return_value"] = expr
        return expr

    if nt == NT_KNOT:
        inputs = get_data_inputs(vn, gi)
        expr = resolve(inputs[0][0], inputs[0][1]) if inputs else vn
        pin_expr[f"{vn}:output"] = expr
        return expr

    if nt == NT_CALL_FUNCTION:
        ue_ref = node.name or ""
        py_name = get_python_name(ue_ref) or (_snake(ue_ref.split("::")[-1]) if ue_ref else vn)
        inputs = get_data_inputs(vn, gi)
        spec = get_function_spec(ue_ref)
        param_map = spec.params if spec else {}
        reverse_param = {value: key for key, value in param_map.items()}

        args = []
        seen_pins = set()
        for sv, sp, dp in inputs:
            kw = reverse_param.get(dp, _snake(dp))
            args.append(f"{kw}={resolve(sv, sp)}")
            seen_pins.add(dp)
        for pn, pv in node.defaults.items():
            if pn not in seen_pins:
                kw = reverse_param.get(pn, _snake(pn))
                args.append(f"{kw}={_literal(pv)}")

        expr = f"{py_name}({', '.join(args)})"
        ret_pin = spec.returns[0] if spec and spec.returns else "ReturnValue"
        pin_expr[f"{vn}:{ret_pin}"] = expr
        pin_expr[f"{vn}:return_value"] = expr
        return expr

    if nt == "K2Node_DynamicCast":
        cast_class = node.class_name or node.name or "Object"
        inputs = get_data_inputs(vn, gi)
        obj_src = next((resolve(sv, sp) for sv, sp, dp in inputs if dp in ("object", "Object")), "obj")
        local_var = _snake(cast_class)
        expr = f'cast({obj_src}, "{cast_class}")'
        pin_expr[f"{vn}:as_{_snake(cast_class)}"] = local_var
        pin_expr[f"{vn}:cast_result"] = local_var
        pin_expr[f"{vn}:__cast_expr__"] = expr
        pin_expr[f"{vn}:__cast_var__"] = local_var
        return expr

    if nt == "K2Node_VariableSet":
        field = node.name or vn
        inputs = get_data_inputs(vn, gi)
        val_src = next((resolve(sv, sp) for sv, sp, dp in inputs if dp not in ("self", "execute")), "None")
        self_src = next((resolve(sv, sp) for sv, sp, dp in inputs if dp == "self"), None)
        lhs = f"{self_src}.{field}" if self_src and self_src != "self" else f"self.{field}"
        pin_expr[f"{vn}:__assign_lhs__"] = lhs
        pin_expr[f"{vn}:__assign_rhs__"] = val_src
        return val_src

    if nt == "K2Node_SetFieldsInStruct":
        inputs = get_data_inputs(vn, gi)
        struct_src = next((resolve(sv, sp) for sv, sp, dp in inputs if dp in ("struct", "input")), vn)
        pin_expr[f"{vn}:__struct_src__"] = struct_src
        for sv, sp, dp in inputs:
            if dp not in ("struct", "input"):
                pin_expr[f"{vn}:__field__{dp}"] = resolve(sv, sp)
        return struct_src

    if nt in ("K2Node_FunctionEntry", "K2Node_Event", "K2Node_CustomEvent"):
        if pin_name:
            expr = _snake(pin_name)
            pin_expr[f"{vn}:{pin_name}"] = expr
            return expr
        return vn

    if nt == "K2Node_FunctionResult":
        return vn

    return pin_name or vn


def _snake(value: str) -> str:
    import re

    value = re.sub(r"[^a-zA-Z0-9]", "_", value).strip("_")
    value = re.sub(r"_+", "_", value)
    return value.lower() or "val"


def _literal(value: str) -> str:
    if value in ("True", "False", "None"):
        return value
    if value.lower() == "true":
        return "True"
    if value.lower() == "false":
        return "False"
    try:
        float(value)
        return value
    except ValueError:
        pass
    return repr(value)
