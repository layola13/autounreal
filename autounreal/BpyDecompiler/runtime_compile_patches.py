from __future__ import annotations

import ast
import json
from pathlib import Path
from typing import Any


def apply() -> None:
    from bpy_compile.compiler import UpperPackageCompiler, ValueRef
    from bpy_compile import loader as bpy_loader

    _register_std_function_specs()

    if getattr(UpperPackageCompiler, "_bpydecompiler_patched", False):
        return

    original_compile_expr = UpperPackageCompiler._compile_expr
    original_call_name = UpperPackageCompiler._call_name
    original_visit_return = UpperPackageCompiler.visit_Return
    original_visit_expr = UpperPackageCompiler.visit_Expr
    original_connect_value = UpperPackageCompiler._connect_value
    original_compile_attribute = UpperPackageCompiler._compile_attribute
    original_parse_graph_decorator = bpy_loader._parse_graph_decorator

    def patched_compile_expr(self: Any, node: ast.AST, expected_type: str | None = None):
        if isinstance(node, ast.Name) and node.id == "self" and self.g is not None:
            self_node = self._new_node("Self", self.g.self_ref)
            return ValueRef(self_node.self, "object/self")
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub, ast.UAdd)) and isinstance(node.operand, ast.Constant):
            value = node.operand.value
            if isinstance(value, (int, float)):
                return ValueRef(-value if isinstance(node.op, ast.USub) else value, type(value).__name__)
        if isinstance(node, ast.Dict):
            if self.g is None:
                return original_compile_expr(self, node, expected_type=expected_type)
            keys = []
            for key in node.keys:
                keys.append(key.value if isinstance(key, ast.Constant) else None)
            values = list(node.values)
            struct_type = ""
            fields: list[tuple[str, ast.AST]] = []
            for key, value in zip(keys, values):
                if key == "__struct_type__" and isinstance(value, ast.Constant):
                    struct_type = str(value.value)
                elif isinstance(key, str):
                    fields.append((key, value))
            make_struct = self._new_node("MakeStruct", lambda: self.g.make_struct(struct_type or expected_type or ""))
            for key, value in fields:
                self._connect_value(self._compile_expr(value), make_struct, key)
            return ValueRef(make_struct[struct_type.rsplit("/", 1)[-1].split(".")[-1] if struct_type else "StructOut"], expected_type or ("struct/" + struct_type if struct_type else "struct"))
        if isinstance(node, ast.List):
            if self.g is None:
                return original_compile_expr(self, node, expected_type=expected_type)
            make_array = self._new_node("MakeArray", lambda: self.g.node(type="MakeArray"))
            for index, element in enumerate(node.elts):
                value_ref = self._compile_expr(element)
                self._connect_value(value_ref, make_array, f"[{index}]")
            return ValueRef(make_array["Array"], expected_type or "array")
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "bool" and len(node.args) == 1:
            return self._compile_expr(node.args[0], expected_type="bool")
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "abs" and len(node.args) == 1:
            return original_compile_expr(self, node, expected_type=expected_type)
        if isinstance(node, ast.Call) and (_is_std_call(node, "cast") or _is_std_call(node, "cast_as")):
            synthetic = ast.copy_location(ast.Call(func=ast.Name(id="cast", ctx=ast.Load()), args=node.args, keywords=node.keywords), node)
            return original_compile_expr(self, synthetic, expected_type=expected_type)
        if isinstance(node, ast.Call) and _is_std_call(node, "select"):
            synthetic = ast.copy_location(ast.Call(func=ast.Name(id="select", ctx=ast.Load()), args=node.args, keywords=node.keywords), node)
            return original_compile_expr(self, synthetic, expected_type=expected_type)
        if isinstance(node, ast.Call) and _is_std_call(node, "event_payload"):
            return ValueRef(_EventPayloadRef(), "event_payload")
        if isinstance(node, ast.Call) and (_is_std_call(node, "InputAction") or _is_std_call(node, "InputKey")):
            return ValueRef(_EventPayloadRef(), "event_payload")
        if isinstance(node, ast.Attribute) and isinstance(node.value, ast.Call) and (_is_std_call(node.value, "event_payload") or _is_std_call(node.value, "InputAction") or _is_std_call(node.value, "InputKey")):
            return ValueRef(True, "bool")
        if isinstance(node, ast.IfExp):
            synthetic = ast.copy_location(ast.Call(func=ast.Name(id="select", ctx=ast.Load()), args=[node.test, node.orelse, node.body], keywords=[]), node)
            return original_compile_expr(self, synthetic, expected_type=expected_type)
        if isinstance(node, ast.Attribute) and isinstance(node.value, ast.Call) and _is_std_call(node.value, "event_payload"):
            return ValueRef(None, None)
        return original_compile_expr(self, node, expected_type=expected_type)

    def patched_connect_value(self: Any, value_ref: Any, node: Any, pin_name: str) -> None:
        return original_connect_value(self, value_ref, node, _resolve_pin_alias(self, node, pin_name))

    def patched_compile_attribute(self: Any, node: ast.Attribute, *, expected_type: str | None = None):
        value_ref = original_compile_attribute(self, node, expected_type=expected_type)
        try:
            from ue_bp_dsl.core import PinRef
        except Exception:
            return value_ref
        pin_ref = getattr(value_ref, "value", None)
        if isinstance(pin_ref, PinRef):
            current_pin = getattr(pin_ref, "_pin_name", node.attr)
            resolved = _resolve_pin_alias(self, pin_ref, current_pin)
            if resolved != current_pin:
                return ValueRef(PinRef(pin_ref._node, pin_ref._graph, resolved, pin_ref._direction), value_ref.type_str)
        return value_ref

    def patched_call_name(self: Any, node: ast.Call) -> str:
        if isinstance(node.func, ast.Attribute):
            chain: list[str] = []
            current: ast.AST = node.func
            while isinstance(current, ast.Attribute):
                chain.append(current.attr)
                current = current.value
            if isinstance(current, ast.Name):
                chain.append(current.id)
                chain.reverse()
                if chain and chain[0] == "std" and len(chain) >= 2:
                    if chain[-1] == "property":
                        return "PropertyAccess"
                    return chain[-1]
        return original_call_name(self, node)

    def patched_visit_return(self: Any, node: ast.Return) -> None:
        if node.value is not None and len(self.graph_spec.outputs) == 0:
            synthetic_none = ast.copy_location(ast.Return(value=None), node)
            return original_visit_return(self, synthetic_none)
        if (
            isinstance(node.value, ast.Call)
            and isinstance(node.value.func, ast.Attribute)
            and node.value.func.attr == "output"
            and isinstance(node.value.func.value, ast.Name)
            and node.value.func.value.id == "std"
        ):
            synthetic = ast.copy_location(ast.Return(value=ast.Call(func=ast.Name(id="result", ctx=ast.Load()), args=[], keywords=node.value.keywords)), node)
            return original_visit_return(self, synthetic)
        return original_visit_return(self, node)

    def patched_visit_expr(self: Any, node: ast.Expr) -> None:
        if isinstance(node.value, ast.Call) and _is_std_call(node.value, "replace"):
            if node.value.args and isinstance(node.value.args[0], ast.Attribute):
                target = node.value.args[0]
                if isinstance(target.value, ast.Name) and target.value.id == "self":
                    for keyword in node.value.keywords:
                        if keyword.arg is None:
                            continue
                        value_ref = self._compile_expr(keyword.value)
                        self._assign_member_field(target.attr, keyword.arg, value_ref, node)
                    return
        return original_visit_expr(self, node)

    def patched_parse_graph_decorator(func_def: ast.FunctionDef, file_path: str):
        info = original_parse_graph_decorator(func_def, file_path)
        if info is not None:
            return info
        for decorator in func_def.decorator_list:
            if not isinstance(decorator, ast.Call):
                continue
            if _is_std_call(decorator, "function"):
                graph_info = {
                    "kind": "function",
                    "name": _default_graph_name(file_path),
                    "pure": False,
                    "outputs": [],
                }
                for keyword in decorator.keywords:
                    if keyword.arg in {"name", "pure", "outputs"}:
                        graph_info[keyword.arg] = ast.literal_eval(keyword.value)
                graph_info["outputs"] = _resolve_graph_outputs(file_path, graph_info.get("outputs", []))
                return graph_info
            if _is_std_call(decorator, "event"):
                graph_info = {
                    "kind": "event_graph",
                    "name": "EventGraph",
                    "entry": None,
                }
                for keyword in decorator.keywords:
                    if keyword.arg in {"name", "entry"}:
                        graph_info[keyword.arg] = ast.literal_eval(keyword.value)
                return graph_info
        return None

    UpperPackageCompiler._compile_expr = patched_compile_expr
    UpperPackageCompiler._connect_value = patched_connect_value
    UpperPackageCompiler._compile_attribute = patched_compile_attribute
    UpperPackageCompiler._call_name = patched_call_name
    UpperPackageCompiler.visit_Return = patched_visit_return
    UpperPackageCompiler.visit_Expr = patched_visit_expr
    bpy_loader._parse_graph_decorator = patched_parse_graph_decorator
    UpperPackageCompiler._bpydecompiler_patched = True


def load_blueprint_meta(upper_file: str | Path):
    from ue_bp_dsl import Blueprint

    base = Path(upper_file).resolve().parent
    path = base / "__blueprint_meta__.json"
    payload = json.loads(path.read_text(encoding="utf-8"))
    bp = Blueprint(
        path=str(payload.get("path") or ""),
        parent=str(payload.get("parent") or ""),
        bp_type=str(payload.get("bp_type") or "Normal"),
    )
    for var in payload.get("variables", []):
        if not isinstance(var, dict):
            continue
        name = var.get("name")
        type_str = var.get("type")
        if name is None or type_str is None:
            continue
        kwargs = {key: value for key, value in var.items() if key not in {"name", "type"}}
        bp.var(str(name), str(type_str), **kwargs)
    for item in payload.get("defaults", []):
        if isinstance(item, list) and len(item) == 2:
            bp.default(str(item[0]), item[1])
    for component in payload.get("components", []):
        if not isinstance(component, dict):
            continue
        name = component.get("name")
        if name is None:
            continue
        kwargs = {key: value for key, value in component.items() if key != "name"}
        bp.component(str(name), **kwargs)
    for interface in payload.get("interfaces", []):
        bp.interface(str(interface))
    return bp


def load_dynamic_function_specs(upper_file: str | Path) -> None:
    try:
        from bpy_compile.maps.function_map import FUNCTION_MAP, FunctionSpec
    except Exception:
        return
    base = Path(upper_file).resolve().parent
    path = base / "__dynamic_function_specs__.json"
    if not path.is_file():
        return
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return
    if not isinstance(payload, dict):
        return
    for name, spec in payload.items():
        if not isinstance(name, str) or not isinstance(spec, dict):
            continue
        ue_ref = spec.get("ue_ref")
        if not isinstance(ue_ref, str) or not ue_ref:
            continue
        node_class = str(spec.get("node_class") or "K2Node_CallFunction")
        FUNCTION_MAP[name] = FunctionSpec(
            ue_ref=ue_ref,
            params={str(k): str(v) for k, v in dict(spec.get("params") or {}).items()},
            returns=tuple(str(value) for value in spec.get("returns", ["ReturnValue"])),
            return_types=tuple(str(value) for value in spec.get("return_types", ["unknown"])),
            pure=True if node_class == "MacroInstance" else bool(spec.get("pure", True)),
            node_class=node_class,
            extra_props=dict(spec.get("extra_props") or {}),
            param_order=tuple(str(value) for value in spec.get("param_order", [])),
        )


def _register_std_function_specs() -> None:
    try:
        from bpy_compile.maps.function_map import FUNCTION_MAP, FunctionSpec
    except Exception:
        return
    manifest = Path(__file__).resolve().parent / "std" / "manifest.json"
    if not manifest.is_file():
        return
    try:
        payload = json.loads(manifest.read_text(encoding="utf-8"))
    except Exception:
        return
    for item in payload.get("functions", []):
        name = item.get("name")
        ue_ref = item.get("ue_ref")
        if name in {"cast_as", "cast", "select", "output", "function", "event", "replace", "property", "InputAction", "InputKey", "event_payload"}:
            continue
        if not isinstance(name, str) or not isinstance(ue_ref, str) or not name or not ue_ref:
            continue
        if name in FUNCTION_MAP:
            continue
        params = {str(param): str(param) for param in item.get("params", []) if isinstance(param, str)}
        returns = tuple(str(value) for value in item.get("returns", ["ReturnValue"]))
        FUNCTION_MAP[name] = FunctionSpec(
            ue_ref=ue_ref,
            params=params,
            returns=returns,
            return_types=tuple("unknown" for _ in returns),
            pure=bool(item.get("pure", True)),
            param_order=tuple(params.keys()),
        )





def _is_std_call(node: ast.Call, name: str) -> bool:
    return (
        isinstance(node.func, ast.Attribute)
        and node.func.attr == name
        and isinstance(node.func.value, ast.Name)
        and node.func.value.id == "std"
    )


def _default_graph_name(file_path: str) -> str:
    base = Path(file_path).stem
    if base.startswith("fn_"):
        return base[3:]
    if base.startswith("evt_"):
        return base[4:]
    return base


class _EventPayloadRef:
    pass


def _resolve_pin_alias(compiler: Any, node: Any, pin_name: str) -> str:
    text = str(pin_name)
    readable_name = getattr(getattr(node, "_node", None), "readable_name", "")
    graph_spec = getattr(compiler, "graph_spec", None)
    meta = getattr(graph_spec, "meta", None) if graph_spec is not None else None
    if isinstance(meta, dict):
        alias_map = meta.get("pin_alias", {})
        if isinstance(alias_map, dict):
            direct = alias_map.get(f"{readable_name}.{text}")
            if isinstance(direct, str):
                return direct
        for map_name in ("input_pin_types", "output_pin_types", "pin_id"):
            pin_map = meta.get(map_name, {})
            if not isinstance(pin_map, dict):
                continue
            prefix = f"{readable_name}."
            for key in pin_map:
                if not isinstance(key, str) or not key.startswith(prefix):
                    continue
                actual = key.split(".", 1)[1]
                if _short_pin_name(actual) == text:
                    return actual
    return text


def _short_pin_name(name: str) -> str:
    import re
    text = str(name)
    if text.startswith("ReturnValue_"):
        text = text[len("ReturnValue_"):]
    text = re.sub(r"_[0-9]+_[A-F0-9]{32}$", "", text)
    return ''.join(ch if ch.isalnum() or ch == '_' else '_' for ch in text).strip('_') or text


def _resolve_graph_outputs(file_path: str, outputs: Any) -> Any:
    try:
        base = Path(file_path).resolve().parent
        meta = json.loads((base / "__roundtrip_bpy__.json").read_text(encoding="utf-8"))
        file_name = Path(file_path).name
        for graph in meta.get("graphs", []):
            if graph.get("file_name") != file_name.replace(".py", ".bp.py"):
                continue
            context = str(graph.get("context_expr") or "")
            marker = "outputs="
            if marker not in context:
                return outputs
            import ast as _ast
            parsed = _ast.parse("_x = " + context, mode="exec")
            call = parsed.body[0].value
            if isinstance(call, _ast.Call):
                for keyword in call.keywords:
                    if keyword.arg == "outputs":
                        return _ast.literal_eval(keyword.value)
    except Exception:
        return outputs
    return outputs
