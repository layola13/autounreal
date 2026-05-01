from __future__ import annotations

import ast
import json
import re
import shutil
from pathlib import Path
from typing import Any

from ..ir import BlueprintIR, EdgeIR, GraphIR, NodeIR
from ..parser import parse_blueprint_package


NODE_METHODS_WITH_TARGET = {
    "call", "get_var", "set_var", "break_struct", "make_struct", "event", "custom_event", "cast", "message",
    "switch_enum", "switch_int",
}


def emit_bpy_package_from_upper(upper_dir: str | Path, output_dir: str | Path) -> Path:
    upper = Path(upper_dir).resolve()
    out = Path(output_dir).resolve()
    manifest = upper / "__roundtrip_bpy__.json"
    if not manifest.is_file():
        raise FileNotFoundError(f"Upper package missing ExportBpy CST manifest: {manifest}")
    _assert_safe_compile_output(upper, out, label="upper")
    payload = json.loads(manifest.read_text(encoding="utf-8"))
    if payload.get("format") != "BpyDecompiler.ExportBpyCst.v2":
        raise ValueError(f"Unsupported roundtrip manifest format in {manifest}")
    return _emit_bpy_package_from_cst_payload(payload, out)


def emit_bpy_package_from_human(human_dir: str | Path, output_dir: str | Path) -> Path:
    human = Path(human_dir).resolve()
    out = Path(output_dir).resolve()
    manifest = human / ".bpy_meta.json"
    if not manifest.is_file():
        raise FileNotFoundError(f"Human package missing lossless metadata: {manifest}")
    _assert_safe_compile_output(human, out, label="human")
    payload = json.loads(manifest.read_text(encoding="utf-8"))
    if payload.get("format") != "BpyDecompiler.HumanSource.v1":
        raise ValueError(f"Unsupported human metadata format in {manifest}")
    if payload.get("compile_payload_format") != "BpyDecompiler.ExportBpyCst.v2":
        raise ValueError(f"Unsupported human compile payload format in {manifest}")
    compile_payload = dict(payload)
    compile_payload["format"] = "BpyDecompiler.ExportBpyCst.v2"
    compile_payload.pop("compile_payload_format", None)
    _apply_human_default_edits(compile_payload, human)
    _apply_human_logic_edits(compile_payload, human)
    return _emit_bpy_package_from_cst_payload(compile_payload, out)


def _apply_human_logic_edits(payload: dict[str, Any], human_dir: Path) -> None:
    blueprint_path = human_dir / "blueprint.py"
    if not blueprint_path.is_file():
        return
    source = blueprint_path.read_text(encoding="utf-8")
    for graph in payload.get("graphs", []):
        file_name = str(graph.get("file_name") or "")
        function_name = _function_name_for_graph_file(file_name)
        if not file_name or not function_name:
            continue

        enum_name = _enum_name_for_graph(graph)
        enum_to_token = _ENUM_VALUE_TO_TOKEN.get(enum_name or "")
        if enum_name and enum_to_token:
            enum_returns = _extract_enum_return_anchors(source, function_name, enum_name)
            if enum_returns:
                _rewrite_graph_return_enum_pins(payload, file_name, enum_returns, enum_to_token)

        struct_spec = _struct_return_spec_for_graph(graph)
        if struct_spec is not None:
            struct_name, field_pins = struct_spec
            struct_returns = _extract_struct_return_anchors(source, function_name, struct_name)
            if struct_returns:
                _rewrite_graph_return_struct_pins(payload, file_name, struct_returns, field_pins)

        call_edits = _extract_call_argument_anchors(source, function_name)
        if call_edits:
            _rewrite_graph_call_literal_pins(payload, file_name, call_edits)


_ENUM_VALUE_TO_TOKEN = {
    "Gait": {"WALK": "NewEnumerator0", "RUN": "NewEnumerator1", "SPRINT": "NewEnumerator2"},
    "RotationMode": {"ORIENT_TO_MOVEMENT": "NewEnumerator0", "STRAFE": "NewEnumerator1", "AIM": "NewEnumerator2"},
    "MovementMode": {"ON_GROUND": "NewEnumerator4", "IN_AIR": "NewEnumerator5", "SLIDING": "NewEnumerator6", "TRAVERSING": "NewEnumerator7"},
    "MovementDirection": {
        "FORWARD": "NewEnumerator0",
        "RIGHT": "NewEnumerator1",
        "BACKWARD": "NewEnumerator2",
        "LEFT": "NewEnumerator3",
        "FORWARD_RIGHT": "NewEnumerator4",
        "FORWARD_LEFT": "NewEnumerator5",
    },
    "Stance": {"STAND": "NewEnumerator0", "CROUCH": "NewEnumerator1"},
}


def _enum_name_for_graph(graph: dict[str, Any]) -> str | None:
    context = str(graph.get("context_expr") or "")
    match = re.search(r'outputs=\[\("ReturnValue",\s*"byte/[^"]*/E_(?P<name>[A-Za-z0-9_]+)\.[^"]+"\)\]', context)
    return match.group("name") if match else None


def _struct_return_spec_for_graph(graph: dict[str, Any]) -> tuple[str, dict[str, str]] | None:
    outputs = _outputs_for_graph(graph)
    if not outputs:
        return None
    return_type = next((pin_type for pin_name, pin_type in outputs if pin_name == "ReturnValue"), "")
    match = re.search(r"/S_(?P<name>[A-Za-z0-9_]+)\.S_[A-Za-z0-9_]+$", str(return_type))
    if not match:
        return None
    field_pins: dict[str, str] = {}
    for pin_name, _pin_type in outputs:
        field_match = re.match(r"ReturnValue_(?P<field>[A-Za-z0-9]+)_\d+_[A-Fa-f0-9]+$", str(pin_name))
        if field_match:
            field_pins[_snake_case_identifier(field_match.group("field"))] = str(pin_name)
    if not field_pins:
        return None
    return match.group("name"), field_pins


def _outputs_for_graph(graph: dict[str, Any]) -> list[tuple[str, str]]:
    context = str(graph.get("context_expr") or "")
    try:
        expr = ast.parse(context, mode="eval").body
    except SyntaxError:
        return []
    if not isinstance(expr, ast.Call):
        return []
    for keyword in expr.keywords:
        if keyword.arg != "outputs":
            continue
        try:
            value = ast.literal_eval(keyword.value)
        except Exception:
            return []
        if not isinstance(value, list):
            return []
        result: list[tuple[str, str]] = []
        for item in value:
            if isinstance(item, tuple) and len(item) == 2:
                result.append((str(item[0]), str(item[1])))
        return result
    return []


def _function_name_for_graph_file(file_name: str) -> str | None:
    if not file_name.startswith("fn_") or not file_name.endswith(".bp.py"):
        return None
    graph_name = file_name[3:-6]
    return _snake_case(graph_name)


def _snake_case_identifier(value: str) -> str:
    text = re.sub(r"[^0-9A-Za-z]+", "_", value).strip("_")
    text = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", "_", text)
    text = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", text)
    text = re.sub(r"_+", "_", text)
    return text.lower()


def _snake_case(value: str) -> str:
    return _snake_case_identifier(value)


def _extract_enum_return_anchors(source: str, function_name: str, enum_name: str) -> dict[str, str]:
    lines = source.splitlines()
    try:
        module = ast.parse(source)
    except SyntaxError:
        return {}
    for node in module.body:
        if not isinstance(node, ast.ClassDef):
            continue
        for item in node.body:
            if not isinstance(item, ast.FunctionDef) or item.name != function_name:
                continue
            result: dict[str, str] = {}
            returns = [child for child in ast.walk(item) if isinstance(child, ast.Return)]
            for child in sorted(returns, key=lambda item: item.lineno):
                value = child.value
                if not (
                    isinstance(value, ast.Attribute)
                    and isinstance(value.value, ast.Name)
                    and value.value.id == enum_name
                ):
                    continue
                line = lines[child.lineno - 1] if child.lineno - 1 < len(lines) else ""
                match = re.search(r"#\s*bpy:\s*(Return__\d+)\.ReturnValue", line)
                if match and match.group(1) not in result:
                    result[match.group(1)] = value.attr
            return result
    return {}


def _rewrite_graph_return_enum_pins(
    payload: dict[str, Any],
    file_name: str,
    enum_values_by_node: dict[str, str],
    enum_to_token: dict[str, str],
) -> None:
    graph = next((item for item in payload.get("graphs", []) if item.get("file_name") == file_name), None)
    if not graph:
        return
    for index, stmt in enumerate(graph.get("nodes", [])):
        text = str(stmt)
        match = re.match(r'(Return__\d+)\.pin\("ReturnValue",', text)
        if not match:
            continue
        node_name = match.group(1)
        enum_value = enum_values_by_node.get(node_name)
        token = enum_to_token.get(enum_value or "")
        if token and f'"{token}"' not in text:
            graph["nodes"][index] = re.sub(
                r'Return__(\d+)\.pin\("ReturnValue",\s*"[^"]+"\)',
                lambda pin_match: f'Return__{pin_match.group(1)}.pin("ReturnValue", "{token}")',
                text,
            )


def _extract_struct_return_anchors(source: str, function_name: str, struct_name: str) -> dict[str, dict[str, Any]]:
    lines = source.splitlines()
    try:
        module = ast.parse(source)
    except SyntaxError:
        return {}
    for node in module.body:
        if not isinstance(node, ast.ClassDef):
            continue
        for item in node.body:
            if not isinstance(item, ast.FunctionDef) or item.name != function_name:
                continue
            result: dict[str, dict[str, Any]] = {}
            returns = [child for child in ast.walk(item) if isinstance(child, ast.Return)]
            for child in sorted(returns, key=lambda item: item.lineno):
                value = child.value
                if not (
                    isinstance(value, ast.Call)
                    and isinstance(value.func, ast.Name)
                    and value.func.id == struct_name
                ):
                    continue
                line = lines[child.lineno - 1] if child.lineno - 1 < len(lines) else ""
                match = re.search(r"#\s*bpy:\s*(Return__\d+)(?:\s|\.|$)", line)
                if not match:
                    continue
                fields: dict[str, Any] = {}
                for keyword in value.keywords:
                    if keyword.arg is None:
                        continue
                    try:
                        fields[_snake_case_identifier(keyword.arg)] = ast.literal_eval(keyword.value)
                    except Exception:
                        pass
                if fields and match.group(1) not in result:
                    result[match.group(1)] = fields
            return result
    return {}


def _rewrite_graph_return_struct_pins(
    payload: dict[str, Any],
    file_name: str,
    values_by_node: dict[str, dict[str, Any]],
    field_pins: dict[str, str],
) -> None:
    graph = next((item for item in payload.get("graphs", []) if item.get("file_name") == file_name), None)
    if not graph:
        return
    reverse_pins = {pin: field for field, pin in field_pins.items()}
    for index, stmt in enumerate(graph.get("nodes", [])):
        text = str(stmt)
        match = re.match(r'(Return__\d+)\.pin\("([^"]+)",\s*(.+)\)$', text)
        if not match:
            continue
        node_name, pin_name = match.group(1), match.group(2)
        field_name = reverse_pins.get(pin_name)
        if field_name is None:
            continue
        node_values = values_by_node.get(node_name)
        if not node_values or field_name not in node_values:
            continue
        graph["nodes"][index] = f'{node_name}.pin("{pin_name}", {_render_pin_literal(node_values[field_name])})'


def _render_pin_literal(value: Any) -> str:
    return repr(value)


def _extract_call_argument_anchors(source: str, function_name: str) -> dict[str, dict[str, Any]]:
    lines = source.splitlines()
    try:
        module = ast.parse(source)
    except SyntaxError:
        return {}
    for node in module.body:
        if not isinstance(node, ast.ClassDef):
            continue
        for item in node.body:
            if not isinstance(item, ast.FunctionDef) or item.name != function_name:
                continue
            result: dict[str, dict[str, Any]] = {}
            call_nodes = [child for child in ast.walk(item) if isinstance(child, ast.Call)]
            for child in sorted(call_nodes, key=lambda item: item.lineno):
                if _call_is_return_value_constructor(child):
                    continue
                line_number = getattr(child, "end_lineno", child.lineno)
                line = lines[line_number - 1] if line_number - 1 < len(lines) else ""
                match = re.search(r"#\s*bpy:\s*(?!Return__)([A-Za-z_][A-Za-z0-9_]*)\s*$", line)
                if not match or match.group(1) in result:
                    continue
                kwargs: dict[str, Any] = {}
                for keyword in child.keywords:
                    if keyword.arg is None:
                        continue
                    try:
                        kwargs[_blueprint_pin_name(keyword.arg)] = ast.literal_eval(keyword.value)
                    except Exception:
                        pass
                if kwargs:
                    result[match.group(1)] = kwargs
            return result
    return {}


def _call_is_return_value_constructor(node: ast.Call) -> bool:
    if isinstance(node.func, ast.Name):
        return node.func.id and node.func.id[0].isupper()
    return False


def _blueprint_pin_name(name: str) -> str:
    aliases = {
        "self_": "self",
        "new_view_target": "NewViewTarget",
        "blend_time": "BlendTime",
        "blend_func": "BlendFunc",
        "blend_exp": "BlendExp",
        "b_lock_outgoing": "bLockOutgoing",
        "b_set_as_view_target": "bSetAsViewTarget",
        "activation_mode": "ActivationMode",
        "player_controller": "PlayerController",
        "child_index": "ChildIndex",
        "variable_name": "VariableName",
        "name": "VariableName",
    }
    if name in aliases:
        return aliases[name]
    parts = name.split("_")
    if len(parts) == 1:
        return name
    return "".join(part[:1].upper() + part[1:] for part in parts if part)


def _rewrite_graph_call_literal_pins(
    payload: dict[str, Any],
    file_name: str,
    call_edits: dict[str, dict[str, Any]],
) -> None:
    graph = next((item for item in payload.get("graphs", []) if item.get("file_name") == file_name), None)
    if not graph:
        return
    for index, stmt in enumerate(graph.get("nodes", [])):
        text = str(stmt)
        match = re.match(r'([A-Za-z_][A-Za-z0-9_]*)\.pin\("([^"]+)",\s*(.+)\)$', text)
        if not match:
            continue
        node_name, pin_name = match.group(1), match.group(2)
        node_edits = call_edits.get(node_name)
        if not node_edits or pin_name not in node_edits:
            continue
        new_value = node_edits[pin_name]
        if _looks_like_ue_struct_text(new_value):
            continue
        if _pin_statement_value_equals(text, new_value):
            continue
        graph["nodes"][index] = f'{node_name}.pin("{pin_name}", {_render_pin_literal(new_value)})'


def _pin_statement_value_equals(statement: str, expected: Any) -> bool:
    try:
        expr = ast.parse(statement, mode="eval").body
    except SyntaxError:
        return False
    if not isinstance(expr, ast.Call) or len(expr.args) < 2:
        return False
    try:
        current = ast.literal_eval(expr.args[1])
    except Exception:
        return False
    return current == expected


def _looks_like_ue_struct_text(value: Any) -> bool:
    return isinstance(value, str) and value.startswith("(") and "=" in value and value.endswith(")")
def _apply_human_default_edits(payload: dict[str, Any], human_dir: Path) -> None:
    defaults_path = human_dir / "class_defaults.py"
    if not defaults_path.is_file():
        return
    class_defaults, component_defaults, variable_defaults = _load_human_defaults(defaults_path)
    root = payload.get("root") or {}
    original_class_defaults = _class_defaults_from_root(root)
    if class_defaults != original_class_defaults:
        root["defaults"] = _merge_class_default_edits(list(root.get("defaults") or []), class_defaults)
    original_component_defaults = _component_defaults_from_root(root)
    if component_defaults != original_component_defaults:
        component_lines = list(root.get("components") or [])
        root["components"] = [
            _rewrite_component_properties(line, component_defaults, original_component_defaults)
            for line in component_lines
        ]
    original_variable_defaults = _variable_defaults_from_root(root)
    simple_variable_defaults = {
        key: value for key, value in variable_defaults.items() if _is_simple_bpy_default_value(value)
    }
    comparable_originals = {key: original_variable_defaults.get(key) for key in simple_variable_defaults}
    if simple_variable_defaults != comparable_originals:
        variable_lines = list(root.get("variables") or [])
        root["variables"] = [
            _rewrite_variable_default(line, simple_variable_defaults, original_variable_defaults)
            for line in variable_lines
        ]


def _class_defaults_from_root(root: dict[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for line in root.get("defaults") or []:
        parsed = _parse_root_call(str(line), "bp.default")
        if parsed is None:
            continue
        args, _kwargs = parsed
        if len(args) >= 2:
            result[str(args[0])] = args[1]
    return result


def _variable_defaults_from_root(root: dict[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for line in root.get("variables") or []:
        parsed = _parse_root_call(str(line), "bp.var")
        if parsed is None:
            continue
        args, kwargs = parsed
        if not args or "default" not in kwargs:
            continue
        result[str(args[0])] = _parse_bpy_default_literal(kwargs["default"])
    return result


def _parse_bpy_default_literal(value: Any) -> Any:
    if value == "True":
        return True
    if value == "False":
        return False
    if value == "None":
        return None
    if isinstance(value, str):
        try:
            if re.fullmatch(r"[-+]?\d+", value):
                return int(value)
            if re.fullmatch(r"[-+]?(?:\d+\.\d*|\d*\.\d+)", value):
                return float(value)
        except Exception:
            return value
    return value


def _is_simple_bpy_default_value(value: Any) -> bool:
    return isinstance(value, (bool, int, float)) or value is None


def _format_bpy_default_value(value: Any) -> str:
    if value is True:
        return "True"
    if value is False:
        return "False"
    if value is None:
        return "None"
    if isinstance(value, float):
        return f"{value:.6f}"
    return str(value)


def _component_defaults_from_root(root: dict[str, Any]) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for line in root.get("components") or []:
        parsed = _parse_root_call(str(line), "bp.component")
        if parsed is None:
            continue
        args, kwargs = parsed
        if args and isinstance(kwargs.get("properties"), dict):
            result[str(args[0])] = dict(kwargs["properties"])
    return result


def _merge_class_default_edits(lines: list[str], class_defaults: dict[str, Any]) -> list[str]:
    seen: set[str] = set()
    rewritten: list[str] = []
    for line in lines:
        parsed = _parse_root_call(str(line), "bp.default")
        if parsed is None:
            rewritten.append(line)
            continue
        args, _kwargs = parsed
        if len(args) < 2:
            rewritten.append(line)
            continue
        key = str(args[0])
        seen.add(key)
        new_value = class_defaults.get(key, args[1])
        if new_value == args[1]:
            rewritten.append(line)
        else:
            rewritten.append(f"bp.default({_dq(key)}, {_py_literal(new_value)})")
    for key, value in class_defaults.items():
        if key not in seen:
            rewritten.append(f"bp.default({_dq(str(key))}, {_py_literal(value)})")
    return rewritten


def _load_human_defaults(path: Path) -> tuple[dict[str, Any], dict[str, dict[str, Any]], dict[str, Any]]:
    module = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    values: dict[str, Any] = {}
    wanted = {"CLASS_DEFAULTS", "COMPONENT_DEFAULTS", "VARIABLE_DEFAULTS"}
    for node in module.body:
        if not isinstance(node, ast.Assign):
            continue
        for target in node.targets:
            if isinstance(target, ast.Name) and target.id in wanted:
                try:
                    values[target.id] = ast.literal_eval(node.value)
                except Exception as exc:
                    raise ValueError(f"Cannot parse {target.id} in {path}: {exc}") from exc
    return (
        dict(values.get("CLASS_DEFAULTS") or {}),
        dict(values.get("COMPONENT_DEFAULTS") or {}),
        dict(values.get("VARIABLE_DEFAULTS") or {}),
    )


def _rewrite_variable_default(
    line: str,
    variable_defaults: dict[str, Any],
    original_variable_defaults: dict[str, Any],
) -> str:
    parsed = _parse_root_call(line, "bp.var")
    if parsed is None:
        return line
    args, kwargs = parsed
    if not args:
        return line
    variable_name = str(args[0])
    if variable_name not in variable_defaults:
        return line
    old_value = original_variable_defaults.get(variable_name)
    new_value = variable_defaults[variable_name]
    if new_value == old_value:
        return line
    kwargs["default"] = _format_bpy_default_value(new_value)
    return _render_call("bp.var", args, kwargs)


def _rewrite_component_properties(
    line: str,
    component_defaults: dict[str, dict[str, Any]],
    original_component_defaults: dict[str, dict[str, Any]],
) -> str:
    parsed = _parse_root_call(line, "bp.component")
    if parsed is None:
        return line
    args, kwargs = parsed
    if not args:
        return line
    component_name = str(args[0])
    if component_name not in component_defaults:
        return line
    old_properties = original_component_defaults.get(component_name, {})
    new_properties = component_defaults[component_name]
    if new_properties == old_properties:
        return line
    kwargs["properties"] = new_properties
    return _render_call("bp.component", args, kwargs)


def _parse_root_call(source: str, function_name: str) -> tuple[list[Any], dict[str, Any]] | None:
    try:
        expr = ast.parse(source, mode="eval").body
    except SyntaxError:
        return None
    if not isinstance(expr, ast.Call):
        return None
    if not isinstance(expr.func, ast.Attribute) or not isinstance(expr.func.value, ast.Name):
        return None
    if f"{expr.func.value.id}.{expr.func.attr}" != function_name:
        return None
    args = [ast.literal_eval(arg) for arg in expr.args]
    kwargs = {keyword.arg: ast.literal_eval(keyword.value) for keyword in expr.keywords if keyword.arg}
    return args, kwargs


def _render_call(function_name: str, args: list[Any], kwargs: dict[str, Any]) -> str:
    rendered = [repr(arg) for arg in args]
    rendered.extend(f"{key}={_py_literal(value)}" for key, value in kwargs.items())
    return f"{function_name}({', '.join(rendered)})"


def _py_literal(value: Any) -> str:
    return repr(value)


def _emit_bpy_package_from_cst_payload(payload: dict[str, Any], out: Path) -> Path:
    if payload.get("format") != "BpyDecompiler.ExportBpyCst.v2":
        raise ValueError("Unsupported CST payload format")
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)
    _write_root_from_cst(payload, out / "__bp__.bp.py", trailing_blank=True)
    source_name = str(payload.get("source_name") or "Blueprint")
    _write_root_from_cst(payload, out / f"{source_name}.bp.py", trailing_blank=False)
    for graph in payload.get("graphs", []):
        file_name = str(graph.get("file_name") or "")
        if not file_name.endswith(".bp.py") or Path(file_name).name != file_name:
            raise ValueError(f"Unsafe graph file name in manifest: {file_name!r}")
        _write_graph_from_cst(graph, out / file_name)
        _write_graph_meta_from_cst(graph, out / (file_name[:-6] + "_meta.py"))
    return out


def _write_root_from_cst(payload: dict[str, Any], path: Path, *, trailing_blank: bool) -> None:
    root = payload.get("root") or {}
    lines = [
        "# Auto-generated by ExportBpy",
        "import importlib.util",
        "import os",
        "from ue_bp_dsl import Blueprint",
        "",
        "def _load_graph_module(stem):",
        "    file_path = os.path.join(os.path.dirname(__file__), f\"{stem}.bp.py\")",
        "    spec = importlib.util.spec_from_file_location(f\"_exportbpy_graph_{stem}\", file_path)",
        "    if spec is None or spec.loader is None:",
        "        raise ImportError(f\"Cannot load graph module: {file_path}\")",
        "    module = importlib.util.module_from_spec(spec)",
        "    spec.loader.exec_module(module)",
        "    return module",
        "",
        "_GRAPH_MODULES = [",
    ]
    for stem in root.get("graph_modules", []):
        lines.append(f"    _load_graph_module({_dq(str(stem))}),")
    lines.extend([
        "]",
        "",
        str(root.get("blueprint_call") or "bp = Blueprint(\n    path=\"\",\n    parent=\"\",\n    bp_type=\"Normal\",\n)"),
        "",
        "# ── Variables ───────────────────────────────────────────",
    ])
    lines.extend(root.get("variables", []))
    lines.extend(["", "# ── Class Defaults ──────────────────────────────────────"])
    lines.extend(root.get("defaults", []))
    lines.extend(["", "# ── Inherited Component Defaults ────────────────────────"])
    lines.extend(root.get("inherited_component_defaults", []))
    lines.extend(["", "# ── Components ──────────────────────────────────────────"])
    lines.extend(root.get("components", []))
    lines.extend(["", "# ── Interfaces ──────────────────────────────────────────"])
    lines.extend(root.get("interfaces", []))
    lines.extend(["", "# ── Event Dispatchers ───────────────────────────────────"])
    lines.extend(root.get("event_dispatchers", []))
    lines.extend(["", "bp.build()", "for _graph_module in _GRAPH_MODULES:", "    _graph_module.register(bp)"])
    text = "\n".join(lines) + ("\n\n" if trailing_blank else "\n")
    path.write_text(text, encoding="utf-8", newline="\n")



def _write_graph_meta_from_cst(graph: dict[str, Any], path: Path) -> None:
    meta_text = graph.get("meta_text")
    if isinstance(meta_text, str) and meta_text:
        path.write_text(meta_text, encoding="utf-8", newline="\n")
        return
    if graph.get("is_sidecar"):
        return
    meta = graph.get("meta")
    if not isinstance(meta, dict):
        meta = {}
    meta = {key: value for key, value in meta.items() if key != "_meta_text"}
    lines = [
        "# Auto-generated by ExportBpy",
        "",
        "# pin_alias: maps DSL_clean_name -> UE_actual_pin_name",
        f"META = {_py_literal(meta)}",
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")

def _write_graph_from_cst(graph: dict[str, Any], path: Path) -> None:
    if graph.get("is_sidecar"):
        _write_sidecar_graph_from_cst(graph, path)
        return
    file_name = path.name
    meta_name = file_name[:-6] + "_meta.py"
    lines = [
        "# Auto-generated by ExportBpy",
        "import importlib.util",
        "import os",
        "from ue_bp_dsl import *",
        "",
    ]
    if graph.get("has_sidecar_loader"):
        lines.extend(_sidecar_loader_lines())
        lines.append("")
    lines.extend([
        "def _load_meta():",
        f"    meta_path = os.path.join(os.path.dirname(__file__), {_dq(meta_name)})",
        f"    spec = importlib.util.spec_from_file_location({_dq('_exportbpy_meta_' + meta_name[:-3])}, meta_path)",
        "    if spec is None or spec.loader is None:",
        "        raise ImportError(f\"Cannot load meta module: {meta_path}\")",
        "    module = importlib.util.module_from_spec(spec)",
        "    spec.loader.exec_module(module)",
        "    return getattr(module, \"META\", {})",
        "",
        "META = _load_meta()",
        "",
        "def register(bp):",
        f"    with {graph.get('context_expr')} as g:",
        "",
        "        # Nodes",
    ])
    _append_graph_body(lines, graph, indent="        ")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def _write_sidecar_graph_from_cst(graph: dict[str, Any], path: Path) -> None:
    lines = [
        "# Auto-generated by ExportBpy",
        "import importlib.util",
        "import os",
        "from ue_bp_dsl import *",
        "",
    ]
    if graph.get("has_sidecar_loader"):
        lines.extend(_sidecar_loader_lines())
        lines.append("")
    blueprint_call = str(graph.get("blueprint_call") or "bp = Blueprint(path=\"/Engine/Transient/ExportBpyNested\", parent=\"/Script/CoreUObject.Object\", bp_type=\"Normal\")")
    lines.extend([
        "def _build_graph():",
        f"    {blueprint_call}",
        f"    with {graph.get('context_expr')} as g:",
        "        # Nodes",
    ])
    if graph.get("connections"):
        for stmt in graph.get("nodes", []):
            lines.append(f"        {stmt}")
        lines.extend(["", "        # Connections"])
        for stmt in graph.get("connections", []):
            lines.append(f"        {stmt}")
        lines.append("")
    else:
        _append_graph_body(lines, graph, indent="        ", connection_title="Connections")
    for stmt in graph.get("footer", []):
        lines.append(str(stmt))
    lines.extend(["", "GRAPH = _build_graph()", ""])
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def _append_graph_body(lines: list[str], graph: dict[str, Any], *, indent: str, connection_title: str = "Data flow") -> None:
    for stmt in graph.get("nodes", []):
        lines.append(f"{indent}{stmt}")
    data_edges = list(graph.get("data_edges", []))
    exec_edges = list(graph.get("exec_edges", []))
    if data_edges:
        lines.extend(["", f"{indent}# {connection_title}"])
        for stmt in data_edges:
            lines.append(f"{indent}{stmt}")
    if exec_edges:
        lines.extend(["", f"{indent}# Exec flow"])
        for stmt in exec_edges:
            lines.append(f"{indent}{stmt}")
    lines.append("")


def _sidecar_loader_lines() -> list[str]:
    return [
        "def _load_sidecar_graph(stem):",
        "    path = os.path.join(os.path.dirname(__file__), f\"{stem}.bp.py\")",
        "    spec = importlib.util.spec_from_file_location(f\"_exportbpy_sidecar_{stem}\", path)",
        "    if spec is None or spec.loader is None:",
        "        raise ImportError(f\"Cannot load sidecar graph module: {path}\")",
        "    module = importlib.util.module_from_spec(spec)",
        "    spec.loader.exec_module(module)",
        "    return getattr(module, \"GRAPH\")",
    ]


def _assert_safe_compile_output(source: Path, out: Path, *, label: str) -> None:
    if out == source or source in out.parents or out in source.parents:
        raise ValueError(f"Unsafe output path. {label}={source} output={out}")
    if not any(part.lower() == "tmp" for part in out.parts):
        raise ValueError(f"Compile-back output must be under tmp: {out}")


def emit_structural_bpy_package(exported_dir: str | Path, output_dir: str | Path) -> Path:
    source = Path(exported_dir).resolve()
    out = Path(output_dir).resolve()
    _assert_safe(source, out)
    bp = parse_blueprint_package(source)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)
    for graph in bp.graphs:
        _write_graph(graph, out / graph.source_path.name)
    root_sidecar = source / f"{source.name}.bp.py"
    if root_sidecar.is_file():
        (out / root_sidecar.name).write_text(root_sidecar.read_text(encoding="utf-8"), encoding="utf-8")
    _write_root(bp, out / "__bp__.bp.py")
    return out


def _assert_safe(source: Path, out: Path) -> None:
    if out == source or source in out.parents or out in source.parents:
        raise ValueError(f"Unsafe output path. source={source} output={out}")
    if not any(part.lower() == "tmp" for part in out.parts):
        raise ValueError(f"Structural compile-back output must be under tmp: {out}")


def _write_root(bp: BlueprintIR, path: Path) -> None:
    lines = [
        "# Auto-generated by ExportBpy",
        "import importlib.util",
        "import os",
        "from ue_bp_dsl import Blueprint",
        "",
        "def _load_graph_module(stem):",
        "    file_path = os.path.join(os.path.dirname(__file__), f\"{stem}.bp.py\")",
        "    spec = importlib.util.spec_from_file_location(f\"_exportbpy_graph_{stem}\", file_path)",
        "    if spec is None or spec.loader is None:",
        "        raise ImportError(f\"Cannot load graph module: {file_path}\")",
        "    module = importlib.util.module_from_spec(spec)",
        "    spec.loader.exec_module(module)",
        "    return module",
        "",
        "_GRAPH_MODULES = [",
    ]
    for stem in bp.graph_stems:
        lines.append(f"    _load_graph_module({stem!r}),")
    lines.extend([
        "]",
        "",
        "bp = Blueprint(",
        f"    path={bp.path!r},",
        f"    parent={bp.parent!r},",
        f"    bp_type={bp.bp_type!r},",
        ")",
        "",
        "# ── Variables ───────────────────────────────────────────",
    ])
    for var in bp.variables:
        kwargs = {k: v for k, v in var.items() if k not in {"name", "type"}}
        lines.append(f"bp.var({var.get('name')!r}, {var.get('type')!r}{_kwargs(kwargs)})")
    lines.extend(["", "# ── Class Defaults ──────────────────────────────────────"])
    for key, value in bp.defaults:
        lines.append(f"bp.default({key!r}, {value!r})")
    lines.extend(["", "# ── Components ──────────────────────────────────────────"])
    for comp in bp.components:
        kwargs = {k: v for k, v in comp.items() if k != "name"}
        lines.append(f"bp.component({comp.get('name')!r}{_kwargs(kwargs)})")
    lines.extend(["", "# ── Interfaces ──────────────────────────────────────────"])
    for interface in bp.interfaces:
        lines.append(f"bp.interface({interface!r})")
    lines.extend([
        "",
        "bp.build()",
        "for _graph_module in _GRAPH_MODULES:",
        "    _graph_module.register(bp)",
        "",
    ])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def _write_graph(graph: GraphIR, path: Path) -> None:
    meta_name = graph.source_path.name[:-6] + "_meta.py"
    lines = [
        "# Auto-generated by ExportBpy",
        "import importlib.util",
        "import os",
        "from ue_bp_dsl import *",
        "",
        "def _load_meta():",
        f'    meta_path = os.path.join(os.path.dirname(__file__), \"{meta_name}\")',
        f'    spec = importlib.util.spec_from_file_location(\"_exportbpy_meta_{meta_name[:-3]}\", meta_path)',
        "    if spec is None or spec.loader is None:",
        "        raise ImportError(f\"Cannot load meta module: {meta_path}\")",
        "    module = importlib.util.module_from_spec(spec)",
        "    spec.loader.exec_module(module)",
        "    return getattr(module, \"META\", {})",
        "",
        "META = _load_meta()",
        "",
        "def register(bp):",
        f"    with {_graph_context(graph)} as g:",
        "",
        "        # Nodes",
    ]
    for node in graph.nodes.values():
        lines.append(f"        {node.name} = {_node_ctor(node)}")
        for pin, value in node.defaults.items():
            lines.append(f"        {node.name}.pin({_format(pin)}, {_format(value)})")
        for key, value in node.props.items():
            if key in {"FunctionOwnerClass", "NodePurityOverride", "VariableContainer", "VariableKind", "VariableScope", "VariableType", "VariableGuid", "VariableGetIsPure", "StructType", "VisiblePins"}:
                continue
            lines.append(f"        {node.name}.set_extra_prop({key!r}, {value!r})")
    data_edges = [edge for edge in graph.edges if not edge.is_exec]
    exec_edges = [edge for edge in graph.edges if edge.is_exec]
    if data_edges:
        lines.extend(["", "        # Data flow"])
        for edge in data_edges:
            lines.append(f"        {_pin(edge.src)} >> {_pin(edge.dst)}")
    lines.extend(["", "        # Exec flow"])
    for edge in exec_edges:
        lines.append(f"        {_pin(edge.src)} >> {_pin(edge.dst)}")
    lines.append("")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def _graph_context(graph: GraphIR) -> str:
    if graph.kind == "event_graph":
        return f"bp.event_graph({_dq(graph.graph_name)})"
    args = [_dq(graph.graph_name)]
    kwargs: list[str] = []
    for key, value in graph.context_kwargs.items():
        if key in {"inputs", "outputs"}:
            kwargs.append(f"{key}={_format(value)}")
        elif key == "pure" and value:
            kwargs.append("pure=True")
        elif key not in {"pure"}:
            kwargs.append(f"{key}={_format(value)}")
    return "bp.function(" + ", ".join(args + kwargs) + ")"


def _node_ctor(node: NodeIR) -> str:
    if node.kind in NODE_METHODS_WITH_TARGET:
        if node.kind == "node":
            return f"g.node({_kwargs(node.kwargs).lstrip(', ')})"
        method = {"break_struct": "break_struct", "make_struct": "make_struct", "self_ref": "self_ref"}.get(node.kind, node.kind)
        args = [] if node.target is None else [_format(node.target)]
        kwargs = _kwargs(node.kwargs)
        return f"g.{method}(" + ", ".join(args + ([kwargs.lstrip(', ')] if kwargs else [])) + ")"
    if node.kind == "node":
        return f"g.node({_kwargs(node.kwargs).lstrip(', ')})"
    if node.kind == "self_ref":
        return "g.self_ref()"
    return f"g.{node.kind}()"


def _pin(ref: Any) -> str:
    node = ref.node
    pin = ref.pin
    attr = _pin_attr(pin)
    if attr:
        return f"{node}.{attr}"
    return f"{node}[{pin!r}]"


def _pin_attr(pin: str) -> str | None:
    if pin == "execute":
        return "exec"
    if pin == "self":
        return "self_"
    if pin.replace("_", "").isalnum() and not pin[0].isdigit():
        if pin in {"true", "false"}:
            return pin + "_"
        return pin
    return None


def _format(value: Any) -> str:
    if isinstance(value, str):
        return _dq(value)
    if isinstance(value, tuple):
        return "(" + ", ".join(_format(v) for v in value) + ("," if len(value) == 1 else "") + ")"
    if isinstance(value, list):
        return "[" + ", ".join(_format(v) for v in value) + "]"
    return repr(value)


def _dq(value: str) -> str:
    return repr(value).replace("\\'", "__APOS__").replace("'", "\"").replace("__APOS__", "\\'")


def _kwargs(kwargs: dict[str, Any]) -> str:
    return "".join(f", {key}={_format(value)}" for key, value in kwargs.items() if value is not None)


