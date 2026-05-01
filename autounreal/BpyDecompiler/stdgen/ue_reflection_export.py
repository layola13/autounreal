from __future__ import annotations

import inspect
import json
import os
import re
import sys
from pathlib import Path

import unreal


def _safe_name(value):
    text = str(value).replace("::", "_")
    text = re.sub(r"\W+", "_", text).strip("_")
    if not text:
        return "value"
    if text[0].isdigit():
        text = "fn_" + text
    return text


def _iter_unreal_wrappers():
    seen = set()
    for attr in dir(unreal):
        if attr.startswith("_"):
            continue
        value = getattr(unreal, attr, None)
        if value is None or attr in seen:
            continue
        seen.add(attr)
        yield attr, value


def _signature_params(callable_obj):
    try:
        signature = inspect.signature(callable_obj)
    except Exception:
        return []
    params = []
    for name, param in signature.parameters.items():
        if name in {"self", "cls"}:
            continue
        if param.kind in (inspect.Parameter.VAR_POSITIONAL, inspect.Parameter.VAR_KEYWORD):
            continue
        params.append(_safe_name(name))
    return params


def _function_flags(function):
    names = []
    try:
        flags = int(function.get_function_flags())
    except Exception:
        flags = 0
    function_flags = getattr(unreal, "FunctionFlags", None)
    if function_flags is not None:
        for attr in dir(function_flags):
            if attr.startswith("_"):
                continue
            try:
                value = int(getattr(function_flags, attr))
            except Exception:
                continue
            if value and flags & value:
                names.append(attr)
    return names


def _iter_classes():
    for _, value in _iter_unreal_wrappers():
        if not hasattr(value, "static_class"):
            continue
        try:
            yield value.static_class(), value
        except Exception:
            continue


def _function_params(function):
    params = []
    returns = []
    try:
        fields = function.get_properties()
    except Exception:
        fields = []
    for prop in fields:
        try:
            name = str(prop.get_name())
        except Exception:
            continue
        if name == "ReturnValue":
            returns.append(name)
        else:
            params.append(_safe_name(name))
    return params, returns or ["ReturnValue"]


def _out_path():
    env_path = os.environ.get("BPYDECOMPILER_REFLECTION_JSON")
    if env_path:
        return Path(env_path)
    argv = [arg for arg in sys.argv[1:] if arg and arg != "--"]
    if argv:
        return Path(argv[-1])
    return Path(unreal.Paths.project_saved_dir()) / "BpyDecompiler" / "ue_reflection_std.json"


def main():
    out_path = _out_path()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    functions = []
    seen = set()

    for class_attr, wrapper in _iter_unreal_wrappers():
        for member_name in dir(wrapper):
            if member_name.startswith("_"):
                continue
            try:
                member = getattr(wrapper, member_name)
            except Exception:
                continue
            if not callable(member):
                continue
            key = f"{class_attr}::{member_name}"
            if key in seen:
                continue
            seen.add(key)
            functions.append({
                "name": _safe_name(key),
                "ue_ref": key,
                "params": _signature_params(member),
                "returns": ["ReturnValue"],
                "pure": True,
                "source": "ue_reflection",
                "class_path": class_attr,
                "flags": ["python_wrapper"],
            })

    for cls, _wrapper in _iter_classes():
        try:
            class_name = str(cls.get_name())
            class_path = str(cls.get_path_name())
        except Exception:
            continue
        try:
            funcs = cls.get_functions()
        except Exception:
            funcs = []
        for fn in funcs:
            try:
                fn_name = str(fn.get_name())
            except Exception:
                continue
            key = f"{class_name}::{fn_name}"
            if key in seen:
                continue
            seen.add(key)
            params, returns = _function_params(fn)
            flags = _function_flags(fn)
            functions.append({
                "name": _safe_name(key),
                "ue_ref": key,
                "params": params,
                "returns": returns,
                "pure": "FUNC_BlueprintPure" in flags or "BLUEPRINT_PURE" in flags,
                "source": "ue_reflection",
                "class_path": class_path,
                "flags": flags,
            })

    payload = {"function_count": len(functions), "functions": functions}
    out_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log(f"BpyDecompiler reflection std exported {len(functions)} functions to {out_path}")


if __name__ == "__main__":
    main()
