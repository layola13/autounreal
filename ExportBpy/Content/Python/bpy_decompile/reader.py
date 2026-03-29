"""
Read a low-level ExportBpy package and build graph data for decompile.

Package layout:
    __bp__.bp.py
    evt_<name>.bp.py
    fn_<name>.bp.py
    evt_<name>_meta.py
    fn_<name>_meta.py
"""

from __future__ import annotations

import ast
import os
import re
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple


@dataclass
class NodeInfo:
    var_name: str
    node_type: str
    name: str = ""
    class_name: str = ""
    pos: Tuple[int, int] = (0, 0)
    node_props: Dict[str, str] = field(default_factory=dict)
    defaults: Dict[str, str] = field(default_factory=dict)


@dataclass
class Connection:
    src_var: str
    src_pin: str
    dst_var: str
    dst_pin: str


@dataclass
class GraphInfo:
    graph_kind: str
    graph_name: str
    nodes: List[NodeInfo] = field(default_factory=list)
    connections: List[Connection] = field(default_factory=list)
    node_guid: Dict[str, str] = field(default_factory=dict)
    node_pos: Dict[str, Tuple[int, int]] = field(default_factory=dict)
    pin_alias: Dict[str, str] = field(default_factory=dict)
    pin_id: Dict[str, str] = field(default_factory=dict)
    node_props_meta: Dict[str, Dict[str, str]] = field(default_factory=dict)


@dataclass
class BPPackage:
    bp_path: str = ""
    parent_class: str = ""
    bp_type: str = "Normal"
    variables: List[Dict[str, Any]] = field(default_factory=list)
    components: List[Dict[str, str]] = field(default_factory=list)
    graphs: List[GraphInfo] = field(default_factory=list)


def _ast_extract(path: str, src: str) -> Dict[str, Any]:
    ns: Dict[str, Any] = {}
    try:
        tree = ast.parse(src, filename=path)
        for node in ast.walk(tree):
            if not isinstance(node, ast.Assign):
                continue
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id == "META":
                    try:
                        ns["META"] = ast.literal_eval(node.value)
                    except Exception:
                        pass
    except Exception:
        pass
    return ns


class _Pin:
    def __init__(self, var_name: str, pin_name: str, graph: "_GraphCapture"):
        self.var_name = var_name
        self.pin_name = pin_name
        self._graph = graph

    def __rshift__(self, other: "_Pin"):
        self._graph._connections.append(
            Connection(self.var_name, self.pin_name, other.var_name, other.pin_name)
        )


class _NodeProxy:
    def __init__(self, var_name: str, node_info: NodeInfo, graph: "_GraphCapture"):
        self._var_name = var_name
        self._node_info = node_info
        self._graph = graph

    def __getitem__(self, pin_name: str) -> _Pin:
        return _Pin(self._var_name, pin_name, self._graph)

    def __rshift__(self, other):
        src = _Pin(self._var_name, "then", self._graph)
        if isinstance(other, _NodeProxy):
            dst = _Pin(other._var_name, "exec", other._graph)
            src.__rshift__(dst)
        elif isinstance(other, _Pin):
            src.__rshift__(other)


_current_graph: Optional["_GraphCapture"] = None


class _GraphCapture:
    def __init__(self, kind: str, name: str):
        self.kind = kind
        self.name = name
        self._nodes: List[NodeInfo] = []
        self._connections: List[Connection] = []
        self._node_map: Dict[str, NodeInfo] = {}

    def __enter__(self):
        global _current_graph
        _current_graph = self
        return self

    def __exit__(self, *_):
        global _current_graph
        _current_graph = None

    def node(self, type: str = "", name: str = "", class_name: str = "",
             pos: Tuple[int, int] = (0, 0)) -> _NodeProxy:
        base = _make_var_name(name or type)
        var_name = base
        index = 0
        while var_name in self._node_map:
            index += 1
            var_name = f"{base}{index}"

        node_info = NodeInfo(
            var_name=var_name,
            node_type=type,
            name=name,
            class_name=class_name,
            pos=pos,
        )
        self._nodes.append(node_info)
        self._node_map[var_name] = node_info
        return _NodeProxy(var_name, node_info, self)

    def set_default(self, node_proxy, pin_name: str, value: str):
        if isinstance(node_proxy, _NodeProxy):
            node_proxy._node_info.defaults[pin_name] = value


def _make_var_name(raw: str) -> str:
    value = re.sub(r"[^a-zA-Z0-9]", "_", raw).lower().strip("_")
    value = re.sub(r"_+", "_", value)
    return value or "node"


def _logical_stem(path: str) -> str:
    name = os.path.basename(path)
    if name.endswith(".bp.py"):
        return name[:-6]
    if name.endswith(".py"):
        return name[:-3]
    return os.path.splitext(name)[0]


class _BPCapture:
    def __init__(self):
        self.bp_path = ""
        self.parent = ""
        self.bp_type = "Normal"
        self.variables: List[Dict[str, Any]] = []
        self.components: List[Dict[str, str]] = []
        self._graphs_pending: List[_GraphCapture] = []

    def event_graph(self, name: str) -> _GraphCapture:
        graph = _GraphCapture("event_graph", name)
        self._graphs_pending.append(graph)
        return graph

    def function(self, name: str) -> _GraphCapture:
        graph = _GraphCapture("function", name)
        self._graphs_pending.append(graph)
        return graph

    def component(self, comp_name: str, class_name: str = "", parent: str = ""):
        self.components.append({"name": comp_name, "class_name": class_name, "parent": parent})

    def variable(self, var_name: str, **kwargs):
        kwargs["name"] = var_name
        self.variables.append(kwargs)

    def var(self, var_name: str, **kwargs):
        self.variable(var_name, **kwargs)

    def build(self):
        pass


def _parse_graph_file(path: str, kind: str) -> GraphInfo:
    with open(path, "r", encoding="utf-8") as handle:
        source = handle.read()

    bp_capture = _BPCapture()
    fake_globals: Dict[str, Any] = {
        "__file__": path,
        "__name__": "_bpy_decompile_graph",
        "bp": bp_capture,
    }

    try:
        exec(compile(source, path, "exec"), fake_globals)  # noqa: S102
        register_fn = fake_globals.get("register")
        if callable(register_fn):
            register_fn(bp_capture)
    except Exception as exc:
        stem = _logical_stem(path)
        graph_name = stem.replace("evt_", "").replace("fn_", "").replace("_", " ")
        graph = GraphInfo(graph_kind=kind, graph_name=graph_name)
        graph.node_props_meta["__parse_error__"] = {"error": str(exc)}
        return graph

    if bp_capture._graphs_pending:
        graph_capture = bp_capture._graphs_pending[-1]
        return GraphInfo(
            graph_kind=graph_capture.kind,
            graph_name=graph_capture.name,
            nodes=graph_capture._nodes,
            connections=graph_capture._connections,
        )

    stem = _logical_stem(path)
    graph_name = stem.replace("evt_", "").replace("fn_", "").replace("_", " ")
    return GraphInfo(graph_kind=kind, graph_name=graph_name)


def _merge_meta(graph: GraphInfo, meta_path: str):
    if not os.path.exists(meta_path):
        return

    with open(meta_path, "r", encoding="utf-8") as handle:
        meta = _ast_extract(meta_path, handle.read()).get("META", {})
    if not isinstance(meta, dict):
        return

    graph.node_guid = meta.get("node_guid", {})
    graph.node_pos = meta.get("node_pos", {})
    graph.pin_alias = meta.get("pin_alias", {})
    graph.pin_id = meta.get("pin_id", {})
    graph.node_props_meta = meta.get("node_props", {})

    for node in graph.nodes:
        props = graph.node_props_meta.get(node.var_name, {})
        if props:
            node.node_props.update(props)


def read_package(pkg_dir: str) -> BPPackage:
    pkg = BPPackage()

    bp_file = os.path.join(pkg_dir, "__bp__.bp.py")
    if os.path.exists(bp_file):
        _parse_bp_file(bp_file, pkg)

    try:
        entries = os.listdir(pkg_dir)
    except OSError:
        return pkg

    graph_files: List[Tuple[str, str, str]] = []
    for file_name in sorted(entries):
        if file_name.startswith("evt_") and file_name.endswith(".bp.py") and not file_name.endswith("_meta.py"):
            graph_files.append((_logical_stem(file_name), "event_graph", os.path.join(pkg_dir, file_name)))
        elif file_name.startswith("fn_") and file_name.endswith(".bp.py") and not file_name.endswith("_meta.py"):
            graph_files.append((_logical_stem(file_name), "function", os.path.join(pkg_dir, file_name)))

    for stem, kind, file_path in graph_files:
        graph = _parse_graph_file(file_path, kind)
        _merge_meta(graph, os.path.join(pkg_dir, stem + "_meta.py"))
        pkg.graphs.append(graph)

    return pkg


def _parse_bp_file(path: str, pkg: BPPackage):
    class _BPBuilder:
        def __call__(self_inner, path: str = "", gate: str = "", parent: str = "", bp_type: str = "Normal", **_):
            pkg.bp_path = path or gate
            pkg.parent_class = parent
            pkg.bp_type = bp_type
            return self_inner

        def component(self_inner, name: str, class_name: str = "", parent: str = ""):
            pkg.components.append({"name": name, "class_name": class_name, "parent": parent})
            return self_inner

        def var(self_inner, name: str, **kwargs):
            kwargs["name"] = name
            pkg.variables.append(kwargs)
            return self_inner

        def variable(self_inner, name: str, **kwargs):
            return self_inner.var(name, **kwargs)

        def interface(self_inner, interface_path: str):
            return self_inner

        def dispatcher(self_inner, name: str, params=None):
            return self_inner

        def build(self_inner):
            pass

    bp_builder = _BPBuilder()

    class _BlueprintClass:
        def __call__(self_inner, path: str = "", gate: str = "", parent: str = "", bp_type: str = "Normal", **_):
            pkg.bp_path = path or gate
            pkg.parent_class = parent
            pkg.bp_type = bp_type
            return bp_builder

    fake_globals: Dict[str, Any] = {
        "__file__": path,
        "__name__": "_bpy_decompile_bp",
        "Blueprint": _BlueprintClass(),
        "bp": bp_builder,
    }

    try:
        with open(path, "r", encoding="utf-8") as handle:
            source = handle.read()
        exec(compile(source, path, "exec"), fake_globals)  # noqa: S102
    except Exception:
        pass
