from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from .checker import check_upper_dir
from .emitter import emit_human_package, emit_upper_package
from .ir import BlueprintIR, Diagnostic
from .lifter import lift_graph
from .lifter.function_names import load_reverse_function_map
from .parser import parse_blueprint_package


@dataclass(slots=True)
class DecompileResult:
    ok: bool
    input_dir: Path
    output_dir: Path
    human_dir: Path | None = None
    blueprint: BlueprintIR | None = None
    diagnostics: list[Diagnostic] = field(default_factory=list)


def decompile_blueprint(exported_dir: str | Path, output_dir: str | Path | None = None, human_output_dir: str | Path | None = None) -> DecompileResult:
    input_path = Path(exported_dir).resolve()
    if output_dir is None:
        project_root = _guess_project_root(input_path)
        output_path = project_root / "UpperBlueprints" / input_path.name
    else:
        output_path = Path(output_dir).resolve()

    bp = parse_blueprint_package(input_path)
    reverse_functions = load_reverse_function_map(Path(__file__).resolve())
    local_functions = {graph.graph_name for graph in bp.graphs if graph.kind == "function"}
    lifted = [lift_graph(graph, reverse_functions, local_functions) for graph in bp.graphs]
    emit_upper_package(bp, lifted, output_path)
    human_path = Path(human_output_dir).resolve() if human_output_dir is not None else None
    if human_path is not None:
        emit_human_package(bp, lifted, human_path)
    diagnostics = list(bp.diagnostics)
    for issue in check_upper_dir(output_path):
        rel_path = issue.path.name
        diagnostics.append(Diagnostic("error", f"python checker failed in {rel_path}:{issue.line}: {issue.message}"))
    for item in lifted:
        diagnostics.extend(item.graph.diagnostics)
        diagnostics.extend(Diagnostic("warning", text, graph=item.graph.graph_name) for text in item.unsupported)
    return DecompileResult(ok=not any(d.level == "error" for d in diagnostics), input_dir=input_path, output_dir=output_path, human_dir=human_path, blueprint=bp, diagnostics=diagnostics)


def _guess_project_root(input_path: Path) -> Path:
    parts = list(input_path.parts)
    for index in range(len(parts) - 2):
        if parts[index].lower() == "exportedblueprints" and parts[index + 1].lower() == "bpy":
            return Path(*parts[:index])
    return input_path.parent.parent if input_path.parent.name.lower() == "bpy" else input_path.parent
