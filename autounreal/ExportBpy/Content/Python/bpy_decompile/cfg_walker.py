"""
cfg_walker.py — 沿 exec 边做 DFS，将 GraphInfo 转换为 CFGBlock 树。

CFGBlock.kind 取值：
  "linear"      — 顺序节点列表
  "branch"      — IfThenElse（pivot=节点, children=[true_block, false_block]）
  "cast_branch" — DynamicCast（pivot=节点, children=[succ_block, fail_block]）
  "sequence"    — ExecutionSequence（pivot=节点, children=[arm0, arm1, ...]）
  "switch"      — SwitchEnum/Integer（pivot=节点, children=[case0, case1, ...], labels=[...]）
"""
from __future__ import annotations
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set
from .reader import GraphInfo, NodeInfo
from .node_patterns import (
    is_entry, is_branch, is_cast, is_sequence, is_switch, is_result,
    get_exec_successors,
    NT_FUNCTION_ENTRY, NT_EVENT, NT_CUSTOM_EVENT,
)


@dataclass
class CFGBlock:
    kind: str
    nodes: List[NodeInfo] = field(default_factory=list)
    children: List["CFGBlock"] = field(default_factory=list)
    pivot: Optional[NodeInfo] = None
    labels: List[str] = field(default_factory=list)


def build_cfg(gi: GraphInfo) -> CFGBlock:
    """构建图的 CFG 树，返回根 CFGBlock（kind="linear"）。"""
    node_map: Dict[str, NodeInfo] = {n.var_name: n for n in gi.nodes}

    # 找入口节点
    entry: Optional[NodeInfo] = None
    for n in gi.nodes:
        if is_entry(n):
            entry = n
            break
    if entry is None and gi.nodes:
        entry = gi.nodes[0]

    visited: Set[str] = set()
    root = CFGBlock(kind="linear")
    if entry:
        _walk(entry, gi, node_map, visited, root)
    return root


def _walk(node: NodeInfo, gi: GraphInfo, node_map: Dict[str, NodeInfo],
          visited: Set[str], current: CFGBlock):
    if node.var_name in visited:
        return
    visited.add(node.var_name)

    if is_branch(node):
        _handle_branch(node, gi, node_map, visited, current)
    elif is_cast(node):
        _handle_cast(node, gi, node_map, visited, current)
    elif is_sequence(node):
        _handle_sequence(node, gi, node_map, visited, current)
    elif is_switch(node):
        _handle_switch(node, gi, node_map, visited, current)
    else:
        # 入口节点也加入 current，但 FunctionEntry 本身是签名载体
        current.nodes.append(node)
        succs = get_exec_successors(node.var_name, gi)
        for dst_var, _dst_pin, _src_pin in succs:
            if dst_var in node_map and dst_var not in visited:
                _walk(node_map[dst_var], gi, node_map, visited, current)


def _handle_branch(node: NodeInfo, gi: GraphInfo, node_map: Dict[str, NodeInfo],
                   visited: Set[str], current: CFGBlock):
    blk = CFGBlock(kind="branch", pivot=node)
    current.nodes.append(node)  # pivot 仍放父块，便于 synth 定位
    current.children.append(blk)

    succs = get_exec_successors(node.var_name, gi)
    # succs 排序后：true(0) 在 false(1) 前
    arms = {src_pin: dst_var for dst_var, _dp, src_pin in succs}

    true_block = CFGBlock(kind="linear")
    false_block = CFGBlock(kind="linear")
    blk.children = [true_block, false_block]
    blk.labels = ["true", "false"]

    if "true" in arms and arms["true"] in node_map and arms["true"] not in visited:
        _walk(node_map[arms["true"]], gi, node_map, set(visited), true_block)
    if "false" in arms and arms["false"] in node_map and arms["false"] not in visited:
        _walk(node_map[arms["false"]], gi, node_map, set(visited), false_block)


def _handle_cast(node: NodeInfo, gi: GraphInfo, node_map: Dict[str, NodeInfo],
                 visited: Set[str], current: CFGBlock):
    blk = CFGBlock(kind="cast_branch", pivot=node)
    current.nodes.append(node)
    current.children.append(blk)

    succs = get_exec_successors(node.var_name, gi)
    arms = {src_pin: dst_var for dst_var, _dp, src_pin in succs}

    succ_block = CFGBlock(kind="linear")
    fail_block = CFGBlock(kind="linear")
    blk.children = [succ_block, fail_block]
    blk.labels = ["cast_succeeded", "cast_failed"]

    if "cast_succeeded" in arms and arms["cast_succeeded"] in node_map:
        _walk(node_map[arms["cast_succeeded"]], gi, node_map, set(visited), succ_block)
    if "cast_failed" in arms and arms["cast_failed"] in node_map:
        _walk(node_map[arms["cast_failed"]], gi, node_map, set(visited), fail_block)


def _handle_sequence(node: NodeInfo, gi: GraphInfo, node_map: Dict[str, NodeInfo],
                     visited: Set[str], current: CFGBlock):
    blk = CFGBlock(kind="sequence", pivot=node)
    current.nodes.append(node)
    current.children.append(blk)

    succs = get_exec_successors(node.var_name, gi)
    for dst_var, _dp, src_pin in succs:
        arm = CFGBlock(kind="linear")
        blk.children.append(arm)
        blk.labels.append(src_pin)
        if dst_var in node_map and dst_var not in visited:
            _walk(node_map[dst_var], gi, node_map, set(visited), arm)


def _handle_switch(node: NodeInfo, gi: GraphInfo, node_map: Dict[str, NodeInfo],
                   visited: Set[str], current: CFGBlock):
    blk = CFGBlock(kind="switch", pivot=node)
    current.nodes.append(node)
    current.children.append(blk)

    succs = get_exec_successors(node.var_name, gi)
    for dst_var, _dp, src_pin in succs:
        arm = CFGBlock(kind="linear")
        blk.children.append(arm)
        blk.labels.append(src_pin)
        if dst_var in node_map and dst_var not in visited:
            _walk(node_map[dst_var], gi, node_map, set(visited), arm)
