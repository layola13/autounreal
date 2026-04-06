"""
node_patterns.py — 节点类型识别与分类助手。
"""
from __future__ import annotations
from typing import List, Tuple
from .reader import NodeInfo, GraphInfo, Connection

# ── 节点类型常量 ──────────────────────────────────────────────────────────────
NT_FUNCTION_ENTRY  = "K2Node_FunctionEntry"
NT_FUNCTION_RESULT = "K2Node_FunctionResult"
NT_CALL_FUNCTION   = "K2Node_CallFunction"
NT_IF_THEN_ELSE    = "K2Node_IfThenElse"
NT_DYNAMIC_CAST    = "K2Node_DynamicCast"
NT_EXEC_SEQUENCE   = "K2Node_ExecutionSequence"
NT_SWITCH_ENUM     = "K2Node_SwitchEnum"
NT_SWITCH_INT      = "K2Node_SwitchInteger"
NT_VAR_GET         = "K2Node_VariableGet"
NT_VAR_SET         = "K2Node_VariableSet"
NT_EVENT           = "K2Node_Event"
NT_CUSTOM_EVENT    = "K2Node_CustomEvent"
NT_SELF            = "K2Node_Self"
NT_SELECT          = "K2Node_Select"
NT_BREAK_STRUCT    = "K2Node_BreakStruct"
NT_MAKE_STRUCT     = "K2Node_MakeStruct"
NT_SET_FIELDS      = "K2Node_SetFieldsInStruct"
NT_MESSAGE         = "K2Node_Message"
NT_MACRO_INSTANCE  = "K2Node_MacroInstance"
NT_TIMELINE        = "K2Node_Timeline"
NT_KNOT            = "K2Node_Knot"

EXEC_NODES = {
    NT_CALL_FUNCTION, NT_IF_THEN_ELSE, NT_DYNAMIC_CAST,
    NT_EXEC_SEQUENCE, NT_SWITCH_ENUM, NT_SWITCH_INT,
    NT_VAR_SET, NT_SET_FIELDS, NT_MESSAGE,
    NT_MACRO_INSTANCE, NT_TIMELINE, NT_FUNCTION_RESULT,
}

PURE_NODES = {
    NT_VAR_GET, NT_SELF, NT_BREAK_STRUCT, NT_MAKE_STRUCT, NT_SELECT,
    NT_KNOT,
}

# ── 谓词 ──────────────────────────────────────────────────────────────────────

def is_entry(n: NodeInfo) -> bool:
    return n.node_type in (NT_FUNCTION_ENTRY, NT_EVENT, NT_CUSTOM_EVENT)

def is_branch(n: NodeInfo) -> bool:
    return n.node_type == NT_IF_THEN_ELSE

def is_cast(n: NodeInfo) -> bool:
    return n.node_type == NT_DYNAMIC_CAST

def is_sequence(n: NodeInfo) -> bool:
    return n.node_type == NT_EXEC_SEQUENCE

def is_switch(n: NodeInfo) -> bool:
    return n.node_type in (NT_SWITCH_ENUM, NT_SWITCH_INT)

def is_call(n: NodeInfo) -> bool:
    return n.node_type == NT_CALL_FUNCTION

def is_var_get(n: NodeInfo) -> bool:
    return n.node_type == NT_VAR_GET

def is_var_set(n: NodeInfo) -> bool:
    return n.node_type == NT_VAR_SET

def is_result(n: NodeInfo) -> bool:
    return n.node_type == NT_FUNCTION_RESULT

# ── exec 边查询 ───────────────────────────────────────────────────────────────

def _is_exec_out_pin(pin: str) -> bool:
    exec_out = {"then", "true", "false", "cast_succeeded", "cast_failed",
                "execute", "completed"}
    if pin in exec_out:
        return True
    if pin.startswith("out_") or pin.startswith("case_"):
        return True
    return False

def _is_exec_in_pin(pin: str) -> bool:
    return pin in ("exec", "execute", "in")

def _exec_pin_order(pin: str) -> int:
    order = {
        "then": -1, "execute": -1,
        "true": 0, "cast_succeeded": 0,
        "false": 1, "cast_failed": 1,
        "completed": 100,
    }
    if pin in order:
        return order[pin]
    for prefix in ("out_", "case_"):
        if pin.startswith(prefix):
            try:
                return int(pin[len(prefix):])
            except ValueError:
                return 50
    return 99

def get_exec_successors(var_name: str, gi: GraphInfo) -> List[Tuple[str, str, str]]:
    """返回从 var_name 出发的所有 exec 出边：[(dst_var, dst_pin, src_pin), ...]，已排序。"""
    results = []
    for c in gi.connections:
        if c.src_var == var_name and _is_exec_out_pin(c.src_pin):
            results.append((c.dst_var, c.dst_pin, c.src_pin))
    results.sort(key=lambda x: _exec_pin_order(x[2]))
    return results

def get_data_inputs(var_name: str, gi: GraphInfo) -> List[Tuple[str, str, str]]:
    """返回连入 var_name 的所有数据边：[(src_var, src_pin, dst_pin), ...]。"""
    results = []
    for c in gi.connections:
        if c.dst_var == var_name and not _is_exec_in_pin(c.dst_pin) and not _is_exec_out_pin(c.src_pin):
            results.append((c.src_var, c.src_pin, c.dst_pin))
    return results
