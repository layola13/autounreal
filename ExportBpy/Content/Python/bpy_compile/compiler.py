# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-

from __future__ import annotations

import ast
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

from ue_bp_dsl.core import Blueprint, GraphContext, NodeProxy, PinRef

from .exec_chain import append_exec_node, connect_execs, merge_exec_tails
from .errors import CompileError
from .loader import GraphSpec, LoadedUpperPackage
from .maps.function_map import FUNCTION_MAP
from .patterns import (
    extract_object_attribute_target,
    extract_self_member_field_target,
    match_cast_none_compare,
)
from .symbol_table import SymbolTable
from .type_inference import (
    BOOL_OP_MAP,
    infer_binop_function,
    infer_compare_function,
    normalize_type_name,
    parse_annotation,
    type_from_constant,
)


@dataclass
class ValueRef:
    value: Any
    type_str: Optional[str] = None


@dataclass
class CastBinding:
    node: NodeProxy
    success_ref: PinRef
    class_path: str


class UpperPackageCompiler(ast.NodeVisitor):
    def __init__(self, package: LoadedUpperPackage):
        self.package = package
        self.blueprint = package.blueprint
        self.g: Optional[GraphContext] = None
        self.graph_spec: Optional[GraphSpec] = None
        self.symbols = SymbolTable()
        self.current_tails: List[PinRef] = []
        self.member_types: Dict[str, str] = {}
        self.input_types: Dict[str, str] = {}
        self.local_types: Dict[str, str] = {}
        self.node_name_counts: Dict[str, int] = {}
        self.uid_prefix = ""
        self.uid_counter = 0
        self.return_node_count = 0

    def compile(self) -> Blueprint:
        self.member_types = {
            var.name: var.type_str
            for var in getattr(self.blueprint, "_variables", [])
        }
        self.blueprint._graphs.clear()
        for graph_spec in self.package.graphs:
            self._compile_graph(graph_spec)
        return self.blueprint

    def _compile_graph(self, graph_spec: GraphSpec) -> None:
        self.graph_spec = graph_spec
        self.symbols = SymbolTable()
        self.current_tails = []
        self.input_types = {name: type_str for name, type_str in graph_spec.inputs}
        self.local_types = {}
        self.node_name_counts = {}
        self.uid_prefix = graph_spec.graph_name.replace(" ", "_")
        self.uid_counter = 0
        self.return_node_count = 0

        if graph_spec.kind == "function":
            with self.blueprint.function(
                graph_spec.graph_name,
                inputs=graph_spec.inputs,
                outputs=graph_spec.outputs,
                pure=graph_spec.pure,
            ) as g:
                self._begin_graph(g)
                entry = self._new_node("Entry", g.entry)
                if not graph_spec.pure:
                    self.current_tails = [entry.then]
                self._compile_statements(graph_spec.func_def.body)
        elif graph_spec.kind == "event_graph":
            with self.blueprint.event_graph(graph_spec.graph_name) as g:
                self._begin_graph(g)
                if graph_spec.entry:
                    event_node = self._new_node(graph_spec.entry, lambda: g.event(graph_spec.entry))
                    self.current_tails = [event_node.then]
                self._compile_statements(graph_spec.func_def.body)
        else:
            raise CompileError(f"不支持的 graph kind: {graph_spec.kind}", file_path=graph_spec.file_path)

    def _begin_graph(self, graph: GraphContext) -> None:
        self.g = graph
        graph._graph.uid_factory = self._next_uid
        graph._graph.metadata = dict(self.graph_spec.meta or {})

    def _next_uid(self, *, node_class: str, kwargs: Dict[str, Any]) -> str:
        value = f"{self.uid_prefix}_{self.uid_counter:04d}"
        self.uid_counter += 1
        return value

    def _compile_statements(self, statements: List[ast.stmt]) -> None:
        for stmt in statements:
            self.visit(stmt)

    def visit_Assign(self, node: ast.Assign) -> None:
        if len(node.targets) != 1:
            raise self._error(node, "仅支持单目标赋值")

        target = node.targets[0]
        if isinstance(target, ast.Tuple):
            self._handle_tuple_assign(node, target)
            return

        if isinstance(target, ast.Name) and isinstance(node.value, ast.Call):
            if isinstance(node.value.func, ast.Name) and node.value.func.id == "cast":
                value_ref, cast_binding = self._compile_cast(node.value)
                self.symbols.set(target.id, value_ref.value, value_ref.type_str)
                self.symbols.remember_cast(target.id, cast_binding)
                if value_ref.type_str:
                    self.local_types[target.id] = value_ref.type_str
                return

        value_type_hint = self._annotation_hint_for_target(target)
        value_ref = self._compile_expr(node.value, expected_type=value_type_hint)

        if isinstance(target, ast.Name):
            inferred = value_type_hint or value_ref.type_str or self.local_types.get(target.id)
            self.symbols.set(target.id, value_ref.value, inferred)
            if inferred:
                self.local_types[target.id] = inferred
            return

        if isinstance(target, ast.Attribute):
            if isinstance(target.value, ast.Name) and target.value.id == "self":
                self._assign_member(target.attr, value_ref)
                return
            self_member_field = extract_self_member_field_target(target)
            if self_member_field is not None:
                self._assign_member_field(self_member_field[0], self_member_field[1], value_ref, node)
                return
            object_target = extract_object_attribute_target(target)
            if object_target is not None:
                self._assign_external_member(object_target[0], object_target[1], value_ref, node)
                return

        raise self._error(node, "不支持的赋值目标")

    def visit_AnnAssign(self, node: ast.AnnAssign) -> None:
        if node.value is None:
            return
        target = node.target
        annotation = parse_annotation(node.annotation)
        if isinstance(target, ast.Name) and annotation:
            self.local_types[target.id] = annotation
        if isinstance(target, ast.Name) and isinstance(node.value, ast.Call):
            if isinstance(node.value.func, ast.Name) and node.value.func.id == "cast":
                value_ref, cast_binding = self._compile_cast(node.value)
                self.symbols.set(target.id, value_ref.value, annotation or value_ref.type_str)
                self.symbols.remember_cast(target.id, cast_binding)
                return
        value_ref = self._compile_expr(node.value, expected_type=annotation)
        if isinstance(target, ast.Name):
            self.symbols.set(target.id, value_ref.value, annotation or value_ref.type_str)
            return
        if isinstance(target, ast.Attribute):
            if isinstance(target.value, ast.Name) and target.value.id == "self":
                self._assign_member(target.attr, value_ref)
                return
            self_member_field = extract_self_member_field_target(target)
            if self_member_field is not None:
                self._assign_member_field(self_member_field[0], self_member_field[1], value_ref, node)
                return
            object_target = extract_object_attribute_target(target)
            if object_target is not None:
                self._assign_external_member(object_target[0], object_target[1], value_ref, node)
                return
        raise self._error(node, "不支持的注解赋值目标")

    def visit_Expr(self, node: ast.Expr) -> None:
        self._compile_expr(node.value)

    def visit_Pass(self, node: ast.Pass) -> None:
        return

    def visit_Return(self, node: ast.Return) -> None:
        if self.g is None or self.graph_spec is None:
            return

        base_name = "Return" if self.return_node_count == 0 else f"Return__{self.return_node_count - 1}"
        ret_node = self._new_node(base_name, self.g.result)
        self.return_node_count += 1
        if self.current_tails:
            connect_execs(self.current_tails, ret_node.exec)
        self.current_tails = []

        if node.value is None:
            return

        if isinstance(node.value, ast.Call) and isinstance(node.value.func, ast.Name) and node.value.func.id == "result":
            for keyword in node.value.keywords:
                if keyword.arg is None:
                    continue
                self._connect_value(self._compile_expr(keyword.value), ret_node, keyword.arg)
            return

        if len(self.graph_spec.outputs) == 1:
            self._connect_value(self._compile_expr(node.value), ret_node, self.graph_spec.outputs[0][0])
            return

        raise self._error(node, "多输出函数必须使用 return result(...)")

    def visit_If(self, node: ast.If) -> None:
        if self._handle_cast_none_if(node):
            return

        if self.g is None:
            return

        condition_ref = self._compile_expr(node.test, expected_type="bool")
        branch = self._new_node("Branch", self.g.branch)
        self._connect_value(condition_ref, branch, "Condition")
        connect_execs(self.current_tails, branch.exec)

        self.current_tails = [branch.true_]
        self._compile_statements(node.body)
        true_tails = list(self.current_tails)

        self.current_tails = [branch.false_]
        self._compile_statements(node.orelse)
        false_tails = list(self.current_tails)

        self.current_tails = merge_exec_tails(true_tails, false_tails)

    def _handle_cast_none_if(self, node: ast.If) -> bool:
        cast_name = match_cast_none_compare(node.test)
        if cast_name is None:
            return False

        binding = self.symbols.get_cast(cast_name)
        if binding is None:
            return False

        success_tails = list(self.current_tails)

        self.current_tails = [binding.node.cast_failed]
        self._compile_statements(node.body)
        failure_tails = list(self.current_tails)

        self.current_tails = success_tails
        self._compile_statements(node.orelse)
        success_branch_tails = list(self.current_tails)

        self.current_tails = merge_exec_tails(failure_tails, success_branch_tails)
        return True

    def _handle_tuple_assign(self, node: ast.Assign, target: ast.Tuple) -> None:
        if not isinstance(node.value, ast.Call):
            raise self._error(node, "tuple 解包只支持函数调用")
        call_result = self._compile_call(node.value)
        if len(target.elts) != len(call_result["returns"]):
            raise self._error(node, "tuple 解包数量与函数返回值数量不匹配")
        for index, element in enumerate(target.elts):
            if not isinstance(element, ast.Name):
                raise self._error(node, "tuple 解包目标必须是局部变量")
            value_ref = ValueRef(call_result["returns"][index], call_result["return_types"][index])
            self.symbols.set(element.id, value_ref.value, value_ref.type_str)
            if value_ref.type_str:
                self.local_types[element.id] = value_ref.type_str

    def _assign_member(self, member_name: str, value_ref: ValueRef) -> None:
        if self.g is None:
            return
        set_node = self._new_node(f"Set_{member_name}", lambda: self.g.set_var(member_name))
        self._configure_member_variable_node(set_node, member_name)
        self._connect_value(value_ref, set_node, member_name)
        self.current_tails = append_exec_node(self.current_tails, set_node)

    def _assign_member_field(self, member_name: str, field_name: str, value_ref: ValueRef, node: ast.AST) -> None:
        if self.g is None:
            return
        struct_type = self.member_types.get(member_name)
        if not struct_type or not struct_type.startswith("struct/"):
            raise self._error(node, f"成员 '{member_name}' 不是 struct，不能设置字段 '{field_name}'")

        get_node = self._new_node(member_name, lambda: self.g.get_var(member_name))
        self._configure_member_variable_node(get_node, member_name)
        set_fields = self._new_node("SetFieldsInStruct", lambda: self.g.set_fields(self._resolve_struct_path(struct_type)))
        set_node = self._new_node(f"Set_{member_name}", lambda: self.g.set_var(member_name))
        self._configure_member_variable_node(set_node, member_name)
        get_node.value >> set_fields["StructRef"]
        self._connect_value(value_ref, set_fields, field_name)
        set_fields["StructOut"] >> set_node[member_name]
        self.current_tails = append_exec_node(self.current_tails, set_fields)
        self.current_tails = append_exec_node(self.current_tails, set_node)

    def _assign_external_member(self, owner_expr: ast.expr, member_name: str, value_ref: ValueRef, node: ast.AST) -> None:
        if self.g is None:
            return

        owner_ref = self._compile_expr(owner_expr)
        owner_type = normalize_type_name(owner_ref.type_str)
        if not self._is_object_type(owner_type) or not isinstance(owner_ref.value, PinRef):
            raise self._error(node, f"无法设置外部对象属性: {member_name}")

        set_node = self._new_node(f"Set_{member_name}", lambda: self.g.set_var(member_name))
        self._configure_external_variable_node(
            set_node,
            owner_pin=owner_ref.value,
            owner_class=self._object_owner_class(owner_type),
            type_str=value_ref.type_str,
        )
        self._connect_value(value_ref, set_node, member_name)
        self.current_tails = append_exec_node(self.current_tails, set_node)

    def _compile_expr(self, node: ast.AST, expected_type: Optional[str] = None) -> ValueRef:
        if isinstance(node, ast.Constant):
            return ValueRef(node.value, type_from_constant(node.value))

        if isinstance(node, ast.Name):
            if self.symbols.has(node.id):
                symbol = self.symbols.get(node.id)
                return ValueRef(symbol.value, symbol.type_str)
            if node.id in self.input_types:
                return self._get_var(node.id, self.input_types[node.id], storage="parameter")
            raise self._error(node, f"未定义变量: {node.id}")

        if isinstance(node, ast.Attribute):
            return self._compile_attribute(node, expected_type=expected_type)

        if isinstance(node, ast.Call):
            if isinstance(node.func, ast.Name) and node.func.id == "cast":
                value_ref, _ = self._compile_cast(node)
                return value_ref
            if isinstance(node.func, ast.Name) and node.func.id == "select":
                return self._compile_select(node, expected_type=expected_type)
            if isinstance(node.func, ast.Name) and node.func.id == "result":
                raise self._error(node, "result() 只能出现在 return 语句里")
            call_info = self._compile_call(node)
            return ValueRef(call_info["returns"][0], call_info["return_types"][0])

        if isinstance(node, ast.BinOp):
            left = self._compile_expr(node.left)
            right = self._compile_expr(node.right)
            token = self._binop_token(node.op)
            expr_text = ast.get_source_segment(self.graph_spec.source if self.graph_spec else "", node) or token
            ue_ref, result_type = infer_binop_function(
                token,
                left_type=left.type_str,
                right_type=right.type_str,
                expected_type=expected_type,
                file_path=self.graph_spec.file_path if self.graph_spec else "",
                line=getattr(node, "lineno", 0),
                expr_text=expr_text,
                graph_name=self.graph_spec.graph_name if self.graph_spec else "",
            )
            op_node = self._new_node(self._call_basename(ue_ref), lambda: self.g.call(ue_ref))
            self._apply_call_owner(op_node, ue_ref)
            self._connect_value(left, op_node, "A")
            self._connect_value(right, op_node, "B")
            return ValueRef(op_node.result, result_type)

        if isinstance(node, ast.Compare):
            if len(node.ops) != 1 or len(node.comparators) != 1:
                raise self._error(node, "仅支持单个比较运算")
            left = self._compile_expr(node.left)
            right = self._compile_expr(node.comparators[0])
            token = self._compare_token(node.ops[0])
            expr_text = ast.get_source_segment(self.graph_spec.source if self.graph_spec else "", node) or token
            ue_ref = infer_compare_function(
                token,
                left_type=left.type_str,
                right_type=right.type_str,
                file_path=self.graph_spec.file_path if self.graph_spec else "",
                line=getattr(node, "lineno", 0),
                expr_text=expr_text,
                graph_name=self.graph_spec.graph_name if self.graph_spec else "",
            )
            op_node = self._new_node(self._call_basename(ue_ref), lambda: self.g.call(ue_ref))
            self._apply_call_owner(op_node, ue_ref)
            self._connect_value(left, op_node, "A")
            self._connect_value(right, op_node, "B")
            return ValueRef(op_node.result, "bool")

        if isinstance(node, ast.BoolOp):
            op_token = "and" if isinstance(node.op, ast.And) else "or"
            current = self._compile_expr(node.values[0], expected_type="bool")
            for next_node in node.values[1:]:
                rhs = self._compile_expr(next_node, expected_type="bool")
                ue_ref = BOOL_OP_MAP[op_token]
                op_node = self._new_node(self._call_basename(ue_ref), lambda: self.g.call(ue_ref))
                self._apply_call_owner(op_node, ue_ref)
                self._connect_value(current, op_node, "A")
                self._connect_value(rhs, op_node, "B")
                current = ValueRef(op_node.result, "bool")
            return current

        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Not):
            operand = self._compile_expr(node.operand, expected_type="bool")
            ue_ref = BOOL_OP_MAP["not"]
            op_node = self._new_node(self._call_basename(ue_ref), lambda: self.g.call(ue_ref))
            self._apply_call_owner(op_node, ue_ref)
            self._connect_value(operand, op_node, "A")
            return ValueRef(op_node.result, "bool")

        raise self._error(node, f"不支持的表达式: {type(node).__name__}")

    def _compile_attribute(self, node: ast.Attribute, *, expected_type: Optional[str] = None) -> ValueRef:
        if isinstance(node.value, ast.Name) and node.value.id == "self":
            return self._get_var(node.attr, self.member_types.get(node.attr), storage="member")

        base = self._compile_expr(node.value)
        base_type = normalize_type_name(base.type_str)
        if base_type and base_type.startswith("struct/"):
            break_node = self._get_or_create_break(base, self._resolve_struct_path(base_type))
            return ValueRef(break_node[node.attr], None)

        if self._is_object_type(base_type) and isinstance(base.value, PinRef):
            get_node = self._new_node(node.attr, lambda: self.g.get_var(node.attr))
            self._configure_external_variable_node(
                get_node,
                owner_pin=base.value,
                owner_class=self._object_owner_class(base_type),
                type_str=expected_type,
            )
            resolved_type = normalize_type_name(get_node._node.extra_props.get("VariableType") or expected_type)
            return ValueRef(get_node.value, resolved_type)

        if isinstance(base.value, PinRef):
            return ValueRef(PinRef(base.value._node, base.value._graph, node.attr, "output"), None)

        raise self._error(node, f"无法访问属性: {node.attr}")

    def _compile_cast(self, node: ast.Call) -> Tuple[ValueRef, CastBinding]:
        if self.g is None:
            raise self._error(node, "cast() 只能在 graph 中使用")
        if len(node.args) != 2:
            raise self._error(node, "cast() 需要两个参数: object, class_path")
        obj_ref = self._compile_expr(node.args[0])
        class_path = self._literal_string(node.args[1])
        cast_node = self._new_node("DynamicCast", lambda: self.g.cast(class_path))
        self._connect_value(obj_ref, cast_node, "Object")
        self.current_tails = append_exec_node(self.current_tails, cast_node)
        success_pin = self._cast_success_pin(class_path)
        value_ref = ValueRef(cast_node[success_pin], f"object/{class_path.lstrip('/')}")
        return value_ref, CastBinding(node=cast_node, success_ref=cast_node[success_pin], class_path=class_path)

    def _compile_select(self, node: ast.Call, *, expected_type: Optional[str]) -> ValueRef:
        if self.g is None:
            raise self._error(node, "select() 只能在 graph 中使用")
        if len(node.args) != 3:
            raise self._error(node, "select() 需要三个参数: condition, when_false, when_true")
        condition = self._compile_expr(node.args[0], expected_type="bool")
        when_false = self._compile_expr(node.args[1], expected_type=expected_type)
        when_true = self._compile_expr(node.args[2], expected_type=expected_type)
        select_node = self._new_node("Select", self.g.select)
        select_node.set_extra_prop("IndexType", "bool")
        self._connect_value(condition, select_node, "Index")
        self._connect_value(when_false, select_node, "Option 0")
        self._connect_value(when_true, select_node, "Option 1")
        return ValueRef(select_node.result, expected_type or when_false.type_str or when_true.type_str)

    def _compile_call(self, node: ast.Call) -> Dict[str, Any]:
        if self.g is None:
            raise self._error(node, "函数调用只能在 graph 中使用")
        func_name = self._call_name(node)
        spec = FUNCTION_MAP.get(func_name)
        if spec is None:
            raise self._error(node, f"未知函数: {func_name}")

        call_node = self._new_node(self._call_basename(spec.ue_ref), lambda: self.g.call(spec.ue_ref, node_class=spec.node_class))
        self._apply_call_owner(call_node, spec.ue_ref)
        for key, value in spec.extra_props.items():
            call_node.set_extra_prop(key, value)
        for key, value in spec.default_values.items():
            call_node.pin(key, value)

        for index, arg in enumerate(node.args):
            if index >= len(spec.param_order):
                raise self._error(node, f"函数 '{func_name}' 位置参数过多")
            param_name = spec.param_order[index]
            pin_name = spec.params.get(param_name, param_name)
            self._connect_value(self._compile_expr(arg), call_node, pin_name)

        for keyword in node.keywords:
            if keyword.arg is None:
                raise self._error(node, f"函数 '{func_name}' 不支持 **kwargs")
            pin_name = spec.params.get(keyword.arg, keyword.arg)
            self._connect_value(self._compile_expr(keyword.value), call_node, pin_name)

        if spec.pure is False:
            if self.graph_spec and self.graph_spec.pure:
                raise self._error(node, f"pure 函数中不能调用 impure 节点: {func_name}")
            self.current_tails = append_exec_node(self.current_tails, call_node)

        return {
            "node": call_node,
            "returns": [call_node[pin_name] for pin_name in spec.returns],
            "return_types": list(spec.return_types),
        }

    def _get_var(self, name: str, type_str: Optional[str], *, storage: str = "member") -> ValueRef:
        if self.g is None:
            raise CompileError("graph 尚未初始化")
        get_node = self._new_node(name, lambda: self.g.get_var(name))
        if storage == "parameter":
            self._configure_parameter_variable_node(get_node, name, type_str)
        else:
            self._configure_member_variable_node(get_node, name)
        return ValueRef(get_node.value, type_str)

    def _get_or_create_break(self, base: ValueRef, struct_path: str) -> NodeProxy:
        if not isinstance(base.value, PinRef):
            raise CompileError("BreakStruct 的输入必须是 PinRef")
        key = (base.value._node.uid, base.value._pin_name, struct_path)
        cached = self.symbols.get_break(key)
        if cached is not None:
            return cached
        break_node = self._new_node("BreakStruct", lambda: self.g.break_struct(struct_path))
        base.value >> break_node["StructRef"]
        self.symbols.remember_break(key, break_node)
        return break_node

    def _connect_value(self, value_ref: ValueRef, node: NodeProxy, pin_name: str) -> None:
        if isinstance(value_ref.value, PinRef):
            value_ref.value >> node[pin_name]
        else:
            node.pin(pin_name, value_ref.value)

    def _new_node(self, base_name: str, factory: Any) -> NodeProxy:
        node = factory()
        node.set_readable_name(self._allocate_name(base_name))
        self._apply_meta_to_node(node)
        return node

    def _allocate_name(self, base_name: str) -> str:
        clean = "".join(ch if ch.isalnum() or ch == "_" else "_" for ch in base_name) or "Node"
        index = self.node_name_counts.get(clean, 0)
        self.node_name_counts[clean] = index + 1
        return clean if index == 0 else f"{clean}_{index}"

    def _annotation_hint_for_target(self, target: ast.AST) -> Optional[str]:
        if isinstance(target, ast.Name):
            return self.local_types.get(target.id)
        return None

    def _configure_parameter_variable_node(self, node: NodeProxy, name: str, type_str: Optional[str]) -> None:
        scope_name = self.graph_spec.graph_name if self.graph_spec else ""
        node.set_extra_prop("VariableContainer", "single")
        node.set_extra_prop("VariableKind", "Parameter")
        node.set_extra_prop("VariableScope", "Local")
        node.set_extra_prop("VariableScopeName", scope_name)
        if type_str:
            node.set_extra_prop("VariableType", type_str)

    def _configure_member_variable_node(self, node: NodeProxy, name: str) -> None:
        node.set_extra_prop("VariableContainer", "single")
        node.set_extra_prop("VariableKind", "Member")
        node.set_extra_prop("VariableScope", "Self")
        type_str = self.member_types.get(name)
        if type_str:
            node.set_extra_prop("VariableType", type_str)

    def _configure_external_variable_node(
        self,
        node: NodeProxy,
        *,
        owner_pin: PinRef,
        owner_class: str,
        type_str: Optional[str],
    ) -> None:
        node_props = node._node.extra_props
        if "VariableContainer" not in node_props:
            node.set_extra_prop("VariableContainer", "single")
        if "VariableKind" not in node_props:
            node.set_extra_prop("VariableKind", "Member")
        if "VariableScope" not in node_props:
            node.set_extra_prop("VariableScope", "External")
        if owner_class and "VariableOwnerClass" not in node_props:
            node.set_extra_prop("VariableOwnerClass", owner_class)
        if type_str and "VariableType" not in node_props:
            node.set_extra_prop("VariableType", type_str)
        owner_pin >> node.self_

    def _apply_meta_to_node(self, node: NodeProxy) -> None:
        if self.graph_spec is None or not self.graph_spec.meta:
            return

        readable_name = node._node.readable_name
        node_guid_map = self.graph_spec.meta.get("node_guid", {})
        node_pos_map = self.graph_spec.meta.get("node_pos", {})
        node_props_map = self.graph_spec.meta.get("node_props", {})
        pin_alias_map = self.graph_spec.meta.get("pin_alias", {})
        pin_id_map = self.graph_spec.meta.get("pin_id", {})

        if readable_name in node_guid_map:
            node.set_node_guid(str(node_guid_map[readable_name]))

        if readable_name in node_pos_map:
            pos_value = node_pos_map[readable_name]
            if isinstance(pos_value, (list, tuple)) and len(pos_value) >= 2:
                node._node.pos_x = pos_value[0]
                node._node.pos_y = pos_value[1]

        if readable_name in node_props_map and isinstance(node_props_map[readable_name], dict):
            for key, value in node_props_map[readable_name].items():
                if key in {"VariableContainer", "VariableKind", "VariableScope", "VariableScopeName", "VariableType"}:
                    continue
                node.set_extra_prop(str(key), value)

        prefix = readable_name + "."
        for alias_key, full_pin_name in pin_alias_map.items():
            if isinstance(alias_key, str) and alias_key.startswith(prefix):
                node.set_pin_alias(alias_key.split(".", 1)[1], str(full_pin_name))

        for pin_key, pin_id in pin_id_map.items():
            if isinstance(pin_key, str) and pin_key.startswith(prefix):
                node.set_pin_id(pin_key.split(".", 1)[1], str(pin_id))

    def _apply_call_owner(self, node: NodeProxy, ue_ref: str) -> None:
        owner_class = self._owner_class_for_ue_ref(ue_ref)
        if owner_class:
            node.set_extra_prop("FunctionOwnerClass", owner_class)

    def _is_object_type(self, type_name: Optional[str]) -> bool:
        text = normalize_type_name(type_name) or ""
        return text.startswith("object/") or text.startswith("/Script/") or text.startswith("/Game/")

    def _object_owner_class(self, type_name: Optional[str]) -> str:
        text = normalize_type_name(type_name) or ""
        if text.startswith("object/"):
            suffix = text.split("object/", 1)[1]
            if suffix.startswith("Script/") or suffix.startswith("Game/"):
                return f"/{suffix}"
            return suffix
        return text

    def _owner_class_for_ue_ref(self, ue_ref: str) -> str:
        if "::" not in ue_ref:
            return ""

        owner_token = ue_ref.split("::", 1)[0]
        if not owner_token or owner_token.startswith("/"):
            return owner_token

        owner_map = {
            "Actor": "/Script/Engine.Actor",
            "CharacterMovementComponent": "/Script/Engine.CharacterMovementComponent",
            "CharacterMoverComponent": "/Script/Mover.CharacterMoverComponent",
            "KismetMathLibrary": "/Script/Engine.KismetMathLibrary",
            "KismetStringLibrary": "/Script/Engine.KismetStringLibrary",
            "KismetSystemLibrary": "/Script/Engine.KismetSystemLibrary",
            "MoverComponent": "/Script/Mover.MoverComponent",
            "Pawn": "/Script/Engine.Pawn",
            "PrimitiveComponent": "/Script/Engine.PrimitiveComponent",
            "BlueprintMapLibrary": "/Script/Engine.BlueprintMapLibrary",
        }
        return owner_map.get(owner_token, "")

    def _resolve_struct_path(self, type_name: str) -> str:
        return type_name.split("struct/", 1)[1] if type_name.startswith("struct/") else type_name

    def _call_name(self, node: ast.Call) -> str:
        if isinstance(node.func, ast.Name):
            return node.func.id
        raise self._error(node, "仅支持直接函数调用")

    def _literal_string(self, node: ast.AST) -> str:
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            return node.value
        raise self._error(node, "这里需要字符串字面量")

    def _cast_success_pin(self, class_path: str) -> str:
        if class_path.startswith("/Script/"):
            class_name = class_path.rsplit(".", 1)[-1]
            return f"As{class_name}"
        token = class_path.replace("\\", "/").split("/")[-1]
        if "." in token:
            token = token.split(".")[0]
        if token.endswith("_C"):
            token = token[:-2]
        return f"As{token.replace('_', ' ')}"

    def _call_basename(self, ue_ref: str) -> str:
        tail = ue_ref.split("::")[-1]
        return "".join(ch if ch.isalnum() or ch == "_" else "_" for ch in tail)

    def _binop_token(self, op: ast.operator) -> str:
        mapping = {
            ast.Add: "+",
            ast.Sub: "-",
            ast.Mult: "*",
            ast.Div: "/",
        }
        for op_type, token in mapping.items():
            if isinstance(op, op_type):
                return token
        raise CompileError(f"不支持的二元运算: {type(op).__name__}")

    def _compare_token(self, op: ast.cmpop) -> str:
        mapping = {
            ast.Eq: "==",
            ast.NotEq: "!=",
            ast.Gt: ">",
            ast.GtE: ">=",
            ast.Lt: "<",
            ast.LtE: "<=",
        }
        for op_type, token in mapping.items():
            if isinstance(op, op_type):
                return token
        raise CompileError(f"不支持的比较运算: {type(op).__name__}")

    def _error(self, node: ast.AST, message: str) -> CompileError:
        return CompileError(
            message=message,
            file_path=self.graph_spec.file_path if self.graph_spec else "",
            line=getattr(node, "lineno", 0),
            column=getattr(node, "col_offset", 0),
            graph_name=self.graph_spec.graph_name if self.graph_spec else "",
        )


def compile_loaded_package(package: LoadedUpperPackage) -> Blueprint:
    compiler = UpperPackageCompiler(package)
    return compiler.compile()
