# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-

from __future__ import annotations

from typing import Iterable, List

from ue_bp_dsl.core import NodeProxy, PinRef


def connect_execs(tails: Iterable[PinRef], dest_pin: PinRef) -> None:
    for tail in tails:
        tail >> dest_pin


def append_exec_node(tails: Iterable[PinRef], node: NodeProxy) -> List[PinRef]:
    connect_execs(tails, node.exec)
    return [node.then]


def merge_exec_tails(*groups: Iterable[PinRef]) -> List[PinRef]:
    merged: List[PinRef] = []
    for group in groups:
        merged.extend(group)
    return merged
