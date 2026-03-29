# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, Optional


@dataclass
class SymbolValue:
    value: Any
    type_str: Optional[str] = None


class SymbolTable:
    def __init__(self) -> None:
        self._symbols: Dict[str, SymbolValue] = {}
        self._break_cache: Dict[tuple[str, str, str], Any] = {}
        self._cast_cache: Dict[str, Any] = {}

    def set(self, name: str, value: Any, type_str: Optional[str] = None) -> None:
        self._symbols[name] = SymbolValue(value=value, type_str=type_str)

    def get(self, name: str) -> SymbolValue:
        return self._symbols[name]

    def has(self, name: str) -> bool:
        return name in self._symbols

    def remember_break(self, key: tuple[str, str, str], node: Any) -> None:
        self._break_cache[key] = node

    def get_break(self, key: tuple[str, str, str]) -> Any:
        return self._break_cache.get(key)

    def remember_cast(self, name: str, binding: Any) -> None:
        self._cast_cache[name] = binding

    def get_cast(self, name: str) -> Any:
        return self._cast_cache.get(name)
