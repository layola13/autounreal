from __future__ import annotations

import importlib.util
import sys
from pathlib import Path


def load_reverse_function_map(start: Path) -> dict[str, str]:
    for parent in [start, *start.parents]:
        candidate = parent / "ExportBpy" / "Content" / "Python" / "bpy_compile" / "maps" / "function_map.py"
        if candidate.is_file():
            return _load(candidate)
        candidate = parent / "Plugins" / "autounreal" / "autounreal" / "ExportBpy" / "Content" / "Python" / "bpy_compile" / "maps" / "function_map.py"
        if candidate.is_file():
            return _load(candidate)
    return {}


def _load(path: Path) -> dict[str, str]:
    spec = importlib.util.spec_from_file_location("_bpy_decompiler_function_map", path)
    if spec is None or spec.loader is None:
        return {}
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
    finally:
        sys.modules.pop(spec.name, None)
    result: dict[str, str] = {}
    for py_name, fn_spec in getattr(module, "FUNCTION_MAP", {}).items():
        ue_ref = getattr(fn_spec, "ue_ref", None)
        if isinstance(ue_ref, str):
            result.setdefault(ue_ref, py_name)
    return result


def safe_identifier(value: str) -> str:
    import re

    text = re.sub(r"\W+", "_", value).strip("_")
    if not text:
        return "call"
    if text[0].isdigit():
        text = f"fn_{text}"
    return text

