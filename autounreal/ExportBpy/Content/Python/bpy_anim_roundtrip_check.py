from __future__ import annotations

import ast
import re
from pathlib import Path
from typing import Any, Iterable, List, Tuple

SOURCE_ABP_CLASS_FRAGMENT = "SandboxCharacter_CMC_ABP_C"
POSE_TOP_PREFIX = "CHT_PoseSearchDatabases_For_"
POSE_CHILD_PREFIXES = (
    "CHT_PoseSearchDatabases_Dense_For_",
    "CHT_PoseSearchDatabases_Sparse_For_",
    "CHT_PoseSearchDatabases_ExtremeSparse_For_",
)
RE_CHOOSER_REF = re.compile(r"/Script/Chooser\.ChooserTable'([^']+)'")
RE_CONTEXT_CLASS = re.compile(r"AnimBlueprintGeneratedClass'([^']+)'")
RE_FOR_TARGET = re.compile(r"_For_(SandboxCharacter_Mover_ABP[^./']*)")


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

    if "CHT_PoseSearchDatabases.CHT_PoseSearchDatabases" not in graph_text:
        errors.append("Update_MotionMatching EvaluateChooser2 does not reference original CHT_PoseSearchDatabases")

    if expected_target:
        expected_pin = f"EvaluateChooser2.{expected_target}_C"
        expected_type = f"\"{expected_pin}\": \"object/{expected_target}_C\""
        if expected_pin not in graph_text:
            errors.append(f"Update_MotionMatching missing self connection to {expected_pin}")
        if expected_type not in meta_text:
            errors.append(f"Update_MotionMatching missing pin type {expected_type}")
    return errors

def validate_export_dir(export_dir: str | Path, expected_target: str = "") -> Tuple[List[str], List[str]]:
    export_path = Path(export_dir)
    errors: List[str] = []
    warnings: List[str] = []

    if not export_path.exists() or not export_path.is_dir():
        return [f"export dir does not exist: {export_path}"], warnings

    meta_files = sorted(export_path.glob("CHT*__asset__.meta.py"))
    if not meta_files:
        warnings.append("no ChooserTable meta files found; check is inconclusive")
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
        if target and any(SOURCE_ABP_CLASS_FRAGMENT in cls for cls in classes):
            errors.append(f"{path.name}: retargeted chooser ContextData still points to source {SOURCE_ABP_CLASS_FRAGMENT}")

        if target and asset_name.startswith(POSE_TOP_PREFIX):
            retargeted_child_refs = [ref for ref in refs if any(prefix + target in ref for prefix in POSE_CHILD_PREFIXES)]
            if retargeted_child_refs:
                errors.append(
                    f"{path.name}: top PoseSearch chooser points to retargeted child tables; "
                    "known TPOSE risk unless every child context is valid"
                )

        if target and (asset_name.startswith("CHT_CMCCharacterAnimations_For_") or asset_name.startswith(POSE_TOP_PREFIX)):
            expected_class = f"/Game/Blueprints/Test/{target}.{target}_C"
            if classes and expected_class not in classes:
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



