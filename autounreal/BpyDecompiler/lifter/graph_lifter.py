from __future__ import annotations

import ast
from dataclasses import dataclass, field
from typing import Iterable
import re

from ..ir import GraphIR, NodeIR
from .function_names import safe_identifier


@dataclass(slots=True)
class LiftedGraph:
    graph: GraphIR
    lines: list[str]
    unsupported: list[str] = field(default_factory=list)


class GraphLifter:
    def __init__(self, graph: GraphIR, reverse_functions: dict[str, str] | None = None, local_functions: set[str] | None = None):
        self.graph = graph
        self.reverse_functions = reverse_functions or {}
        self.local_functions = local_functions or set()
        self.data_in: dict[tuple[str, str], tuple[str, str]] = {}
        self.exec_out: dict[str, list[tuple[str, str]]] = {}
        self.exec_in: dict[str, list[tuple[str, str]]] = {}
        self.unsupported: list[str] = []
        self.visited: set[str] = set()
        self.data_used: set[str] = set()
        self.input_names = {name for name, _type in self.graph.inputs}
        self._index_edges()

    def lift(self) -> LiftedGraph:
        lines: list[str] = []
        for start in self._start_nodes():
            if start in self.visited:
                continue
            block = self._walk(start)
            if not block:
                continue
            if lines:
                lines.append("")
            lines.extend(block)
        if not lines:
            lines = [self._empty_output_return_statement() if self.graph.outputs else self._graph_noop_statement()]
        if self.graph.outputs and not self._lines_guarantee_return(lines):
            lines.append(self._default_return_statement())
        return LiftedGraph(self.graph, lines, self.unsupported)


    def _index_edges(self) -> None:
        for edge in self.graph.edges:
            if edge.is_exec:
                self.exec_out.setdefault(edge.src.node, []).append((edge.src.pin, edge.dst.node))
                self.exec_in.setdefault(edge.dst.node, []).append((edge.dst.pin, edge.src.node))
            else:
                self.data_in[(edge.dst.node, edge.dst.pin)] = (edge.src.node, edge.src.pin)

    def _start_nodes(self) -> list[str]:
        starts: list[str] = []
        entries = [self.graph.entry, *[name for name, node in self.graph.nodes.items() if node.kind in {"entry", "event", "custom_event"}]]
        for entry in entries:
            if entry and self.exec_out.get(entry):
                starts.append(self.exec_out[entry][0][1])
        if self.graph.kind == "event_graph":
            starts.extend(
                name
                for name, node in self.graph.nodes.items()
                if self._is_executable(node) and name not in self.exec_in and self.exec_out.get(name) and node.kind != "sequence" and not (node.kind == "node" and str(node.kwargs.get("type") or "") == "Sequence")
            )
        if not starts:
            result_nodes = [name for name, node in self.graph.nodes.items() if node.kind == "result"]
            if len(result_nodes) == 1:
                starts.append(result_nodes[0])
        result: list[str] = []
        for name in starts:
            if name and name not in result:
                result.append(name)
        return result

    def _walk(self, node_name: str | None, stop: set[str] | None = None) -> list[str]:
        stop = stop or set()
        lines: list[str] = []
        current = node_name
        while current and current not in stop:
            if current in self.visited:
                return lines
            node = self.graph.nodes.get(current)
            if node is None:
                return lines
            self.visited.add(current)
            if node.kind == "branch":
                lines.extend(self._emit_branch(node))
                return lines
            if self._has_multiple_exec_outputs(node.name):
                lines.extend(self._emit_multi_exec(node))
                return lines
            lines.extend(self._emit_node(node))
            current = self._single_next(current)
        return lines


    def _emit_multi_exec(self, node: NodeIR) -> list[str]:
        outs = self.exec_out.get(node.name, [])
        if node.kind in {"switch_enum", "switch_int"}:
            return self._emit_switch(node, outs)
        if node.kind == "node" and str(node.kwargs.get("type") or "") == "Sequence":
            lines: list[str] = []
            for _pin, dst in sorted(outs, key=lambda item: self._exec_pin_sort_key(item[0])):
                lines.extend(self._walk(dst))
            return lines or [self._opaque_node_statement(node)]
        positive = self._next_for_pin(node.name, {"then", "is valid", "is_valid", "valid"})
        negative = self._next_for_pin(node.name, {"else", "cast_failed", "cast failed", "is not valid", "not valid", "invalid"})
        if positive or negative:
            condition = self._exec_condition(node)
            true_lines = self._walk(positive) if positive else []
            false_lines = self._walk(negative) if negative else []
            return self._conditional_lines(condition, true_lines, false_lines)
        return self._emit_named_exec_paths(node, outs)



    def _emit_switch(self, node: NodeIR, outs: list[tuple[str, str]]) -> list[str]:
        selection = self._expr_for_pin(node.name, "selection") or self._expr_for_pin(node.name, "Selection") or "None"
        result = [f"match {selection}:"]
        emitted = False
        base_visited = set(self.visited)
        accumulated_visited = set(self.visited)
        grouped: list[tuple[list[str], list[str]]] = []
        for pin, dst in outs:
            case_value = self._switch_case_value(pin)
            self.visited = set(base_visited)
            branch_lines = self._walk(dst)
            accumulated_visited.update(self.visited)
            if not branch_lines:
                continue
            branch_key = tuple(branch_lines)
            for case_values, existing_lines in grouped:
                if tuple(existing_lines) == branch_key:
                    case_values.append(case_value)
                    break
            else:
                grouped.append(([case_value], branch_lines))
        self.visited = accumulated_visited
        for case_values, branch_lines in grouped:
            result.append(f"    case {' | '.join(case_values)}:")
            result.extend("        " + line for line in branch_lines)
            emitted = True
        if not emitted:
            return []
        return result

    def _switch_case_value(self, pin: str) -> str:
        text = str(pin)
        if text.startswith("case(") and text.endswith(")"):
            return text[5:-1]
        if text.startswith("NewEnumerator"):
            return repr(text)
        try:
            return str(int(text))
        except ValueError:
            return repr(text)

    def _emit_named_exec_paths(self, node: NodeIR, outs: list[tuple[str, str]]) -> list[str]:
        if self._is_sequence_like_outputs(outs):
            lines: list[str] = []
            for _pin, dst in sorted(outs, key=lambda item: self._exec_pin_sort_key(item[0])):
                lines.extend(self._walk(dst))
            return lines or [self._opaque_node_statement(node)]
        base = self._event_source_expr(node)
        lines: list[str] = []
        for pin, dst in outs:
            condition = f"{base}.{safe_identifier(pin)}"
            branch_lines = self._walk(dst)
            if not branch_lines:
                continue
            lines.append(f"if {condition}:")
            lines.extend(self._indent(branch_lines))
        return lines or [self._opaque_node_statement(node)]

    def _is_sequence_like_outputs(self, outs: list[tuple[str, str]]) -> bool:
        return all(pin.lower().startswith("then") for pin, _dst in outs)

    def _exec_pin_sort_key(self, pin: str) -> tuple[int, str]:
        text = pin.strip().lower().replace("then_", "").replace("then", "0")
        try:
            return (int(text), pin)
        except ValueError:
            return (9999, pin)

    def _event_source_expr(self, node: NodeIR) -> str:
        node_type = str(node.kwargs.get("type") or node.target or node.name)
        if node_type == "EnhancedInputAction":
            action = node.props.get("InputAction") or node.kwargs.get("InputAction")
            return f"std.InputAction({action!r})"
        if node_type == "InputKey":
            key = node.props.get("InputKey") or node.kwargs.get("InputKey")
            return f"std.InputKey({key!r})"
        return self._opaque_node_statement(node)

    def _exec_condition(self, node: NodeIR) -> str:
        if node.kind == "get_var":
            return f"bool(self.{safe_identifier(str(node.target or node.name))})"
        if node.kind == "cast":
            obj = self._expr_for_pin(node.name, "Object") or "None"
            return f"std.cast_as({obj}, {str(node.target or '')!r}) != None"
        if node.kind == "node" and str(node.kwargs.get("type") or "") == "IsValid":
            return self._expr_for_pin(node.name, "InputObject") or self._first_data_expr(node) or "False"
        if node.kind == "call":
            return self._call_expr(node)
        return self._expr_for_pin(node.name, "condition") or self._first_data_expr(node) or self._opaque_node_statement(node)

    def _has_multiple_exec_outputs(self, node_name: str) -> bool:
        return len(self.exec_out.get(node_name, [])) > 1

    def _emit_branch(self, node: NodeIR) -> list[str]:
        condition = self._expr_for_pin(node.name, "condition") or "False"
        true_next = self._next_for_pin(node.name, {"true", "True"})
        false_next = self._next_for_pin(node.name, {"false", "False"})
        join = self._find_simple_join(true_next, false_next)
        true_lines = self._walk(true_next, {join} if join else set()) if true_next else []
        false_lines = self._walk(false_next, {join} if join else set()) if false_next else []
        result = self._conditional_lines(condition, true_lines, false_lines)
        if join:
            result.extend(self._walk(join))
        return result

    def _emit_node(self, node: NodeIR) -> list[str]:
        if node.kind in {"event", "custom_event"} and self._has_data_consumers(node):
            return [f"{self._temp_name(node)} = std.event_payload({str(node.target or node.name)!r})"]
        if node.kind in {"entry", "event", "custom_event", "get_var", "self", "self_ref"}:
            return []
        if node.kind in {"sequence", "switch_enum", "switch_int"}:
            return []
        if node.kind == "node" and str(node.kwargs.get("type") or "") in {"Knot", "EdGraphNode_Comment"}:
            return []
        if node.kind == "cast":
            return []
        if node.kind == "result":
            return [self._emit_return(node)]
        if node.kind == "set_var":
            target = safe_identifier(str(node.target or node.name))
            value = self._expr_for_pin(node.name, str(node.target or "value")) or self._first_data_expr(node) or self._default_for_node(node) or "None"
            return [f"self.{target} = {value}"]
        if node.kind in {"call", "message"}:
            call = self._call_expr(node)
            anchor = f"  # bpy: {node.name}"
            if self._has_data_consumers(node):
                return [f"{self._temp_name(node)} = {call}{anchor}"]
            return [f"{call}{anchor}"]
        return [self._opaque_node_statement(node)]

    def _opaque_node_statement(self, node: NodeIR) -> str:
        expression = self._node_expr(node)
        if expression != safe_identifier(node.name):
            return expression
        label = self._node_label(node)
        inputs = {pin: self._expr_for_pin(node.name, pin) for dst_node, pin in self.data_in if dst_node == node.name}
        defaults = dict(node.defaults)
        props = {key: value for key, value in node.props.items() if key not in {"FunctionOwnerClass", "NodePurityOverride"}}
        return (
            f"Node({node.name!r}, kind={node.kind!r}, target={label!r}, "
            f"inputs={inputs!r}, defaults={defaults!r}, props={props!r})"
        )

    def _graph_noop_statement(self) -> str:
        return "return None"

    def _default_return_statement(self) -> str:
        return "raise ValueError('unhandled input combination')"

    def _empty_output_return_statement(self) -> str:
        output_names = [name for name, _type in self.graph.outputs]
        if not output_names:
            return "return"
        if len(output_names) == 1:
            return "return None"
        pairs = ", ".join(f"{self._readable_pin_name(name)}=None" for name in output_names)
        return f"return std.output({pairs})"

    def _lines_guarantee_return(self, lines: list[str]) -> bool:
        source = "def _graph():\n" + "\n".join("    " + line if line else "" for line in lines) + "\n"
        try:
            tree = ast.parse(source)
        except SyntaxError:
            return False
        fn = tree.body[0] if tree.body else None
        return isinstance(fn, ast.FunctionDef) and self._block_guarantees_return(fn.body)

    def _block_guarantees_return(self, body: list[ast.stmt]) -> bool:
        for stmt in body:
            if isinstance(stmt, ast.Return):
                return True
            if isinstance(stmt, ast.Raise):
                return True
            if isinstance(stmt, ast.If):
                if stmt.orelse and self._block_guarantees_return(stmt.body) and self._block_guarantees_return(stmt.orelse):
                    return True
            if isinstance(stmt, ast.Match):
                if stmt.cases and any(self._is_wildcard_case(case) for case in stmt.cases):
                    if all(self._block_guarantees_return(case.body) for case in stmt.cases):
                        return True
            if isinstance(stmt, ast.Try):
                handlers_return = stmt.handlers and all(self._block_guarantees_return(handler.body) for handler in stmt.handlers)
                if handlers_return and self._block_guarantees_return(stmt.body) and (not stmt.orelse or self._block_guarantees_return(stmt.orelse)):
                    return True
        return False

    def _is_wildcard_case(self, case: ast.match_case) -> bool:
        return isinstance(case.pattern, ast.MatchAs) and case.pattern.name is None and case.pattern.pattern is None

    def _conditional_lines(self, condition: str, true_lines: list[str], false_lines: list[str]) -> list[str]:
        if true_lines:
            result = [f"if {condition}:"]
            result.extend(self._indent(true_lines))
            if false_lines:
                result.append("else:")
                result.extend(self._indent(false_lines))
            return result
        if false_lines:
            result = [f"if not ({condition}):"]
            result.extend(self._indent(false_lines))
            return result
        return []

    def _empty_branch_statement(self, node: NodeIR, branch_name: str) -> str:
        return ""

    def _emit_return(self, node: NodeIR) -> str:
        output_names = [name for name, _type in self.graph.outputs] or self._output_pins(node)
        if not output_names:
            return f"return  # bpy: {node.name}"
        values = [self._expr_for_pin(node.name, name) or self._pin_default(node, name) or "None" for name in output_names]
        anchor = f"  # bpy: {node.name}" if len(output_names) != 1 else f"  # bpy: {node.name}.{output_names[0]}"
        if len(values) == 1:
            return f"return {values[0]}{anchor}"
        pairs = ", ".join(f"{self._readable_pin_name(name)}={value}" for name, value in zip(output_names, values))
        return f"return std.output({pairs}){anchor}"

    def _call_expr(self, node: NodeIR, wanted_pin: str | None = None) -> str:
        special = self._special_call_expr(node)
        if special is not None:
            return special
        target_ref = str(node.target or "")
        fn_name = self.reverse_functions.get(target_ref) or safe_identifier(str(node.target or node.name).replace("::", "_"))
        if fn_name not in self.local_functions and (target_ref in self.reverse_functions or "::" in target_ref or target_ref):
            fn_name = f"std.{fn_name}"
        args = []
        for pin in self._call_input_pins(node):
            value = self._expr_for_pin(node.name, pin) or self._pin_default(node, pin)
            if value is not None:
                args.append(f"{safe_identifier(pin)}={value}")
        return f"{fn_name}({', '.join(args)})"


    def _special_call_expr(self, node: NodeIR) -> str | None:
        target = str(node.target or "")
        if target == "KismetMathLibrary::BooleanAND":
            left = self._expr_for_pin(node.name, "A") or "False"
            right = self._expr_for_pin(node.name, "B") or "False"
            return f"({left} and {right})"
        if target == "KismetMathLibrary::BooleanOR":
            left = self._expr_for_pin(node.name, "A") or "False"
            right = self._expr_for_pin(node.name, "B") or "False"
            return f"({left} or {right})"
        if target == "KismetMathLibrary::Not_PreBool":
            value = self._expr_for_pin(node.name, "A") or self._expr_for_pin(node.name, "Value") or "False"
            return f"(not {value})"
        if target in {"KismetMathLibrary::Abs", "KismetMathLibrary::Abs_Double", "KismetMathLibrary::Abs_Int"}:
            value = self._expr_for_pin(node.name, "A") or self._expr_for_pin(node.name, "Value") or "0"
            return f"abs({value})"
        compare_ops = {
            "Greater": ">",
            "GreaterEqual": ">=",
            "Less": "<",
            "LessEqual": "<=",
            "EqualEqual": "==",
            "NotEqual": "!=",
        }
        for prefix, op in compare_ops.items():
            if target.startswith(f"KismetMathLibrary::{prefix}_"):
                left = self._expr_for_pin(node.name, "A") or self._pin_default(node, "A") or "0"
                right = self._expr_for_pin(node.name, "B") or self._pin_default(node, "B") or "0"
                return f"({left} {op} {right})"
        return None
    def _expr_for_pin(self, node_name: str, pin: str) -> str | None:
        src = self.data_in.get((node_name, pin))
        if not src:
            node = self.graph.nodes.get(node_name)
            return self._pin_default(node, pin) if node else None
        src_node_name, src_pin = src
        src_node = self.graph.nodes.get(src_node_name)
        if src_node is None:
            return None
        self._mark_data_used(src_node_name)
        if src_node.kind == "entry":
            return safe_identifier(src_pin) if src_pin in self.input_names else None
        if src_node.kind in {"event", "custom_event"}:
            return f"std.event_payload({str(src_node.target or src_node.name)!r}).{safe_identifier(src_pin)}"
        if src_node.kind == "get_var":
            raw_name = str(src_node.target or src_node.name)
            var_name = safe_identifier(raw_name)
            return var_name if raw_name in self.input_names else f"self.{var_name}"
        if src_node.kind in {"self", "self_ref"}:
            return "self"
        if src_node.kind == "cast":
            obj = self._expr_for_pin(src_node.name, "Object") or "None"
            return f"std.cast_as({obj}, {str(src_node.target or '')!r})"
        if src_node.kind in {"call", "message"}:
            expr = self._call_expr(src_node, src_pin)
            if self._has_multiple_data_consumers(src_node) and self._is_executable(src_node):
                temp = self._temp_name(src_node)
                if src_pin not in {"result", "ReturnValue", "value"}:
                    return f"{temp}.{self._readable_pin_name(src_pin)}"
                return temp
            if src_pin not in {"result", "ReturnValue", "value"}:
                return f"{expr}.{self._readable_pin_name(src_pin)}"
            return expr
        if src_node.kind == "node" and src_node.kwargs.get("type") == "MakeArray":
            return self._make_array_expr(src_node)
        if src_node.kind == "sequence":
            return None
        if src_node.kind == "node" and src_node.kwargs.get("type") in {"EdGraphNode_Comment", "Knot", "Sequence"}:
            return None
        if src_node.kind == "node" and (src_node.target in {"EqualEqual_ByteByte", "NotEqual_ByteByte"} or src_node.kwargs.get("type") in {"EnumEquality", "EnumInequality"}):
            left = self._expr_for_pin(src_node.name, "A") or "None"
            right = self._expr_for_pin(src_node.name, "B") or self._pin_default(src_node, "B") or "None"
            op = "!=" if src_node.target == "NotEqual_ByteByte" or src_node.kwargs.get("type") == "EnumInequality" else "=="
            return f"({left} {op} {right})"
        if src_node.kind == "node" and src_node.kwargs.get("type") not in {"EdGraphNode_Comment", "Knot"}:
            return self._node_expr(src_node)
        if src_node.kind == "select":
            condition = self._expr_for_pin(src_node.name, "index") or self._expr_for_pin(src_node.name, "Index") or "False"
            when_false = self._pin_default(src_node, "Option 0") or self._pin_default(src_node, "False") or "False"
            when_true = self._pin_default(src_node, "Option 1") or self._pin_default(src_node, "True") or "True"
            return f"({when_true} if {condition} else {when_false})"
        if src_node.kind == "make_struct":
            return self._make_struct_expr(src_node)
        if src_node.kind == "break_struct":
            base_pin = str(src_node.target or "StructRef").split(".")[-1]
            base = self._first_data_expr(src_node) or self._expr_for_pin(src_node.name, base_pin) or self._expr_for_pin(src_node.name, "StructRef")
            if base:
                return f"{base}.{self._readable_pin_name(src_pin)}"
            return f"{safe_identifier(src_node.name)}.{self._readable_pin_name(src_pin)}"
        if src_node.kind == "set_var":
            return f"self.{safe_identifier(str(src_node.target or src_node.name))}"
        if src_pin in {"result", "ReturnValue", "value", "Output_Get"}:
            return safe_identifier(src_node.name)
        return f"{safe_identifier(src_node.name)}.{self._readable_pin_name(src_pin)}"

    def _readable_pin_name(self, name: str) -> str:
        text = str(name)
        if text == "ReturnValue":
            return text
        if text.startswith("ReturnValue_"):
            text = text[len("ReturnValue_"):]
        text = re.sub(r"_[0-9]+_[A-F0-9]{32}$", "", text)
        return safe_identifier(text)


    def _mark_data_used(self, node_name: str) -> None:
        if node_name in self.data_used:
            return
        self.data_used.add(node_name)
        node = self.graph.nodes.get(node_name)
        if node is None:
            return
        for dst_node, dst_pin in list(self.data_in):
            if dst_node == node_name:
                src = self.data_in.get((dst_node, dst_pin))
                if src:
                    self._mark_data_used(src[0])

    def _first_data_expr(self, node: NodeIR) -> str | None:
        for dst_node, dst_pin in self.data_in:
            if dst_node == node.name:
                return self._expr_for_pin(dst_node, dst_pin)
        return None

    def _call_input_pins(self, node: NodeIR) -> list[str]:
        pins = [dst_pin for (dst_node, dst_pin) in self.data_in if dst_node == node.name and dst_pin != "execute"]
        for pin in node.defaults:
            if pin not in pins and pin != "self":
                pins.append(pin)
        return [pin for pin in pins if pin not in {"then", "result", "ReturnValue"}]

    def _node_expr(self, node: NodeIR) -> str:
        node_type = str(node.kwargs.get("type") or node.target or node.name)
        if node_type == "Knot":
            return ""
        if node_type == "EnhancedInputAction":
            action = node.props.get("InputAction") or node.kwargs.get("InputAction")
            return f"std.InputAction({action!r})"
        if node_type == "InputKey":
            key = node.props.get("InputKey") or node.kwargs.get("InputKey")
            return f"std.InputKey({key!r})"
        if node_type.startswith("AnimGraphNode_"):
            return self._anim_node_expr(node, node_type)
        if node_type in {"SetFieldsInStruct", "SetFieldsInStruct_0"} or node_type == "SetFieldsInStruct":
            return self._struct_update_expr(node)
        fn_name = self._generic_node_function_name(node, node_type)
        args = []
        for key, value in sorted(node.props.items()):
            if key in {"InputAction", "target_type", "TargetType", "Enum"}:
                args.append(f"{safe_identifier(key)}={value!r}")
        for dst_node, pin in self.data_in:
            if dst_node == node.name:
                value = self._expr_for_pin(node.name, pin) or "None"
                args.append(f"{safe_identifier(pin)}={value}")
        return f"{fn_name}({', '.join(args)})"


    def _generic_node_function_name(self, node: NodeIR, node_type: str) -> str:
        if node_type == "MacroInstance":
            name = node.kwargs.get("name") or node.props.get("Name")
            if isinstance(name, str) and name:
                return safe_identifier(name)
        if node_type == "PropertyAccess":
            return "std.property"
        return f"std.{safe_identifier(node_type)}"

    def _anim_node_expr(self, node: NodeIR, node_type: str) -> str:
        args = []
        for dst_node, pin in self.data_in:
            if dst_node == node.name:
                args.append(f"{safe_identifier(pin)}={self._expr_for_pin(node.name, pin) or 'None'}")
        for pin, value in node.defaults.items():
            args.append(f"{safe_identifier(pin)}={value!r}")
        node_prop = node.props.get("Node")
        if node_prop and node_prop != "()":
            args.append(f"settings={node_prop!r}")
        return f"std.make_anim_node({node_type!r}{', ' if args else ''}{', '.join(args)})"

    def _struct_update_expr(self, node: NodeIR) -> str:
        fields = []
        for dst_node, pin in self.data_in:
            if dst_node == node.name and pin != "StructRef":
                fields.append(f"{pin!r}: {self._expr_for_pin(node.name, pin) or 'None'}")
        for pin, value in node.defaults.items():
            fields.append(f"{pin!r}: {value!r}")
        target = self._expr_for_pin(node.name, "StructRef") or "None"
        kwargs = []
        for dst_node, pin in self.data_in:
            if dst_node == node.name and pin != "StructRef":
                kwargs.append(f"{self._readable_pin_name(pin)}={self._expr_for_pin(node.name, pin) or 'None'}")
        for pin, value in node.defaults.items():
            kwargs.append(f"{self._readable_pin_name(pin)}={value!r}")
        return "std.replace(" + target + (", " if kwargs else "") + ", ".join(kwargs) + ")"

    def _make_struct_expr(self, node: NodeIR) -> str:
        fields: list[str] = []
        if node.target:
            fields.append(f"'__struct_type__': {str(node.target)!r}")
        for dst_node, dst_pin in sorted(self.data_in):
            if dst_node != node.name:
                continue
            value = self._expr_for_pin(dst_node, dst_pin) or "None"
            fields.append(f"{self._readable_pin_name(dst_pin)!r}: {value}")
        return "{" + ", ".join(fields) + "}"

    def _make_array_expr(self, node: NodeIR) -> str:
        indexed: list[tuple[int, str]] = []
        for dst_node, dst_pin in self.data_in:
            if dst_node != node.name:
                continue
            if dst_pin.startswith("[") and dst_pin.endswith("]"):
                try:
                    index = int(dst_pin[1:-1])
                except ValueError:
                    continue
                indexed.append((index, self._expr_for_pin(dst_node, dst_pin) or "None"))
        indexed.sort(key=lambda item: item[0])
        return "[" + ", ".join(value for _index, value in indexed) + "]"

    def _output_pins(self, node: NodeIR) -> list[str]:
        return [dst_pin for (dst_node, dst_pin) in self.data_in if dst_node == node.name and dst_pin != "execute"]

    def _pin_default(self, node: NodeIR | None, pin: str) -> str | None:
        if node is None:
            return None
        if pin in node.defaults:
            return repr(node.defaults[pin])
        default = node.defaults.get(pin[:1].upper() + pin[1:])
        return repr(default) if default is not None else None

    def _default_for_node(self, node: NodeIR) -> str | None:
        return repr(next(iter(node.defaults.values()))) if node.defaults else None

    def _single_next(self, node_name: str) -> str | None:
        outs = self.exec_out.get(node_name, [])
        return outs[0][1] if len(outs) == 1 else None

    def _next_for_pin(self, node_name: str, pins: set[str]) -> str | None:
        normalized = {pin.lower().rstrip("_") for pin in pins}
        for pin, dst in self.exec_out.get(node_name, []):
            if pin.lower().rstrip("_") in normalized:
                return dst
        return None

    def _find_simple_join(self, left: str | None, right: str | None) -> str | None:
        if not left or not right:
            return None
        left_seen = self._linear_reachable(left, 64)
        current = right
        for _ in range(64):
            if current in left_seen:
                return current
            node = self.graph.nodes.get(current)
            if node is None or node.kind == "branch":
                return None
            current = self._single_next(current)
            if current is None:
                return None
        return None

    def _linear_reachable(self, start: str, limit: int) -> set[str]:
        seen: set[str] = set()
        current: str | None = start
        for _ in range(limit):
            if current is None or current in seen:
                break
            seen.add(current)
            node = self.graph.nodes.get(current)
            if node is None or node.kind == "branch":
                break
            current = self._single_next(current)
        return seen

    def _has_data_consumers(self, node: NodeIR) -> bool:
        return any(src_node == node.name for src_node, _src_pin in self.data_in.values())

    def _has_multiple_data_consumers(self, node: NodeIR) -> bool:
        return sum(1 for src_node, _src_pin in self.data_in.values() if src_node == node.name) > 1

    def _temp_name(self, node: NodeIR) -> str:
        return safe_identifier(node.name)

    def _is_executable(self, node: NodeIR) -> bool:
        if node.kind in {"entry", "event", "custom_event", "self", "self_ref", "break_struct", "make_struct", "select"}:
            return False
        if node.kind in {"get_var", "cast", "call"}:
            return node.name in self.exec_in or node.name in self.exec_out
        if node.kind == "node" and str(node.kwargs.get("type") or "") in {"EdGraphNode_Comment", "Knot"}:
            return node.name in self.exec_in or node.name in self.exec_out
        return True

    def _node_label(self, node: NodeIR) -> str:
        return str(node.target or node.kind)

    def _indent(self, lines: Iterable[str]) -> list[str]:
        return ["    " + line for line in lines]


def lift_graph(graph: GraphIR, reverse_functions: dict[str, str] | None = None, local_functions: set[str] | None = None) -> LiftedGraph:
    return GraphLifter(graph, reverse_functions, local_functions).lift()









