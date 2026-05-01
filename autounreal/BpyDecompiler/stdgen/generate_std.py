from __future__ import annotations

import argparse
import ast
import importlib.util
import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable


@dataclass(slots=True)
class StdFunction:
    name: str
    ue_ref: str
    params: list[str]
    returns: list[str]
    pure: bool
    source: str


_IDENTIFIER_RE = re.compile(r"\W+")
_CALL_RE = re.compile(r"g\.call\(\s*['\"]([^'\"]+)['\"]")
_NODE_TYPE_RE = re.compile(r"g\.node\(.*?type\s*=\s*['\"]([^'\"]+)['\"]")
_UFUNCTION_RE = re.compile(r"UFUNCTION\s*\([^)]*\)\s*(?:[\w:\<\>]+\s+)*([A-Za-z_]\w*)\s*\(([^)]*)\)", re.S)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Generate BpyDecompiler upper std/ Python API stubs from ExportBpy maps, exported .bp.py, and optional UE source headers.")
    parser.add_argument("--project-root", default=".")
    parser.add_argument("--ue-root", default=r"E:\unreal_engine\UE_5.7")
    parser.add_argument("--output", default="Plugins/autounreal/autounreal/BpyDecompiler/std")
    parser.add_argument("--exported-bpy", default="ExportedBlueprints/bpy")
    parser.add_argument("--max-headers", type=int, default=20000)
    parser.add_argument("--reflection-json", default="", help="Optional JSON exported by stdgen/ue_reflection_export.py running inside Unreal Editor.")
    args = parser.parse_args(argv)

    project_root = Path(args.project_root).resolve()
    output = (project_root / args.output).resolve() if not Path(args.output).is_absolute() else Path(args.output).resolve()
    functions: dict[str, StdFunction] = {}
    _collect_function_map(project_root, functions)
    _collect_exported_bpy(project_root / args.exported_bpy, functions)
    _collect_reflection_json(Path(args.reflection_json), functions)
    _collect_ue_headers(Path(args.ue_root), functions, args.max_headers)
    _write_std(output, functions, Path(args.ue_root), project_root)
    print(f"generated {len(functions)} std functions -> {output}")
    return 0


def _collect_function_map(project_root: Path, functions: dict[str, StdFunction]) -> None:
    path = project_root / "Plugins/autounreal/autounreal/ExportBpy/Content/Python/bpy_compile/maps/function_map.py"
    if not path.is_file():
        return
    spec = importlib.util.spec_from_file_location("_bpy_std_function_map", path)
    if spec is None or spec.loader is None:
        return
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
    finally:
        sys.modules.pop(spec.name, None)
    for name, fn_spec in getattr(module, "FUNCTION_MAP", {}).items():
        ue_ref = getattr(fn_spec, "ue_ref", "")
        if not isinstance(ue_ref, str) or not ue_ref:
            continue
        params = list(getattr(fn_spec, "param_order", ()) or getattr(fn_spec, "params", {}).keys())
        returns = list(getattr(fn_spec, "returns", ("ReturnValue",)))
        pure = bool(getattr(fn_spec, "pure", True))
        _add(functions, name, ue_ref, params, returns, pure, "function_map")


def _collect_exported_bpy(root: Path, functions: dict[str, StdFunction]) -> None:
    if not root.is_dir():
        return
    for path in root.rglob("*.bp.py"):
        if path.name.endswith("_meta.py"):
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for match in _CALL_RE.finditer(text):
            ue_ref = match.group(1)
            _add(functions, _safe_name(ue_ref), ue_ref, [], ["ReturnValue"], True, "exported_bpy")
        for match in _NODE_TYPE_RE.finditer(text):
            node_type = match.group(1)
            _add(functions, _safe_name(node_type), node_type, [], ["ReturnValue"], True, "exported_bpy_node")


def _collect_reflection_json(path: Path, functions: dict[str, StdFunction]) -> None:
    if not path.is_file():
        return
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return
    for item in payload.get("functions", []):
        name = item.get("name")
        ue_ref = item.get("ue_ref")
        if not isinstance(name, str) or not isinstance(ue_ref, str):
            continue
        params = item.get("params", []) if isinstance(item.get("params", []), list) else []
        returns = item.get("returns", ["ReturnValue"]) if isinstance(item.get("returns", []), list) else ["ReturnValue"]
        _add(functions, name, ue_ref, params, returns, bool(item.get("pure", True)), "ue_reflection")


def _collect_ue_headers(ue_root: Path, functions: dict[str, StdFunction], max_headers: int) -> None:
    source_root = ue_root / "Engine/Source"
    if not source_root.is_dir():
        return
    count = 0
    for path in source_root.rglob("*.h"):
        count += 1
        if count > max_headers:
            break
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        class_name = _class_name(text) or path.stem
        for match in _UFUNCTION_RE.finditer(text):
            fn_name = match.group(1)
            params = [_safe_name(part.strip().split()[-1].lstrip("*&")) for part in match.group(2).split(",") if part.strip() and part.strip() != "void"]
            ue_ref = f"{class_name}::{fn_name}"
            _add(functions, _safe_name(ue_ref), ue_ref, params, ["ReturnValue"], True, "ue_header")


def _class_name(text: str) -> str | None:
    match = re.search(r"class\s+(?:\w+_API\s+)?([A-Z]\w+)", text)
    return match.group(1) if match else None


def _add(functions: dict[str, StdFunction], name: str, ue_ref: str, params: Iterable[str], returns: Iterable[str], pure: bool, source: str) -> None:
    name = _safe_name(name)
    if not name or name in functions:
        return
    clean_params = []
    for param in params:
        safe = _safe_name(str(param))
        if safe and safe not in clean_params:
            clean_params.append(safe)
    functions[name] = StdFunction(name, ue_ref, clean_params, list(returns), pure, source)


def _safe_name(value: str) -> str:
    text = value.replace("::", "_")
    text = _IDENTIFIER_RE.sub("_", text).strip("_")
    if not text:
        return "value"
    if text[0].isdigit():
        text = f"fn_{text}"
    return text


def _write_std(output: Path, functions: dict[str, StdFunction], ue_root: Path, project_root: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    sorted_functions = sorted(functions.values(), key=lambda item: item.name.lower())
    (output / "manifest.json").write_text(json.dumps({
        "ue_root": str(ue_root),
        "project_root": str(project_root),
        "function_count": len(sorted_functions),
        "functions": [asdict(item) for item in sorted_functions],
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    (output / "__init__.py").write_text(_render_init(sorted_functions), encoding="utf-8", newline="\n")
    (output / "ue.py").write_text(_render_ue(sorted_functions), encoding="utf-8", newline="\n")
    (output / "anim.py").write_text(_render_anim(), encoding="utf-8", newline="\n")
    (output / "input.py").write_text(_render_input(), encoding="utf-8", newline="\n")


def _render_init(functions: list[StdFunction]) -> str:
    names = [item.name for item in functions]
    return "from .ue import *\nfrom .anim import *\nfrom .input import *\n\n__all__ = " + repr([*names, "AnimNode", "InputAction", "InputKey", "replace", "output", "function", "event", "cast_as", "select", "event_payload"]) + "\n"


def _render_ue(functions: list[StdFunction]) -> str:
    lines = [
        "from __future__ import annotations",
        "",
        "from dataclasses import dataclass, replace as _dc_replace",
        "from types import SimpleNamespace",
        "from typing import Any",
        "",
        "def replace(target: Any, **fields: Any) -> Any:",
        "    if hasattr(target, '__dataclass_fields__'):",
        "        return _dc_replace(target, **fields)",
        "    data = dict(getattr(target, '__dict__', {}))",
        "    data.update(fields)",
        "    return SimpleNamespace(**data)",
        "",
        "def property(path: str | None = None, **kwargs: Any) -> Any:",
        "    return _ue_call('PropertyAccess', path=path, **kwargs)",
        "",
        "def _ue_call(name: str, *args: Any, **kwargs: Any) -> Any:",
        "    return SimpleNamespace(__ue_call__=name, args=args, kwargs=kwargs)",
        "",
        "def output(**fields: Any) -> Any:",
        "    return SimpleNamespace(**fields)",
        "",
        "def function(**metadata: Any):",
        "    def decorate(fn: Any) -> Any:",
        "        setattr(fn, \"__bp_function__\", metadata)",
        "        return fn",
        "    return decorate",
        "",
        "def event(**metadata: Any):",
        "    def decorate(fn: Any) -> Any:",
        "        setattr(fn, \"__bp_event__\", metadata)",
        "        return fn",
        "    return decorate",
        "",
        "def cast_as(value: Any, type_name: str) -> Any:",
        "    return value",
        "",
        "def select(condition: Any, when_false: Any, when_true: Any) -> Any:",
        "    return when_true if condition else when_false",
        "",
    ]
    for fn in functions:
        params = [p for p in fn.params if p]
        signature = ", ".join([*params, "**kwargs: Any"] if params else ["**kwargs: Any"])
        call_args = ", ".join(params)
        if call_args:
            call_args += ", "
        lines.extend([
            f"def {fn.name}({signature}) -> Any:",
            f"    return _ue_call({fn.ue_ref!r}, {call_args}**kwargs)",
            "",
        ])
    return "\n".join(lines)


def _render_anim() -> str:
    return '''from __future__ import annotations\n\nfrom dataclasses import dataclass, field\nfrom typing import Any\n\n@dataclass\nclass AnimNode:\n    node_type: str\n    inputs: dict[str, Any] = field(default_factory=dict)\n    settings: Any = None\n\ndef make_anim_node(node_type: str, **kwargs: Any) -> AnimNode:\n    settings = kwargs.pop("settings", None)\n    return AnimNode(node_type=node_type, inputs=kwargs, settings=settings)\n'''


def _render_input() -> str:
    return '''from __future__ import annotations\n\nfrom dataclasses import dataclass\n\n@dataclass(frozen=True)\nclass InputActionState:\n    action: str\n    Triggered: bool = True\n    Started: bool = True\n    Completed: bool = True\n    Canceled: bool = False\n\ndef InputAction(action: str) -> InputActionState:\n    return InputActionState(action)\n\n@dataclass(frozen=True)\nclass InputKeyState:\n    key: str\n    Pressed: bool = True\n    Released: bool = True\n\ndef InputKey(key: str) -> InputKeyState:\n    return InputKeyState(key)\n'''


if __name__ == "__main__":
    raise SystemExit(main())
