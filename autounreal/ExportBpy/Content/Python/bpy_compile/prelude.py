# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-
"""Compile-time helpers imported by upper package source files."""

from __future__ import annotations

from typing import Any, Callable, Dict, Optional


def bp_function(
    *,
    name: Optional[str] = None,
    inputs: Optional[list[tuple[str, str]]] = None,
    outputs: Optional[list[tuple[str, str]]] = None,
    pure: bool = False,
) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
    def decorator(fn: Callable[..., Any]) -> Callable[..., Any]:
        fn.__bp_graph__ = {  # type: ignore[attr-defined]
            "kind": "function",
            "name": name,
            "inputs": inputs or [],
            "outputs": outputs or [],
            "pure": pure,
        }
        return fn

    return decorator


def bp_event(
    *,
    name: str = "EventGraph",
    entry: Optional[str] = None,
) -> Callable[[Callable[..., Any]], Callable[..., Any]]:
    def decorator(fn: Callable[..., Any]) -> Callable[..., Any]:
        fn.__bp_graph__ = {  # type: ignore[attr-defined]
            "kind": "event_graph",
            "name": name,
            "entry": entry,
        }
        return fn

    return decorator


def result(**kwargs: Any) -> Dict[str, Any]:
    return {"__bp_result__": True, **kwargs}


def cast(obj: Any, cls: Any) -> tuple[Any, Any]:
    return (obj, cls)


def select(condition: Any, when_false: Any, when_true: Any) -> tuple[Any, Any, Any]:
    return (condition, when_false, when_true)
