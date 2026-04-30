from __future__ import annotations

import ast
import re
from pathlib import Path
from typing import Any, Iterable, List, Tuple

RE_CHOOSER_REF = re.compile(r"/Script/Chooser\.ChooserTable'([^']+)'")
RE_CONTEXT_CLASS = re.compile(r"AnimBlueprintGeneratedClass'([^']+)'")
RE_FOR_TARGET = re.compile(r"_For_([^./']+)")
RE_BLUEPRINT_PATH = re.compile(r"path\s*=\s*['\"]([^'\"]+)['\"]")


def _load_meta(path: Path) -> dict[str, Any]:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    for stmt in tree.body:
        if not isinstance(stmt, ast.Assign):
            continue
        for target in stmt.targets:
            if isinstance(target, ast.Name) and target.id == "META":
                value = ast.literal_eval(stmt.value)
                return value if isinstance(value, dict) else {}
    return {}


def _walk_strings(value: Any) -> Iterable[str]:
    if isinstance(value, dict):
        for item in value.values():
            yield from _walk_strings(item)
    elif isinstance(value, (list, tuple)):
        for item in value:
            yield from _walk_strings(item)
    elif value is not None:
        yield str(value)


def _infer_target_from_asset(asset_path: str) -> str:
    match = RE_FOR_TARGET.search(asset_path or "")
    return match.group(1) if match else ""


def _context_classes(all_text: str) -> List[str]:
    classes = RE_CONTEXT_CLASS.findall(all_text)
    if "Class=None" in all_text:
        classes.append("Class=None")
    return sorted(set(classes))


def _asset_name(asset_path: str) -> str:
    return (asset_path or "").rsplit("/", 1)[-1].split(".", 1)[0]


def _source_class_name(export_path: Path) -> str:
    root_path = export_path / "__bp__.bp.py"
    if not root_path.exists():
        root_path = export_path / f"{export_path.name}.bp.py"
    if not root_path.exists():
        return ""
    text = root_path.read_text(encoding="utf-8", errors="ignore")
    match = RE_BLUEPRINT_PATH.search(text)
    if not match:
        return ""
    return f"{_asset_name(match.group(1))}_C"


def _has_pose_search_database_result(meta_text: str) -> bool:
    return "object/PoseSearchDatabase|array" in meta_text or "PoseSearchDatabase" in meta_text



def _state_controller_hook_counts(export_path: Path) -> dict[str, int]:
    patterns = (
        "StateEntryFunction",
        "StateFullyBlendedInFunction",
        "StateExitFunction",
        "StateFullyBlendedOutFunction",
        "OnStateEntry",
        "OnUpdate",
    )
    counts = {pattern: 0 for pattern in patterns}
    for path in export_path.glob("*.py"):
        name = path.name
        if "State_Controller" not in name and "OnStateEntry" not in name and "OnUpdate_Transition" not in name:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for pattern in patterns:
            counts[pattern] += text.count(pattern)
    return counts


def validate_state_controller_hooks(export_dir: str | Path) -> list[str]:
    export_path = Path(export_dir)
    counts = _state_controller_hook_counts(export_path)
    errors: list[str] = []
    if counts["StateEntryFunction"] < 7:
        errors.append(f"State Controller has too few StateEntryFunction bindings: {counts['StateEntryFunction']} < 7")
    if counts["OnStateEntry"] < 20:
        errors.append(f"State Controller exported OnStateEntry references look incomplete: {counts['OnStateEntry']} < 20")

    state_files = sorted(export_path.glob("*State_Controller*.bp.py"))
    if len(state_files) < 30:
        errors.append(f"State Controller exported graph sidecars look incomplete: {len(state_files)} < 30")

    all_text = "\n".join(path.read_text(encoding="utf-8", errors="ignore") for path in state_files)
    required_tokens = {
        "state machine graph": "AnimStateEntryNode",
        "state nodes": "AnimStateNode",
        "transition nodes": "AnimStateTransitionNode",
        "transition graph results": "AnimGraphNode_TransitionResult",
        "state result hooks": "AnimGraphNode_StateResult",
        "anim getter source node uid": "AnimGetterSourceNodeUid",
        "anim getter source blueprint": "AnimGetterSourceBlueprint",
        "source node uid": "SourceNodeUid",
    }
    for label, token in required_tokens.items():
        if token not in all_text:
            errors.append(f"State Controller missing {label} token: {token}")

    anim_graph = export_path / "fn_AnimGraph.bp.py"
    if anim_graph.exists():
        anim_graph_text = anim_graph.read_text(encoding="utf-8", errors="ignore")
        if "StateMachineGraphJson" not in anim_graph_text or "other_fn_AnimGraph__StateMachine__State_Controller" not in anim_graph_text:
            errors.append("AnimGraph state machine node is missing State_Controller nested graph reference")
    else:
        errors.append("missing fn_AnimGraph.bp.py for State Controller validation")

    return errors



def validate_update_motion_matching(export_dir: str | Path, expected_target: str = "") -> list[str]:
    export_path = Path(export_dir)
    errors: list[str] = []
    graph_path = export_path / "fn_Update_MotionMatching.bp.py"
    meta_path = export_path / "fn_Update_MotionMatching_meta.py"
    if not graph_path.exists() or not meta_path.exists():
        return errors

    graph_text = graph_path.read_text(encoding="utf-8", errors="ignore")
    meta_text = meta_path.read_text(encoding="utf-8", errors="ignore")
    if "EvaluateChooser2" not in graph_text:
        return errors

    chooser_refs = RE_CHOOSER_REF.findall(graph_text)
    if not chooser_refs:
        errors.append("Update_MotionMatching EvaluateChooser2 has no ChooserTable reference")
    if not _has_pose_search_database_result(meta_text):
        errors.append("Update_MotionMatching EvaluateChooser2 does not expose a PoseSearchDatabase array result")

    if expected_target:
        expected_pin = f"EvaluateChooser2.{expected_target}_C"
        expected_type = f"\"{expected_pin}\": \"object/{expected_target}_C\""
        source_pin_match = re.search(r"EvaluateChooser2\.([A-Za-z0-9_]+_ABP(?:_[A-Za-z0-9_]+)?)_C", graph_text + "\n" + meta_text)
        source_type_match = re.search(r"\"EvaluateChooser2\.([A-Za-z0-9_]+_ABP(?:_[A-Za-z0-9_]+)?)_C\": \"object/\1_C\"", meta_text)
        if expected_pin in graph_text:
            if expected_type not in meta_text:
                errors.append(f"Update_MotionMatching missing pin type {expected_type}")
        elif source_pin_match and source_type_match:
            source_name = source_pin_match.group(1)
            if source_name != source_type_match.group(1):
                errors.append(
                    "Update_MotionMatching source self pin and meta type disagree: "
                    f"{source_name} vs {source_type_match.group(1)}"
                )
        else:
            errors.append(
                f"Update_MotionMatching cannot prove self pin can be retargeted to {expected_pin}"
            )
    return errors

def validate_export_dir(export_dir: str | Path, expected_target: str = "") -> Tuple[List[str], List[str]]:
    export_path = Path(export_dir)
    errors: List[str] = []
    warnings: List[str] = []

    if not export_path.exists() or not export_path.is_dir():
        return [f"export dir does not exist: {export_path}"], warnings

    source_class_name = _source_class_name(export_path)
    meta_files = sorted(export_path.glob("CHT*__asset__.meta.py"))
    graph_texts = "\n".join(
        path.read_text(encoding="utf-8", errors="ignore")
        for path in export_path.glob("*.bp.py")
    )
    if not meta_files:
        chooser_refs = RE_CHOOSER_REF.findall(graph_texts)
        if chooser_refs:
            warnings.append("ChooserTable meta files absent; using original ChooserTable references, no retargeted child Chooser TPOSE risk detected")
        elif "EvaluateChooser" in graph_texts or "ChooserTable" in graph_texts:
            errors.append("Chooser nodes are present but no ChooserTable meta files or original ChooserTable reference were found")
        else:
            warnings.append("no ChooserTable dependencies detected")
        errors.extend(validate_state_controller_hooks(export_path))
        errors.extend(validate_update_motion_matching(export_path, expected_target))
        return errors, warnings

    seen_targets: set[str] = set()
    mismatched_target_files: list[str] = []
    for path in meta_files:
        meta = _load_meta(path)
        asset = str(meta.get("asset", ""))
        target = _infer_target_from_asset(asset)
        if target:
            seen_targets.add(target)
        if expected_target and target and target != expected_target:
            mismatched_target_files.append(f"{path.name} -> {target}")

        all_text = "\n".join(_walk_strings(meta))
        classes = _context_classes(all_text)
        refs = sorted(set(RE_CHOOSER_REF.findall(all_text)))
        asset_name = asset.rsplit("/", 1)[-1].split(".", 1)[0]

        if target and f"_For_{target}_For_{target}" in all_text:
            errors.append(f"{path.name}: double retarget suffix '_For_{target}_For_{target}' found")
        if target and "Class=None" in classes:
            errors.append(f"{path.name}: retargeted chooser has ContextData Class=None")
        if target and source_class_name and any(source_class_name in cls for cls in classes):
            errors.append(f"{path.name}: retargeted chooser ContextData still points to source {source_class_name}")

        if target:
            retargeted_child_refs = [ref for ref in refs if f"_For_{target}" in _asset_name(ref)]
            if retargeted_child_refs:
                errors.append(
                    f"{path.name}: retargeted chooser points to retargeted child tables; "
                    "known TPOSE risk unless the full dependency graph is exported and context-retargeted"
                )

        if target and classes:
            expected_class = f"/Game/Blueprints/Test/{target}.{target}_C"
            if expected_class not in classes:
                errors.append(f"{path.name}: expected target context {expected_class}, got {classes}")

    if mismatched_target_files:
        errors.append(
            f"retargeted chooser suffix does not match expected target '{expected_target}': "
            + "; ".join(mismatched_target_files[:8])
            + ("; ..." if len(mismatched_target_files) > 8 else "")
        )
    if expected_target and expected_target not in seen_targets:
        warnings.append(f"no retargeted chooser assets found for expected target '{expected_target}'")
    errors.extend(validate_state_controller_hooks(export_path))
    errors.extend(validate_update_motion_matching(export_path, expected_target))
    return errors, warnings



