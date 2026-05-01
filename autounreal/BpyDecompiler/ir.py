from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


@dataclass(slots=True)
class Diagnostic:
    level: str
    message: str
    graph: str | None = None
    node: str | None = None


@dataclass(slots=True)
class PinRefIR:
    node: str
    pin: str


@dataclass(slots=True)
class EdgeIR:
    src: PinRefIR
    dst: PinRefIR
    is_exec: bool = False


@dataclass(slots=True)
class NodeIR:
    name: str
    kind: str
    target: str | None = None
    args: list[Any] = field(default_factory=list)
    kwargs: dict[str, Any] = field(default_factory=dict)
    defaults: dict[str, Any] = field(default_factory=dict)
    props: dict[str, Any] = field(default_factory=dict)


@dataclass(slots=True)
class GraphIR:
    stem: str
    graph_name: str
    kind: str
    source_path: Path
    meta: dict[str, Any] = field(default_factory=dict)
    inputs: list[tuple[str, str]] = field(default_factory=list)
    outputs: list[tuple[str, str]] = field(default_factory=list)
    pure: bool = False
    entry: str | None = None
    context_kwargs: dict[str, Any] = field(default_factory=dict)
    nodes: dict[str, NodeIR] = field(default_factory=dict)
    edges: list[EdgeIR] = field(default_factory=list)
    diagnostics: list[Diagnostic] = field(default_factory=list)


@dataclass(slots=True)
class RootCstIR:
    graph_modules: list[str] = field(default_factory=list)
    blueprint_call: str = ""
    variables: list[str] = field(default_factory=list)
    defaults: list[str] = field(default_factory=list)
    inherited_component_defaults: list[str] = field(default_factory=list)
    components: list[str] = field(default_factory=list)
    interfaces: list[str] = field(default_factory=list)
    event_dispatchers: list[str] = field(default_factory=list)


@dataclass(slots=True)
class GraphCstIR:
    file_name: str
    context_expr: str
    is_sidecar: bool = False
    has_sidecar_loader: bool = False
    blueprint_call: str = ""
    footer: list[str] = field(default_factory=list)
    connections: list[str] = field(default_factory=list)
    nodes: list[str] = field(default_factory=list)
    data_edges: list[str] = field(default_factory=list)
    exec_edges: list[str] = field(default_factory=list)


@dataclass(slots=True)
class RoundtripCstIR:
    root: RootCstIR = field(default_factory=RootCstIR)
    graphs: list[GraphCstIR] = field(default_factory=list)


@dataclass(slots=True)
class BlueprintIR:
    source_dir: Path
    name: str
    path: str
    parent: str
    bp_type: str
    variables: list[dict[str, Any]] = field(default_factory=list)
    defaults: list[tuple[str, Any]] = field(default_factory=list)
    components: list[dict[str, Any]] = field(default_factory=list)
    interfaces: list[str] = field(default_factory=list)
    graph_stems: list[str] = field(default_factory=list)
    graphs: list[GraphIR] = field(default_factory=list)
    diagnostics: list[Diagnostic] = field(default_factory=list)
    roundtrip_cst: RoundtripCstIR = field(default_factory=RoundtripCstIR)
