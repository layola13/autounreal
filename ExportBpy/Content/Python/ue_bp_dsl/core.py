# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-
"""
ue_bp_dsl/core.py — Blueprint DSL 核心层

实现文档要求的面向对象 + >> 操作符格式：

    from ue_bp_dsl import *

    bp = Blueprint(
        path="/Game/Blueprints/BP_Enemy",
        parent="/Script/Engine.Character",
    )

    bp.var("Health", "float", default=100.0, category="Stats")
    bp.component("Mesh", "USkeletalMeshComponent", parent="DefaultSceneRoot")

    with bp.event_graph("EventGraph") as g:
        BeginPlay  = g.event("ReceiveBeginPlay")
        PrintStr_0 = g.call_function("UKismetSystemLibrary::PrintString")

        PrintStr_0.set_pin("InString", "Hello!")
        BeginPlay.then >> PrintStr_0.exec

    with bp.function("ApplyDamage",
        inputs=[("Amount", "float")],
        outputs=[("Survived", "bool")],
    ) as g:
        Entry    = g.func_entry()
        Return   = g.func_result()
        Entry.then >> Return.exec
"""

from __future__ import annotations

import uuid
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple


# ═══════════════════════════════════════════════════════════════
#  Internal data model
# ═══════════════════════════════════════════════════════════════

@dataclass
class _Connection:
    src_node_uid: str
    src_pin:      str
    dst_node_uid: str
    dst_pin:      str
    src_pin_full: str = ""
    dst_pin_full: str = ""
    src_pin_id:   str = ""
    dst_pin_id:   str = ""


@dataclass
class _Node:
    uid:          str
    node_class:   str
    member_name:  str               = ""
    function_ref: str               = ""
    custom_params: Optional[List]   = None
    tunnel_type:  str               = ""   # "entry" | "exit" for macros
    target_type:  str               = ""   # for DynamicCast
    defaults:     Dict[str, Any]    = field(default_factory=dict)
    pos_x:        float             = 0.0
    pos_y:        float             = 0.0
    readable_name: str             = ""
    node_guid:    str               = ""
    extra_props:  Dict[str, Any]    = field(default_factory=dict)
    pin_aliases:  Dict[str, str]    = field(default_factory=dict)
    pin_ids:      Dict[str, str]    = field(default_factory=dict)


@dataclass
class _Graph:
    name:        str
    graph_type:  str          # "event_graph" | "function" | "macro"
    nodes:       List[_Node]  = field(default_factory=list)
    connections: List[_Connection] = field(default_factory=list)
    inputs:      List[Tuple[str, str]] = field(default_factory=list)
    outputs:     List[Tuple[str, str]] = field(default_factory=list)
    is_pure:     bool  = False
    category:    str   = ""
    metadata:    Dict[str, Any] = field(default_factory=dict)
    uid_factory: Any = None


@dataclass
class _Variable:
    name:        str
    type_str:    str
    container:   str        = "single"
    default:     Any        = None
    category:    str        = ""
    replicated:  bool       = False
    rep_notify:  str        = ""
    instance_editable: bool = False
    tooltip:     str        = ""


@dataclass
class _Dispatcher:
    name:   str
    params: List[Tuple[str, str]] = field(default_factory=list)


@dataclass
class _Component:
    name:       str
    class_name: str
    parent:     Optional[str]       = None
    properties: Dict[str, Any]      = field(default_factory=dict)


@dataclass
class _TimelineKey:
    time:            float
    value:           Any
    interp:          str   = "linear"
    arrive_tangent:  float = 0.0
    leave_tangent:   float = 0.0


@dataclass
class _TimelineTrack:
    name:       str
    track_type: str   # "float" | "vector" | "color" | "event"
    keys:       List[_TimelineKey] = field(default_factory=list)


@dataclass
class _Timeline:
    name:             str
    length:           float = 1.0
    looping:          bool  = False
    autoplay:         bool  = False
    use_last_keyframe: bool = True
    tracks:           List[_TimelineTrack] = field(default_factory=list)


# ═══════════════════════════════════════════════════════════════
#  PinRef — supports >> operator
# ═══════════════════════════════════════════════════════════════

class PinRef:
    """Pin 引用，支持 >> 操作符建立连线。"""

    def __init__(self, node: _Node, graph: _Graph,
                 pin_name: str, direction: str):
        self._node      = node
        self._graph     = graph
        self._pin_name  = pin_name
        self._direction = direction  # "input" | "output"

    def __rshift__(self, other: "PinRef") -> "PinRef":
        """src_pin >> dst_pin  →  记录一条连线。"""
        if not isinstance(other, PinRef):
            raise TypeError(f">> 右侧必须是 PinRef，得到 {type(other)}")
        self._graph.connections.append(_Connection(
            src_node_uid=self._node.uid,
            src_pin=self._pin_name,
            dst_node_uid=other._node.uid,
            dst_pin=other._pin_name,
        ))
        return other


# ═══════════════════════════════════════════════════════════════
#  NodeProxy — node handle with pin accessors
# ═══════════════════════════════════════════════════════════════

class NodeProxy:
    """节点代理，支持 set_pin / in_pin / out_pin / then / exec 等访问器。"""

    def __init__(self, node: _Node, graph: _Graph):
        self._node  = node
        self._graph = graph

    # ── default value ─────────────────────────────────────────

    def set_pin(self, pin_name: str, value: Any) -> "NodeProxy":
        """设置输入 Pin 默认值。"""
        self._node.defaults[pin_name] = value
        return self

    def pin(self, pin_name: str, value: Any) -> "NodeProxy":
        """设计 DSL 的 pin() 别名。"""
        return self.set_pin(pin_name, value)

    def set_readable_name(self, name: str) -> "NodeProxy":
        self._node.readable_name = name
        return self

    def set_node_guid(self, node_guid: str) -> "NodeProxy":
        self._node.node_guid = node_guid
        return self

    def set_extra_prop(self, key: str, value: Any) -> "NodeProxy":
        self._node.extra_props[key] = value
        return self

    def set_pin_alias(self, pin_name: str, full_pin_name: str) -> "NodeProxy":
        self._node.pin_aliases[pin_name] = full_pin_name
        return self

    def set_pin_id(self, pin_name: str, pin_id: str) -> "NodeProxy":
        self._node.pin_ids[pin_name] = pin_id
        return self

    def _auto_pin(self, pin_name: str) -> PinRef:
        return PinRef(self._node, self._graph, pin_name, "")

    # ── pin references ────────────────────────────────────────

    def in_pin(self, pin_name: str) -> PinRef:
        return PinRef(self._node, self._graph, pin_name, "input")

    def out_pin(self, pin_name: str = "ReturnValue") -> PinRef:
        return PinRef(self._node, self._graph, pin_name, "output")

    def case(self, value: Any) -> PinRef:
        """Switch 节点 case(...) 输出 pin。"""
        return self._auto_pin(str(value))

    def __getitem__(self, pin_name: str) -> PinRef:
        return self._auto_pin(str(pin_name))

    def __getattr__(self, attr_name: str) -> PinRef:
        if attr_name.startswith("_"):
            raise AttributeError(attr_name)
        pin_name = {
            "true_": "True",
            "false_": "False",
            "cast_failed": "CastFailed",
            "self_": "self",
        }.get(attr_name, attr_name)
        return self._auto_pin(pin_name)

    # ── exec sugar ────────────────────────────────────────────

    @property
    def exec(self) -> PinRef:
        """输入执行 Pin（接收执行流）。"""
        return PinRef(self._node, self._graph, "execute", "input")

    @property
    def then(self) -> PinRef:
        """输出执行 Pin（发出执行流）。"""
        return PinRef(self._node, self._graph, "then", "output")

    @property
    def value(self) -> PinRef:
        if self._node.node_class == "K2Node_VariableGet" and self._node.member_name:
            return self._auto_pin(self._node.member_name)
        return self._auto_pin("ReturnValue")

    @property
    def result(self) -> PinRef:
        return self._auto_pin("ReturnValue")

    @property
    def condition(self) -> PinRef:
        return self._auto_pin("Condition")

    @property
    def selection(self) -> PinRef:
        return self._auto_pin("Selection")

    @property
    def index(self) -> PinRef:
        return self._auto_pin("Index")

    @property
    def out_true(self) -> PinRef:
        return PinRef(self._node, self._graph, "True", "output")

    @property
    def out_false(self) -> PinRef:
        return PinRef(self._node, self._graph, "False", "output")

    @property
    def out_completed(self) -> PinRef:
        return PinRef(self._node, self._graph, "Completed", "output")

    @property
    def out_loop_body(self) -> PinRef:
        return PinRef(self._node, self._graph, "LoopBody", "output")

    def out_seq(self, index: int) -> PinRef:
        return PinRef(self._node, self._graph, f"then_{index}", "output")


# ═══════════════════════════════════════════════════════════════
#  GraphContext — context manager + node factory
# ═══════════════════════════════════════════════════════════════

class GraphContext:
    """with 语句上下文管理器，同时作为节点工厂。"""

    def __init__(self, blueprint: "Blueprint", graph: _Graph):
        self._bp    = blueprint
        self._graph = graph

    def __enter__(self) -> "GraphContext":
        return self

    def __exit__(self, *args):
        self._bp._graphs.append(self._graph)

    def set_default(self, node: NodeProxy, pin_name: str, value: Any) -> NodeProxy:
        """兼容旧导出器的 g.set_default(node, pin, value)。"""
        return node.set_pin(pin_name, value)

    # ── node factories ────────────────────────────────────────

    def _add(self, node_class: str, **kwargs) -> NodeProxy:
        pos = kwargs.pop("pos", None)
        pos_x = kwargs.pop("pos_x", 0.0)
        pos_y = kwargs.pop("pos_y", 0.0)
        uid = kwargs.pop("uid", None)
        if pos is not None:
            pos_x, pos_y = pos
        if uid is None and callable(getattr(self._graph, "uid_factory", None)):
            uid = self._graph.uid_factory(node_class=node_class, kwargs=kwargs)
        n = _Node(
            uid=str(uid or uuid.uuid4()),
            node_class=node_class,
            pos_x=pos_x,
            pos_y=pos_y,
            **kwargs,
        )
        self._graph.nodes.append(n)
        return NodeProxy(n, self._graph)

    def event(self, event_name: str) -> NodeProxy:
        return self._add("K2Node_Event", member_name=event_name)

    def custom_event(self, name: str,
                     params: Optional[List[Tuple[str, str]]] = None) -> NodeProxy:
        return self._add("K2Node_CustomEvent",
                         member_name=name, custom_params=params or [])

    def call_function(self, func_ref: str, *, node_class: str = "K2Node_CallFunction") -> NodeProxy:
        """func_ref 格式: 'ClassName::FunctionName'"""
        return self._add(node_class, function_ref=func_ref)

    def call(self, func_ref: str, *, node_class: str = "K2Node_CallFunction") -> NodeProxy:
        return self.call_function(func_ref, node_class=node_class)

    def message(self, func_ref: str, *, node_class: str = "K2Node_Message") -> NodeProxy:
        return self._add(node_class, function_ref=func_ref)

    def get_var(self, var_name: str) -> NodeProxy:
        return self._add("K2Node_VariableGet", member_name=var_name)

    def get_component(self, component_name: str) -> NodeProxy:
        return self._add("K2Node_VariableGet", member_name=component_name)

    def set_var(self, var_name: str) -> NodeProxy:
        return self._add("K2Node_VariableSet", member_name=var_name)

    def branch(self) -> NodeProxy:
        return self._add("K2Node_IfThenElse")

    def sequence(self) -> NodeProxy:
        return self._add("K2Node_ExecutionSequence")

    def for_each_loop(self) -> NodeProxy:
        return self._add("K2Node_ForEachElementInArray")

    def cast(self, target_class: str) -> NodeProxy:
        return self._add("K2Node_DynamicCast", target_type=target_class)

    def call_dispatcher(self, dispatcher_name: str) -> NodeProxy:
        return self._add("K2Node_CallDelegate", member_name=dispatcher_name)

    def func_entry(self) -> NodeProxy:
        return self._add("K2Node_FunctionEntry")

    def entry(self) -> NodeProxy:
        return self.func_entry()

    def func_result(self) -> NodeProxy:
        return self._add("K2Node_FunctionResult")

    def result(self) -> NodeProxy:
        return self.func_result()

    def macro_entry(self) -> NodeProxy:
        return self._add("K2Node_Tunnel", tunnel_type="entry")

    def macro_result(self) -> NodeProxy:
        return self._add("K2Node_Tunnel", tunnel_type="exit")

    def timeline_node(self, timeline_name: str) -> NodeProxy:
        return self._add("K2Node_Timeline", member_name=timeline_name)

    def select(self) -> NodeProxy:
        return self._add("K2Node_Select")

    def switch_enum(self, enum_type: str = "") -> NodeProxy:
        return self._add("K2Node_SwitchEnum", target_type=enum_type)

    def switch_int(self) -> NodeProxy:
        return self._add("K2Node_SwitchInteger")

    def make_array(self) -> NodeProxy:
        return self._add("K2Node_MakeArray")

    def make_struct(self, struct_type: str = "") -> NodeProxy:
        return self._add("K2Node_MakeStruct", target_type=struct_type)

    def break_struct(self, struct_type: str = "") -> NodeProxy:
        return self._add("K2Node_BreakStruct", target_type=struct_type)

    def set_fields(self, struct_type: str = "") -> NodeProxy:
        extra_props: Dict[str, Any] = {}
        if struct_type:
            extra_props["StructType"] = struct_type
        return self._add("K2Node_SetFieldsInStruct", target_type=struct_type, extra_props=extra_props)

    def self_ref(self) -> NodeProxy:
        return self._add("K2Node_Self")

    def node(self, *, type: str, name: str = "", class_name: str = "",
             pos: Optional[Tuple[float, float]] = None,
             target_type: str = "", struct_type: str = "",
             enum_type: str = "") -> NodeProxy:
        """
        通用 fallback 工厂，用于兼容旧导出器和未语义化节点。
        """
        node_class = type if type.startswith("K2Node_") else f"K2Node_{type}"
        resolved_target = target_type or struct_type or enum_type

        kwargs: Dict[str, Any] = {"pos": pos}
        if resolved_target:
            kwargs["target_type"] = resolved_target

        if node_class == "K2Node_CallFunction":
            kwargs["function_ref"] = f"{class_name}::{name}" if class_name else name
        elif node_class == "K2Node_Message":
            kwargs["function_ref"] = f"{class_name}::{name}" if class_name else name
            if name:
                kwargs["member_name"] = name
        elif node_class in {
            "K2Node_VariableGet",
            "K2Node_VariableSet",
            "K2Node_Event",
            "K2Node_CustomEvent",
            "K2Node_CallDelegate",
        }:
            kwargs["member_name"] = name
        elif name:
            kwargs["member_name"] = name

        if node_class == "K2Node_Message" and class_name:
            kwargs.setdefault("extra_props", {})["InterfaceClass"] = class_name
        elif node_class == "K2Node_MacroInstance" and resolved_target:
            kwargs.setdefault("extra_props", {})["MacroGraph"] = resolved_target

        return self._add(node_class, **kwargs)


# ═══════════════════════════════════════════════════════════════
#  Blueprint — top-level recording object
# ═══════════════════════════════════════════════════════════════

class Blueprint:
    """
    蓝图描述对象。所有方法调用都被记录到内部模型，
    最终由 bp_exporter / bp_importer 序列化/反序列化。
    """

    def __init__(
        self,
        path:    str = "",
        parent:  str = "/Script/Engine.Actor",
        bp_type: str = "Normal",
        **legacy: Any,
    ):
        gate = str(legacy.pop("gate", "") or "")
        name = str(legacy.pop("name", "") or "")
        if not path:
            path = gate
        self._path      = path
        self._parent    = parent
        self._bp_type   = bp_type
        self._name      = name or (path.split("/")[-1].split(".")[-1] if path else "")
        self._interfaces:  List[str]         = []
        self._variables:   List[_Variable]   = []
        self._dispatchers: List[_Dispatcher] = []
        self._components:  List[_Component]  = []
        self._graphs:      List[_Graph]      = []
        self._timelines:   List[_Timeline]   = []

    # ── Interfaces ────────────────────────────────────────────

    def interface(self, interface_path: str) -> "Blueprint":
        self._interfaces.append(interface_path)
        return self

    # ── Variables ─────────────────────────────────────────────

    def var(self, name: str, type_str: Optional[str] = None, *,
            type: Optional[str] = None,
            container: str = "single",
            default: Any = None,
            category: str = "",
            replicated: bool = False,
            rep_notify: str = "",
            instance_editable: bool = False,
            tooltip: str = "") -> "Blueprint":
        resolved_type = type_str or type or ""
        self._variables.append(_Variable(
            name=name, type_str=resolved_type, container=container, default=default,
            category=category, replicated=replicated,
            rep_notify=rep_notify,
            instance_editable=instance_editable,
            tooltip=tooltip,
        ))
        return self

    def build(self) -> "Blueprint":
        """兼容旧示例中的 build() 调用。"""
        return self

    # ── Event Dispatchers ─────────────────────────────────────

    def dispatcher(self, name: str,
                   params: Optional[List[Tuple[str, str]]] = None) -> "Blueprint":
        self._dispatchers.append(_Dispatcher(name=name, params=params or []))
        return self

    # ── Components ────────────────────────────────────────────

    def component(self, name: str, class_name: str, *,
                  parent: Optional[str] = None,
                  properties: Optional[Dict[str, Any]] = None) -> "Blueprint":
        self._components.append(_Component(
            name=name, class_name=class_name,
            parent=parent, properties=properties or {},
        ))
        return self

    # ── Graphs ────────────────────────────────────────────────

    def event_graph(self, name: str = "EventGraph") -> GraphContext:
        g = _Graph(name=name, graph_type="event_graph")
        return GraphContext(self, g)

    def function(self, name: str, *,
                 inputs:   Optional[List[Tuple[str, str]]] = None,
                 outputs:  Optional[List[Tuple[str, str]]] = None,
                 pure:     bool = False,
                 category: str  = "") -> GraphContext:
        g = _Graph(
            name=name, graph_type="function",
            inputs=inputs or [], outputs=outputs or [],
            is_pure=pure, category=category,
        )
        return GraphContext(self, g)

    def macro(self, name: str, *,
              inputs:  Optional[List[Tuple[str, str]]] = None,
              outputs: Optional[List[Tuple[str, str]]] = None) -> GraphContext:
        g = _Graph(
            name=name, graph_type="macro",
            inputs=inputs or [], outputs=outputs or [],
        )
        return GraphContext(self, g)

    # ── Timelines ─────────────────────────────────────────────

    def timeline(self, name: str, *,
                 length:            float = 1.0,
                 looping:           bool  = False,
                 autoplay:          bool  = False,
                 use_last_keyframe: bool  = True,
                 tracks: Optional[List[_TimelineTrack]] = None) -> "Blueprint":
        self._timelines.append(_Timeline(
            name=name, length=length, looping=looping,
            autoplay=autoplay, use_last_keyframe=use_last_keyframe,
            tracks=tracks or [],
        ))
        return self

    # ── Serialisation ─────────────────────────────────────────

    def to_dict(self) -> Dict[str, Any]:
        """序列化为 dict，供 JSON 导出或 C++ 导入器使用。"""
        return {
            "path":       self._path,
            "parent":     self._parent,
            "bp_type":    self._bp_type,
            "name":       self._name,
            "interfaces": self._interfaces,
            "variables": [
                {
                    "name":              v.name,
                    "type":              v.type_str,
                    "container":         v.container,
                    "default":           str(v.default) if v.default is not None else "",
                    "category":          v.category,
                    "replicated":        v.replicated,
                    "rep_notify":        v.rep_notify,
                    "instance_editable": v.instance_editable,
                    "tooltip":           v.tooltip,
                }
                for v in self._variables
            ],
            "dispatchers": [
                {"name": d.name, "params": d.params}
                for d in self._dispatchers
            ],
            "components": [
                {
                    "name":       c.name,
                    "class_name": c.class_name,
                    "parent":     c.parent or "",
                    "properties": c.properties,
                }
                for c in self._components
            ],
            "graphs": [_serialize_graph(g) for g in self._graphs],
            "timelines": [_serialize_timeline(t) for t in self._timelines],
        }


# ═══════════════════════════════════════════════════════════════
#  Serialisation helpers
# ═══════════════════════════════════════════════════════════════

def _serialize_graph(g: _Graph) -> Dict[str, Any]:
    node_by_uid = {n.uid: n for n in g.nodes}

    return {
        "name":       g.name,
        "graph_type": g.graph_type,
        "inputs":     [{"name": n, "type": t} for n, t in g.inputs],
        "outputs":    [{"name": n, "type": t} for n, t in g.outputs],
        "is_pure":    g.is_pure,
        "category":   g.category,
        "nodes": [
            {
                "uid":          n.uid,
                "node_class":   n.node_class,
                "member_name":  n.member_name,
                "function_ref": n.function_ref,
                "tunnel_type":  n.tunnel_type,
                "target_type":  n.target_type,
                "custom_params": [
                    {"name": param_name, "type": param_type}
                    for param_name, param_type in (n.custom_params or [])
                ],
                "defaults":     n.defaults,
                "pos_x":        n.pos_x,
                "pos_y":        n.pos_y,
                "readable_name": n.readable_name,
                "node_guid":    n.node_guid,
                "node_props":   n.extra_props,
                "pin_aliases":  n.pin_aliases,
                "pin_ids":      n.pin_ids,
            }
            for n in g.nodes
        ],
        "connections": [
            {
                "src_node": c.src_node_uid,
                "src_pin":  c.src_pin,
                "dst_node": c.dst_node_uid,
                "dst_pin":  c.dst_pin,
                "src_pin_full": c.src_pin_full
                    or node_by_uid.get(c.src_node_uid, _Node(uid="", node_class="")).pin_aliases.get(c.src_pin, ""),
                "dst_pin_full": c.dst_pin_full
                    or node_by_uid.get(c.dst_node_uid, _Node(uid="", node_class="")).pin_aliases.get(c.dst_pin, ""),
                "src_pin_id": c.src_pin_id
                    or node_by_uid.get(c.src_node_uid, _Node(uid="", node_class="")).pin_ids.get(c.src_pin, ""),
                "dst_pin_id": c.dst_pin_id
                    or node_by_uid.get(c.dst_node_uid, _Node(uid="", node_class="")).pin_ids.get(c.dst_pin, ""),
            }
            for c in g.connections
        ],
        "metadata": g.metadata,
    }


def _serialize_timeline(t: _Timeline) -> Dict[str, Any]:
    return {
        "name":              t.name,
        "length":            t.length,
        "looping":           t.looping,
        "autoplay":          t.autoplay,
        "use_last_keyframe": t.use_last_keyframe,
        "tracks": [
            {
                "name":       tr.name,
                "track_type": tr.track_type,
                "keys": [
                    {
                        "time":           k.time,
                        "value":          k.value,
                        "interp":         k.interp,
                        "arrive_tangent": k.arrive_tangent,
                        "leave_tangent":  k.leave_tangent,
                    }
                    for k in tr.keys
                ],
            }
            for tr in t.tracks
        ],
    }

# ═══════════════════════════════════════════════════════════════
#  DSL helper functions (used in .bp.py scripts)
# ═══════════════════════════════════════════════════════════════

def float_track(name: str, keys: List[_TimelineKey]) -> _TimelineTrack:
    return _TimelineTrack(name=name, track_type="float", keys=keys)

def vector_track(name: str, keys: List[_TimelineKey]) -> _TimelineTrack:
    return _TimelineTrack(name=name, track_type="vector", keys=keys)

def color_track(name: str, keys: List[_TimelineKey]) -> _TimelineTrack:
    return _TimelineTrack(name=name, track_type="color", keys=keys)

def event_track(name: str, keys: List[_TimelineKey]) -> _TimelineTrack:
    return _TimelineTrack(name=name, track_type="event", keys=keys)

def key(time: float, value: Any = None, *,
        interp: str = "linear",
        arrive_tangent: float = 0.0,
        leave_tangent:  float = 0.0) -> _TimelineKey:
    return _TimelineKey(
        time=time, value=value, interp=interp,
        arrive_tangent=arrive_tangent, leave_tangent=leave_tangent,
    )

def asset(path: str) -> str:
    """资产软引用，导出为字符串。"""
    return f"asset:{path}"

def soft_ref(path: str) -> str:
    return f"soft:{path}"

def class_ref(path: str) -> str:
    return f"class:{path}"

def vec3(x: float, y: float, z: float) -> tuple:
    return (x, y, z)

def struct(struct_type: str, **fields) -> Dict[str, Any]:
    return {"__struct__": struct_type, **fields}

def enum(enum_type: str, value: str) -> str:
    return f"enum:{enum_type}::{value}"
