from __future__ import annotations

import ast
import builtins
import keyword
import pprint
import re
import shutil
from pathlib import Path
from typing import Iterable

from ..ir import BlueprintIR, GraphIR
from ..lifter.graph_lifter import LiftedGraph
from .upper_emitter import _wrap_long_line


_KNOWN_COMPONENT_TYPES = {
    "CapsuleComponent",
    "SkeletalMeshComponent",
    "CharacterMoverComponent",
    "ChildActorComponent",
    "Controller",
    "MotionWarpingComponent",
    "SpringArmComponent",
    "CameraComponent",
    "GameplayCameraComponent",
    "GameplayCameraComponentBase",
    "NavMoverComponent",
    "AudioComponent",
}


_UE_ALIAS_MAP = {
    "Actor": "UActor",
    "ActorComponent": "UActorComponent",
    "AIController": "UAIController",
    "AnimBlueprintGeneratedClass": "UAnimBlueprintGeneratedClass",
    "AnimInstance": "UAnimInstance",
    "AnimMontage": "UAnimMontage",
    "AnimNode": "UAnimNode",
    "AnimSequence": "UAnimSequence",
    "AnimSequenceBase": "UAnimSequenceBase",
    "AnimationAsset": "UAnimationAsset",
    "AudioComponent": "UAudioComponent",
    "BlendSpace": "UBlendSpace",
    "BlendStackAnimNode": "UBlendStackAnimNode",
    "BlueprintGeneratedClass": "UBlueprintGeneratedClass",
    "BlueprintObject": "UBlueprintObject",
    "CameraComponent": "UCameraComponent",
    "CapsuleComponent": "UCapsuleComponent",
    "Character": "UCharacter",
    "CharacterMoverComponent": "UCharacterMoverComponent",
    "ChildActorComponent": "UChildActorComponent",
    "Controller": "UController",
    "FootPlacementAnimNode": "UFootPlacementAnimNode",
    "GameplayCameraComponent": "UGameplayCameraComponent",
    "GameplayCameraComponentBase": "UGameplayCameraComponentBase",
    "MeshComponent": "UMeshComponent",
    "MotionMatchingAnimNode": "UMotionMatchingAnimNode",
    "MotionWarpingComponent": "UMotionWarpingComponent",
    "MoverComponent": "UMoverComponent",
    "MovementComponent": "UMovementComponent",
    "NavMoverComponent": "UNavMoverComponent",
    "OffsetRootBoneAnimNode": "UOffsetRootBoneAnimNode",
    "OrientationWarpingAnimNode": "UOrientationWarpingAnimNode",
    "Pawn": "UPawn",
    "PawnMovementComponent": "UPawnMovementComponent",
    "PlayerController": "UPlayerController",
    "PoseLink": "UPoseLink",
    "PoseSearchDatabase": "UPoseSearchDatabase",
    "PoseSearchHistory": "UPoseSearchHistory",
    "PoseSearchHistoryCollectorAnimNode": "UPoseSearchHistoryCollectorAnimNode",
    "PoseSearchResult": "UPoseSearchResult",
    "PrimitiveComponent": "UPrimitiveComponent",
    "SceneComponent": "USceneComponent",
    "ShapeComponent": "UShapeComponent",
    "SkeletalMesh": "USkeletalMesh",
    "SkeletalMeshComponent": "USkeletalMeshComponent",
    "Skeleton": "USkeleton",
    "SkinnedMeshComponent": "USkinnedMeshComponent",
    "SpringArmComponent": "USpringArmComponent",
    "UObject": "UObject",
}


def emit_human_package(bp: BlueprintIR, lifted_graphs: Iterable[LiftedGraph], output_dir: str | Path) -> Path:
    out = Path(output_dir).resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)
    lifted_list = list(lifted_graphs)
    local_functions = {
        item.graph.graph_name: (_graph_module_name(item.graph), _function_name(item.graph))
        for item in lifted_list
        if item.graph.kind == "function"
    }
    (out / "README.md").write_text(_readme(bp), encoding="utf-8", newline="\n")
    (out / "__init__.py").write_text('"""Human-readable Blueprint source."""\n', encoding="utf-8", newline="\n")
    _write_support_modules(bp, out, lifted_list, local_functions)
    _write_class_defaults(bp, out)
    _write_blueprint_class(bp, lifted_list, local_functions, out)
    return out




def _write_class_defaults(bp: BlueprintIR, out: Path) -> None:
    class_defaults = {str(key): _human_default_value(value) for key, value in bp.defaults}
    component_defaults = {
        str(component.get("name")): _human_default_map(dict(component.get("properties") or {}))
        for component in bp.components
        if component.get("name") and component.get("properties")
    }
    variable_defaults = {
        str(variable.get("name")): _human_default_value(variable.get("default"), _default_context_for_variable(variable))
        for variable in bp.variables
        if variable.get("name") and variable.get("default") not in {None, ""}
    }
    lines = [
        '"""Human-readable Blueprint class and component defaults."""',
        "",
        "from .domain_types import (",
        "    ExperimentalStateMachineState,",
        "    Gait,",
        "    MovementDirectionBias,",
        "    CameraMode,",
        "    MovementMode,",
        "    MovementState,",
        "    RotationMode,",
        "    Stance,",
        "    TraversalActionType,",
        "    Component,",
        "    AnimationAsset,",
        "    UAnimationAsset,",
        "    BlendStackAnimNode,",
        "    UBlendStackAnimNode,",
        "    FootPlacementAnimNode,",
        "    OffsetRootBoneAnimNode,",
        "    OrientationWarpingAnimNode,",
        "    PawnMovementComponent,",
        "    SkeletalMesh,",
        "    USkeletalMesh,",
        "    Skeleton,",
        "    USkeleton,",
        "    Vector,",
        ")",
        "",
        "CLASS_DEFAULTS = " + _format_defaults(class_defaults),
        "",
        "COMPONENT_DEFAULTS = " + _format_defaults(component_defaults),
        "",
        "VARIABLE_DEFAULTS = " + _format_defaults(variable_defaults),
        "",
        "__all__ = ['CLASS_DEFAULTS', 'COMPONENT_DEFAULTS', 'VARIABLE_DEFAULTS']",
        "",
    ]
    (out / "class_defaults.py").write_text("\n".join(lines), encoding="utf-8", newline="\n")


def _human_default_map(values: dict[str, object]) -> dict[str, object]:
    return {key: _human_default_value(value, _default_context_for_name(key)) for key, value in values.items()}


def _default_context_for_variable(variable: dict[str, object]) -> str:
    type_str = str(variable.get("type") or "")
    if "E_MovementMode" in type_str:
        return "MovementMode"
    if "E_Gait" in type_str:
        return "Gait"
    if "E_RotationMode" in type_str:
        return "RotationMode"
    if "E_Stance" in type_str:
        return "Stance"
    if "E_MovementState" in type_str:
        return "MovementState"
    if "E_MovementDirectionBias" in type_str:
        return "MovementDirectionBias"
    if "E_MovementDirection" in type_str:
        return "MovementDirection"
    if "E_ExperimentalStateMachineState" in type_str:
        return "ExperimentalStateMachineState"
    if "E_TraversalActionType" in type_str:
        return "TraversalActionType"
    return _default_context_for_name(str(variable.get("name") or ""))


def _default_context_for_name(name: str) -> str:
    normalized = re.sub(r"_[0-9]+_[0-9A-Fa-f]+$", "", name)
    lowered = normalized.lower()
    if "movementmode" in lowered or lowered == "movement_mode":
        return "MovementMode"
    if lowered == "gait" or lowered.startswith("gait_"):
        return "Gait"
    if "rotationmode" in lowered or lowered == "rotation_mode":
        return "RotationMode"
    if lowered == "stance" or lowered.startswith("stance_"):
        return "Stance"
    if "movementstate" in lowered or lowered == "movement_state":
        return "MovementState"
    if "movementdirectionbias" in lowered:
        return "MovementDirectionBias"
    if "movementdirection" in lowered or lowered == "movement_direction":
        return "MovementDirection"
    if "statemachinestate" in lowered or normalized == "StateMachineState":
        return "ExperimentalStateMachineState"
    if "cameramode" in lowered or lowered == "camera_mode":
        return "CameraMode"
    if "traversalactiontype" in lowered:
        return "TraversalActionType"
    return "MovementMode"


def _human_default_value(value: object, enum_context: str = "MovementMode") -> object:
    if not isinstance(value, str):
        return value
    text = value.strip()
    if text == "True":
        return True
    if text == "False":
        return False
    if text == "None":
        return None
    if re.fullmatch(r"[-+]?\d+", text):
        return int(text)
    if re.fullmatch(r"[-+]?(?:\d+\.\d*|\d*\.\d+)", text):
        return float(text)
    vector_match = re.fullmatch(r"(?P<x>-?\d+\.\d+),(?P<y>-?\d+\.\d+),(?P<z>-?\d+\.\d+)", text)
    if vector_match:
        return _RawExpr(
            f"Vector({float(vector_match.group('x')):g}, "
            f"{float(vector_match.group('y')):g}, "
            f"{float(vector_match.group('z')):g})"
        )
    if text.startswith("NewEnumerator"):
        return _RawExpr(_enum_expr(enum_context, text))
    if "NewEnumerator" in text:
        return _RawExpr(_replace_enum_tokens_in_default(text))
    return value


def _enum_expr(enum_context: str, text: str) -> str:
    names = {
        "MovementMode": {"NewEnumerator4": "ON_GROUND", "NewEnumerator5": "IN_AIR", "NewEnumerator6": "SLIDING", "NewEnumerator7": "TRAVERSING"},
        "Gait": {"NewEnumerator0": "WALK", "NewEnumerator1": "RUN", "NewEnumerator2": "SPRINT"},
        "RotationMode": {"NewEnumerator0": "ORIENT_TO_MOVEMENT", "NewEnumerator1": "STRAFE", "NewEnumerator2": "AIM"},
        "Stance": {"NewEnumerator0": "STAND", "NewEnumerator1": "CROUCH"},
        "MovementState": {"NewEnumerator0": "MOVING", "NewEnumerator4": "IDLE"},
        "MovementDirection": {"NewEnumerator0": "FORWARD", "NewEnumerator1": "RIGHT", "NewEnumerator2": "BACKWARD", "NewEnumerator3": "LEFT", "NewEnumerator4": "FORWARD_RIGHT", "NewEnumerator5": "FORWARD_LEFT"},
        "CameraMode": {"NewEnumerator0": "DEFAULT", "NewEnumerator1": "ORBIT", "NewEnumerator2": "AIM", "NewEnumerator3": "TWIN_STICK"},
        "MovementDirectionBias": {"NewEnumerator0": "LEFT_FOOT_FORWARD", "NewEnumerator1": "RIGHT_FOOT_FORWARD"},
        "ExperimentalStateMachineState": {
            "NewEnumerator0": "IDLE",
            "NewEnumerator1": "LOOP",
            "NewEnumerator2": "LOCOMOTION",
            "NewEnumerator3": "AIR",
            "NewEnumerator4": "TRANSITION",
            "NewEnumerator5": "BREAK",
            "NewEnumerator6": "SLIDE",
            "NewEnumerator8": "TRANSITION_TO_SLIDE",
            "NewEnumerator9": "SLIDE_LOOP",
        },
        "TraversalActionType": {"NewEnumerator0": "VAULT", "NewEnumerator1": "HURDLE", "NewEnumerator2": "MANTLE", "NewEnumerator3": "TRAVERSE"},
    }
    unknown_match = re.fullmatch(r"NewEnumerator(?P<index>\d+)", text)
    fallback = f"UNKNOWN_{unknown_match.group('index')}" if unknown_match else re.sub(r"(?<!^)(?=[A-Z])", "_", text).upper()
    return f"{enum_context}.{names.get(enum_context, {}).get(text, fallback)}"


def _replace_enum_tokens_in_default(text: str) -> str:
    converted = repr(text)
    field_pattern = re.compile(r"(?P<field>[A-Za-z][A-Za-z0-9_]*?)(?:_\d+_[0-9A-Fa-f]{16,})?=(?P<token>NewEnumerator\d+)")

    def field_repl(match: re.Match[str]) -> str:
        field_name = match.group("field")
        token = match.group("token")
        return f"{field_name}={_enum_expr(_default_context_for_name(field_name), token)}"

    converted = field_pattern.sub(field_repl, converted)
    return re.sub(r"NewEnumerator\d+", lambda match: _enum_expr("MovementMode", match.group(0)), converted)


class _RawExpr(str):
    pass


def _format_defaults(value: object) -> str:
    formatted = pprint.pformat(value, sort_dicts=True, width=120)
    return re.sub(r"_RawExpr\((?P<quote>['\"])(?P<body>.*?)(?P=quote)\)", lambda match: match.group("body"), formatted)

def _write_support_modules(bp: BlueprintIR, out: Path, lifted_graphs: list[LiftedGraph], local_functions: dict[str, tuple[str, str]]) -> None:
    dataclass_blocks = _collect_domain_dataclasses(lifted_graphs, local_functions)
    custom_component_blocks = _custom_component_class_blocks(bp)
    domain_lines = [
        "from __future__ import annotations",
        "",
        "from dataclasses import dataclass",
        "from typing import Any",
        "",
        "from Plugins.autounreal.autounreal.BpyDecompiler.std.enums import (",
        "    ExperimentalStateMachineState,",
        "    Gait,",
        "    MovementDirection,",
        "    MovementDirectionBias,",
        "    CameraMode,",
        "    MovementMode,",
        "    MovementState,",
        "    RotationMode,",
        "    Stance,",
        "    TraversalActionType,",
        ")",
        "from Plugins.autounreal.autounreal.BpyDecompiler.std.types import (",
        "    UObject,",
        "    Actor,",
        "    AActor,",
        "    ActorComponent,",
        "    AIController,",
        "    AAIController,",
        "    AnimInstance,",
        "    UAnimInstance,",
        "    BlueprintGeneratedClass,",
        "    UBlueprintGeneratedClass,",
        "    AnimBlueprintGeneratedClass,",
        "    UAnimBlueprintGeneratedClass,",
        "    AnimSingleNodeInstance,",
        "    UAnimSingleNodeInstance,",
        "    AnimNode,",
        "    UAnimNode,",
        "    AnimSequenceBase,",
        "    UAnimSequenceBase,",
        "    AnimMontage,",
        "    AnimSequence,",
        "    UAnimSequence,",
        "    BlendSpace,",
        "    UBlendSpace,",
        "    PoseLink,",
        "    UPoseLink,",
        "    AudioComponent,",
        "    BlueprintObject,",
        "    UBlueprintObject,",
        "    CameraComponent,",
        "    Controller,",
        "    AController,",
        "    UController,",
        "    CapsuleComponent,",
        "    Character,",
        "    ACharacter,",
        "    UCharacter,",
        "    CharacterMoverComponent,",
        "    Controller,",
        "    AController,",
        "    UController,",
        "    ChildActorComponent,",
        "    Component,",
        "    GameplayCameraComponent,",
        "    GameplayCameraComponentBase,",
        "    MeshComponent,",
        "    MotionWarpingComponent,",
        "    MoverComponent,",
        "    MotionMatchingAnimNode,",
        "    UMotionMatchingAnimNode,",
        "    MovementComponent,",
        "    MotionMatchingAnimNode,",
        "    UMotionMatchingAnimNode,",
        "    NavMoverComponent,",
        "    Pawn,",
        "    APawn,",
        "    UPawn,",
        "    GameModeBase,",
        "    AGameModeBase,",
        "    GameMode,",
        "    AGameMode,",
        "    GameStateBase,",
        "    AGameStateBase,",
        "    GameState,",
        "    AGameState,",
        "    PlayerState,",
        "    APlayerState,",
        "    PlayerController,",
        "    APlayerController,",
        "    UPlayerController,",
        "    PoseLink,",
        "    UPoseLink,",
        "    PoseSearchDatabase,",
        "    UPoseSearchDatabase,",
        "    PoseSearchHistory,",
        "    UPoseSearchHistory,",
        "    PoseSearchHistoryCollectorAnimNode,",
        "    UPoseSearchHistoryCollectorAnimNode,",
        "    PoseSearchResult,",
        "    UPoseSearchResult,",
        "    GameModeBase,",
        "    AGameModeBase,",
        "    GameMode,",
        "    AGameMode,",
        "    GameStateBase,",
        "    AGameStateBase,",
        "    GameState,",
        "    AGameState,",
        "    PlayerState,",
        "    APlayerState,",
        "    PlayerController,",
        "    APlayerController,",
        "    UPlayerController,",
        "    PoseSearchDatabase,",
        "    UPoseSearchDatabase,",
        "    PoseSearchHistory,",
        "    UPoseSearchHistory,",
        "    PoseSearchHistoryCollectorAnimNode,",
        "    UPoseSearchHistoryCollectorAnimNode,",
        "    PoseSearchResult,",
        "    UPoseSearchResult,",
        "    PrimitiveComponent,",
        "    SceneComponent,",
        "    ShapeComponent,",
        "    SkeletalMeshComponent,",
        "    USkeletalMeshComponent,",
        "    SkinnedMeshComponent,",
        "    SpringArmComponent,",
        "    USpringArmComponent,",
        "    AnimationAsset,",
        "    UAnimationAsset,",
        "    BlendStackAnimNode,",
        "    UBlendStackAnimNode,",
        "    FootPlacementAnimNode,",
        "    OffsetRootBoneAnimNode,",
        "    OrientationWarpingAnimNode,",
        "    PawnMovementComponent,",
        "    SkeletalMesh,",
        "    USkeletalMesh,",
        "    Skeleton,",
        "    USkeleton,",
        "    Vector,",
        ")",
        "",
    ]
    if custom_component_blocks:
        domain_lines.extend(custom_component_blocks)
        domain_lines.append("")
    if dataclass_blocks:
        domain_lines.extend(dataclass_blocks)
        domain_lines.append("")
    exported_names = [
        "ExperimentalStateMachineState",
        "Gait",
        "MovementDirection",
        "MovementDirectionBias",
        "CameraMode",
        "MovementMode",
        "MovementState",
        "RotationMode",
        "Stance",
        "TraversalActionType",
        "AnimInstance",
        "BlueprintGeneratedClass",
        "AnimBlueprintGeneratedClass",
        "AnimSingleNodeInstance",
        "AnimNode",
        "AnimSequenceBase",
        "AnimMontage",
        "AnimSequence",
        "AnimationAsset",
        "BlendSpace",
        "BlendStackAnimNode",
        "PoseLink",
        "Actor",
        "AIController",
        "BlueprintObject",
        "Character",
        "ActorComponent",
        "AIController",
        "GameModeBase",
        "GameMode",
        "GameStateBase",
        "GameState",
        "PlayerState",
        "AnimationAsset",
        "AudioComponent",
        "BlendStackAnimNode",
        "CameraComponent",
        "CapsuleComponent",
        "CharacterMoverComponent",
        "ChildActorComponent",
        "Controller",
        "Controller",
        "Component",
        "GameplayCameraComponent",
        "GameplayCameraComponentBase",
        "MeshComponent",
        "MotionWarpingComponent",
        "MotionMatchingAnimNode",
        "MoverComponent",
        "MovementComponent",
        "NavMoverComponent",
        "Pawn",
        "PawnMovementComponent",
        "PlayerController",
        "PoseSearchDatabase",
        "PoseSearchHistory",
        "PoseSearchHistoryCollectorAnimNode",
        "PoseSearchResult",
        "PrimitiveComponent",
        "SceneComponent",
        "ShapeComponent",
        "SkeletalMesh",
        "SkeletalMeshComponent",
        "Skeleton",
        "UActor",
        "UAIController",
        "UBlueprintGeneratedClass",
        "UAnimBlueprintGeneratedClass",
        "UAnimInstance",
        "UAnimSingleNodeInstance",
        "UAnimMontage",
        "UAnimNode",
        "UAnimSequence",
        "UAnimSequenceBase",
        "UAnimationAsset",
        "UBlendSpace",
        "UBlendStackAnimNode",
        "UBlueprintGeneratedClass",
        "UCharacter",
        "UController",
        "UFootPlacementAnimNode",
        "UMotionMatchingAnimNode",
        "UOffsetRootBoneAnimNode",
        "UOrientationWarpingAnimNode",
        "UPawn",
        "UPoseLink",
        "UPoseSearchDatabase",
        "UPoseSearchHistory",
        "UPoseSearchHistoryCollectorAnimNode",
        "UPoseSearchResult",
        "SkinnedMeshComponent",
        "SpringArmComponent",
        "Vector",
        *_custom_component_class_names(bp),
        *_domain_dataclass_names(dataclass_blocks),
    ]
    exported_names = list(dict.fromkeys(exported_names))
    domain_lines.append(f"__all__ = {exported_names!r}")
    domain_lines.append("")
    (out / "domain_types.py").write_text("\n".join(domain_lines), encoding="utf-8", newline="\n")
    (out / "ue_helpers.py").write_text(
        "from Plugins.autounreal.autounreal.BpyDecompiler import std\n"
        "from Plugins.autounreal.autounreal.BpyDecompiler.std import actor, gameplay, math, mover, strings, system, vectors\n"
        "from Plugins.autounreal.autounreal.BpyDecompiler.std import InputAction as input_action\n"
        "from Plugins.autounreal.autounreal.BpyDecompiler.std import InputKey as input_key\n"
        "from Plugins.autounreal.autounreal.BpyDecompiler.std import cast_as as cast\n"
        "from Plugins.autounreal.autounreal.BpyDecompiler.std import event_payload, property as property_value, replace\n"
        "\n"
        "__all__ = [\n"
        "    'actor', 'cast', 'event_payload', 'gameplay', 'input_action', 'input_key', 'math',\n"
        "    'mover', 'property_value', 'replace', 'std', 'strings', 'system', 'vectors',\n"
        "]\n",
        encoding="utf-8",
        newline="\n",
    )


def _collect_domain_dataclasses(lifted_graphs: list[LiftedGraph], local_functions: dict[str, tuple[str, str]]) -> list[str]:
    blocks: list[str] = []
    seen: set[str] = set()
    for lifted in lifted_graphs:
        graph = lifted.graph
        body = _humanize_lines(lifted.lines or ["return None"], graph, local_functions)
        output_class = _output_class_name(graph)
        if output_class in seen:
            continue
        struct_names = _used_struct_names("\n".join(body), graph)
        if output_class not in struct_names:
            continue
        block = _dataclass_block(graph, output_class, "\n".join(body))
        if not block:
            continue
        seen.add(output_class)
        if blocks:
            blocks.append("")
        blocks.extend(block)
    return blocks


def _domain_dataclass_names(blocks: list[str]) -> list[str]:
    names: list[str] = []
    for line in blocks:
        match = re.match(r"class (?P<name>[A-Za-z_]\w*):", line)
        if match:
            names.append(match.group("name"))
    return names


def _custom_component_class_blocks(bp: BlueprintIR) -> list[str]:
    blocks: list[str] = []
    for name in _custom_component_class_names(bp):
        blocks.extend([f"class {name}(ActorComponent):", "    pass", ""])
    if blocks:
        blocks.pop()
    return blocks


def _custom_component_class_names(bp: BlueprintIR) -> list[str]:
    names: list[str] = []
    for component in bp.components:
        class_name = component.get("class_name")
        type_name = _component_type_name(class_name)
        if type_name != "ActorComponent" and _is_custom_component_class(class_name) and type_name not in names:
            names.append(type_name)
    return names


def _is_custom_component_class(class_name: object) -> bool:
    raw = str(class_name or "")
    if not raw or raw.startswith("/Script/"):
        return False
    short = raw.rsplit("/", 1)[-1].split(".")[-1].removesuffix("_C")
    return bool(short) and short not in _KNOWN_COMPONENT_TYPES


def _write_blueprint_class(bp: BlueprintIR, lifted_graphs: list[LiftedGraph], local_functions: dict[str, tuple[str, str]], out: Path) -> None:
    class_name = _blueprint_class_name(bp.name)
    base_name = _base_class_name(bp.parent, bp.bp_type)
    rendered_methods = [
        _render_class_method(lifted, local_functions)
        for lifted in lifted_graphs
        if lifted.graph.kind == "function"
    ]
    rendered_events = [
        _render_class_method(lifted, local_functions)
        for lifted in lifted_graphs
        if lifted.graph.kind == "event_graph"
    ]
    declaration_lines = [
        *_class_default_declaration_lines(bp),
        *_component_declaration_lines(bp),
        *_variable_declaration_lines(bp),
    ]
    constructor_lines = _constructor_lines(bp)
    body_source = "\n".join([*declaration_lines, *constructor_lines, *rendered_methods, *rendered_events])
    imports = _blueprint_import_lines(base_name, body_source)
    lines = [
        "from __future__ import annotations",
        "",
        "from typing import Any",
        "",
        *imports,
        "",
        "",
        f"class {class_name}({base_name}):",
        f"    \"\"\"Readable Python model of the {bp.name} Blueprint.\"\"\"",
        "",
    ]
    if declaration_lines:
        lines.extend(declaration_lines)
        lines.append("")
    lines.extend(constructor_lines)
    method_blocks = [*rendered_events, *rendered_methods]
    if method_blocks:
        for method in method_blocks:
            lines.append("")
            lines.extend(method.splitlines())
    elif not declaration_lines:
        lines.append("    pass")
    lines.append("")
    (out / "blueprint.py").write_text("\n".join(lines), encoding="utf-8", newline="\n")


def _class_default_declaration_lines(bp: BlueprintIR) -> list[str]:
    return [f"    { _safe_name(str(key)) }: {_annotation_for_value(value)}" for key, value in bp.defaults]


def _component_type_name(class_name: object) -> str:
    raw = str(class_name or "Component")
    short = raw.rsplit("/", 1)[-1].split(".")[-1]
    short = short.removesuffix("_C")
    safe = _pascal_case(short)
    if safe in _KNOWN_COMPONENT_TYPES:
        return safe
    if raw.startswith("/Script/"):
        return "ActorComponent"
    return safe if safe != "Result" else "ActorComponent"


def _component_declaration_lines(bp: BlueprintIR) -> list[str]:
    lines: list[str] = []
    for component in bp.components:
        name = component.get("name")
        if name:
            lines.append(f"    {_safe_name(str(name))}: {_component_type_name(component.get('class_name'))}")
    return lines


def _variable_declaration_lines(bp: BlueprintIR) -> list[str]:
    lines: list[str] = []
    for variable in bp.variables:
        name = str(variable.get("name") or "")
        if not name or variable.get("default") in {None, ""}:
            continue
        lines.append(f"    {_safe_name(name)}: {_python_type(str(variable.get('type') or ''))}")
    return lines


def _constructor_lines(bp: BlueprintIR) -> list[str]:
    lines = ["    def __init__(self) -> None:", "        super().__init__()"]
    for key, _value in bp.defaults:
        lines.append(f"        self.{_safe_name(str(key))} = CLASS_DEFAULTS[{str(key)!r}]")
    if bp.defaults:
        lines.append("")
    for component in bp.components:
        name = component.get("name")
        if not name:
            continue
        attr = _safe_name(str(name))
        component_type = _component_type_name(component.get("class_name"))
        parent = str(component.get("parent") or "")
        parent_expr = f"self.{_safe_name(parent)}" if parent else "None"
        lines.append(
            f"        self.{attr} = self.create_default_subobject("
            f"{component_type}, name={str(name)!r}, parent={parent_expr}, "
            f"properties=COMPONENT_DEFAULTS.get({str(name)!r}, {{}}))"
        )
    if bp.components:
        lines.append("")
    for variable in bp.variables:
        name = str(variable.get("name") or "")
        if not name or variable.get("default") in {None, ""}:
            continue
        lines.append(f"        self.{_safe_name(name)} = VARIABLE_DEFAULTS[{name!r}]")
    if len(lines) == 2:
        lines.append("        pass")
    return lines

def _blueprint_import_lines(base_name: str, body_source: str) -> list[str]:
    domain_names = [
        base_name,
        "BlueprintObject",
        "Component",
        "BlueprintGeneratedClass",
        "AnimBlueprintGeneratedClass",
        "AnimSingleNodeInstance",
        "AnimNode",
        "AnimSequenceBase",
        "AnimMontage",
        "AnimSequence",
        "AnimationAsset",
        "BlendSpace",
        "BlendStackAnimNode",
        "PoseLink",
        "ActorComponent",
        "AIController",
        "GameModeBase",
        "GameMode",
        "GameStateBase",
        "GameState",
        "PlayerState",
        "AnimationAsset",
        "AudioComponent",
        "BlendStackAnimNode",
        "CameraComponent",
        "CapsuleComponent",
        "CharacterMoverComponent",
        "ChildActorComponent",
        "Controller",
        "Controller",
        "GameplayCameraComponent",
        "GameplayCameraComponentBase",
        "MotionWarpingComponent",
        "MotionMatchingAnimNode",
        "NavMoverComponent",
        "SpringArmComponent",
        "PawnMovementComponent",
        "PlayerController",
        "PoseSearchDatabase",
        "PoseSearchHistory",
        "PoseSearchHistoryCollectorAnimNode",
        "PoseSearchResult",
        "SkeletalMesh",
        "SkeletalMeshComponent",
        "Skeleton",
        "UActor",
        "UAIController",
        "UBlueprintGeneratedClass",
        "UAnimBlueprintGeneratedClass",
        "UAnimInstance",
        "UAnimSingleNodeInstance",
        "UAnimMontage",
        "UAnimNode",
        "UAnimSequence",
        "UAnimSequenceBase",
        "UAnimationAsset",
        "UBlendSpace",
        "UBlendStackAnimNode",
        "UBlueprintGeneratedClass",
        "UCharacter",
        "UController",
        "UFootPlacementAnimNode",
        "UMotionMatchingAnimNode",
        "UOffsetRootBoneAnimNode",
        "UOrientationWarpingAnimNode",
        "UPawn",
        "UPoseLink",
        "UPoseSearchDatabase",
        "UPoseSearchHistory",
        "UPoseSearchHistoryCollectorAnimNode",
        "UPoseSearchResult",
        "ExperimentalStateMachineState",
        "Gait",
        "MovementDirection",
        "MovementDirectionBias",
        "CameraMode",
        "MovementMode",
        "MovementState",
        "RotationMode",
        "Stance",
        "TraversalActionType",
        "Result",
        "Vector",
    ]
    known_domain_types = set(_collect_known_domain_type_names(body_source))
    for match in re.finditer(r"(?<![\w.])([A-Z][A-Za-z0-9_]*)\(", body_source):
        name = match.group(1)
        if name in known_domain_types and name not in domain_names:
            domain_names.append(name)
    for pattern in (
        r"(?m)^    [A-Za-z_][A-Za-z0-9_]*:\s*([A-Z][A-Za-z0-9_]*)",
        r"create_default_subobject\(([A-Z][A-Za-z0-9_]*)",
    ):
        for match in re.finditer(pattern, body_source):
            name = match.group(1)
            if name not in {"Any", "None", "True", "False"} and name not in domain_names:
                domain_names.append(name)
    used_domain = [
        name
        for name in domain_names
        if name in {base_name, "Component"}
        or re.search(rf"(?<![\w.]){re.escape(name)}(?![\w])", body_source)
    ]
    helper_names = ["std", "actor", "cast", "event_payload", "gameplay", "input_action", "input_key", "math", "mover", "property_value", "replace", "strings", "system", "vectors"]
    used_helpers = [name for name in helper_names if re.search(rf"(?<![\w.]){re.escape(name)}(?:\.|\()", body_source)]
    imports = ["from .class_defaults import CLASS_DEFAULTS, COMPONENT_DEFAULTS, VARIABLE_DEFAULTS"]
    imports.append(f"from .domain_types import {', '.join(dict.fromkeys(used_domain))}")
    if used_helpers:
        imports.append(f"from .ue_helpers import {', '.join(used_helpers)}")
    return imports


def _collect_known_domain_type_names(source: str) -> list[str]:
    names = {
        "UObject", "Actor", "ActorComponent", "AIController", "AnimBlueprintGeneratedClass", "AnimInstance", "AnimMontage", "AnimNode",
        "AnimSequence", "AnimSequenceBase", "AnimationAsset", "AudioComponent", "BlendSpace", "BlendStackAnimNode",
        "BlueprintObject", "CameraComponent",
        "CapsuleComponent", "Character", "CharacterMoverComponent", "ChildActorComponent", "Component", "Controller",
        "GameplayCameraComponent", "GameplayCameraComponentBase", "MeshComponent", "MotionWarpingComponent",
        "MotionMatchingAnimNode", "MoverComponent", "MovementComponent", "NavMoverComponent", "Pawn", "PawnMovementComponent",
        "PlayerController", "PoseLink", "PoseSearchDatabase", "PoseSearchHistory", "PoseSearchHistoryCollectorAnimNode",
        "PoseSearchResult", "PrimitiveComponent",
        "SceneComponent", "ShapeComponent", "SkeletalMesh", "SkeletalMeshComponent", "Skeleton",
        "SkinnedMeshComponent", "SpringArmComponent", "Result", "Vector",
    }
    for match in re.finditer(r"(?<![\w.])([A-Z][A-Za-z0-9_]*(?:Result|Inputs|Thresholds|Properties|Camera|Traversal|Animation))\(", source):
        candidate = match.group(1)
        if candidate not in {"ValueError", "IsValid"}:
            names.add(candidate)
    return sorted(names)


def _render_class_method(lifted: LiftedGraph, local_functions: dict[str, tuple[str, str]]) -> str:
    graph = lifted.graph
    function_name = _function_name(graph)
    params = _params(graph)
    body = _humanize_lines(lifted.lines or ["return None"], graph, local_functions)
    body = _rewrite_parameter_references(body, graph)
    body = _rewrite_calls_for_class_methods(body, graph, local_functions)
    lines = _def_lines(function_name, params, _return_annotation(graph, "\n".join(body)))
    rendered: list[str] = []
    for line in lines:
        rendered.append("    " + line)
    if body:
        for line in body:
            for physical in _wrap_long_line(line):
                rendered.append("        " + physical)
    else:
        rendered.append("        pass")
    return "\n".join(rendered)


def _rewrite_parameter_references(body: list[str], graph: GraphIR) -> list[str]:
    mapping = _param_name_map(graph)
    if not mapping:
        return body
    rewritten: list[str] = []
    for line in body:
        text = line
        for original, readable in sorted(mapping.items(), key=lambda item: len(item[0]), reverse=True):
            if original != readable:
                text = re.sub(rf"(?<![\w.]){re.escape(original)}(?![\w])", readable, text)
        rewritten.append(text)
    return rewritten


def _rewrite_calls_for_class_methods(body: list[str], graph: GraphIR, local_functions: dict[str, tuple[str, str]]) -> list[str]:
    current_name = _function_name(graph)
    method_names = {py_name for _graph_name, (_module_name, py_name) in local_functions.items()}
    rewritten: list[str] = []
    for line in body:
        text = line
        for py_name in sorted(method_names, key=len, reverse=True):
            if py_name == current_name:
                continue
            text = re.sub(rf"(?<![\w.]){re.escape(py_name)}\(self,\s*", f"self.{py_name}(", text)
            text = re.sub(rf"(?<![\w.]){re.escape(py_name)}\(self\)", f"self.{py_name}()", text)
        rewritten.append(text)
    return rewritten


def _class_default_attr_lines(bp: BlueprintIR) -> list[str]:
    lines: list[str] = []
    for key, value in bp.defaults:
        attr = _safe_name(str(key))
        annotation = _annotation_for_value(value)
        lines.append(f"    {attr}: {annotation} = CLASS_DEFAULTS[{str(key)!r}]")
    return lines


def _component_attr_lines(bp: BlueprintIR) -> list[str]:
    lines: list[str] = []
    for component in bp.components:
        name = component.get("name")
        if not name:
            continue
        attr = _safe_name(str(name))
        class_name = str(component.get("class_name") or "Component")
        parent = component.get("parent")
        parent_expr = repr(str(parent)) if parent else "None"
        lines.append(
            f"    {attr}: Component = Component(name={str(name)!r}, type={class_name!r}, parent={parent_expr}, "
            f"properties=COMPONENT_DEFAULTS.get({str(name)!r}, {{}}))"
        )
    return lines


def _variable_attr_lines(bp: BlueprintIR) -> list[str]:
    lines: list[str] = []
    for variable in bp.variables:
        name = str(variable.get("name") or "")
        if not name or variable.get("default") in {None, ""}:
            continue
        attr = _safe_name(name)
        annotation = _python_type(str(variable.get("type") or ""))
        lines.append(f"    {attr}: {annotation} = VARIABLE_DEFAULTS[{name!r}]")
    return lines


def _blueprint_class_name(name: str) -> str:
    return _pascal_case(name)


def _base_class_name(parent: str, bp_type: str) -> str:
    parent_leaf = str(parent or "").rsplit("/", 1)[-1].split(".")[-1].removesuffix("_C")
    exact = {
        "Actor": "Actor",
        "Pawn": "Pawn",
        "Character": "Character",
        "Controller": "Controller",
        "PlayerController": "PlayerController",
        "AIController": "AIController",
        "GameModeBase": "GameModeBase",
        "GameMode": "GameMode",
        "GameStateBase": "GameStateBase",
        "GameState": "GameState",
        "PlayerState": "PlayerState",
        "AnimInstance": "AnimInstance",
        "AnimSingleNodeInstance": "AnimSingleNodeInstance",
        "BlueprintGeneratedClass": "BlueprintGeneratedClass",
        "AnimBlueprintGeneratedClass": "AnimBlueprintGeneratedClass",
    }
    if parent_leaf in exact:
        return exact[parent_leaf]
    if bp_type == "AnimBlueprint" or "AnimInstance" in parent_leaf:
        return "AnimInstance"
    for marker, class_name in (
        ("Character", "Character"),
        ("PlayerController", "PlayerController"),
        ("AIController", "AIController"),
        ("Controller", "Controller"),
        ("Pawn", "Pawn"),
        ("GameMode", "GameMode"),
        ("GameState", "GameState"),
        ("PlayerState", "PlayerState"),
        ("Actor", "Actor"),
    ):
        if marker in parent_leaf:
            return class_name
    return "BlueprintObject"


def _annotation_for_value(value: object) -> str:
    if isinstance(value, bool):
        return "bool"
    if isinstance(value, int):
        return "int"
    if isinstance(value, float):
        return "float"
    if isinstance(value, str):
        return "str"
    return "object"

def _readme(bp: BlueprintIR) -> str:
    return (
        f"# {bp.name} humanized Python\n\n"
        "This directory is optimized for human review. Compile-back metadata stays in the sibling upper package; "
        "these files intentionally hide Blueprint decorator/type noise where possible.\n"
    )


def _render_graph(lifted: LiftedGraph, local_functions: dict[str, tuple[str, str]]) -> str:
    graph = lifted.graph
    function_name = _function_name(graph)
    params = _params(graph)
    body = _humanize_lines(lifted.lines or ["return None"], graph, local_functions)
    lines = _top_import_lines(graph, body, local_functions)
    lines.extend(["", ""])
    lines.extend(_def_lines(function_name, params, _return_annotation(graph, "\n".join(body))))
    for line in body:
        for physical in _wrap_long_line(line):
            lines.append("    " + physical)
    lines.append("")
    return "\n".join(lines)


def _humanize_lines(lines: list[str], graph: GraphIR, local_functions: dict[str, tuple[str, str]]) -> list[str]:
    result: list[str] = []
    output_class = _output_class_name(graph)
    output_field_names = {_safe_name(name): _snake_case(name) for name, _type in graph.outputs}
    enum_context = _enum_context_for_graph(graph)
    enum_context_by_indent: dict[int, str] = {0: enum_context}
    for line in lines:
        text = line
        indent = len(text) - len(text.lstrip())
        stripped = text.strip()
        if stripped.startswith("match "):
            enum_context = _enum_context_for_expression(stripped, _enum_context_for_graph(graph))
            enum_context_by_indent[indent + 4] = enum_context
        elif stripped.startswith("case "):
            enum_context = enum_context_by_indent.get(indent + 4, enum_context_by_indent.get(indent, _enum_context_for_graph(graph)))
        else:
            enum_context = _enum_context_for_expression(stripped, _enum_context_for_graph(graph))
        text = text.replace("std.output(", f"{output_class}(")
        text = re.sub(r"\bstd\.make_struct\((?P<args>.+)\)", _replace_make_struct, text)
        text = _replace_inline_struct_dict(text)
        text = re.sub(r"\bstd\.KismetMathLibrary_", "math.", text)
        text = re.sub(r"\bstd\.KismetStringLibrary_", "strings.", text)
        text = re.sub(r"\bstd\.KismetSystemLibrary_", "system.", text)
        text = re.sub(r"\bstd\.GameplayStatics_", "gameplay.", text)
        text = re.sub(r"\bstd\.InputAction\(", "input_action(", text)
        text = re.sub(r"\bstd\.InputKey\(", "input_key(", text)
        text = re.sub(r"\bstd\.cast_as\(", "cast(", text)
        text = _replace_class_targets(text)
        text = re.sub(r"\bstd\.replace\(", "replace(", text)
        text = re.sub(r"\bstd\.event_payload\(", "event_payload(", text)
        text = re.sub(r"\bstd\.property\(", "property_value(", text)
        text = re.sub(r"(?<![\w.])IsValid\(", "std.IsValid(", text)
        text = re.sub(r"(?<![\w.])ForEachLoop\(", "std.ForEachLoop(", text)
        text = _replace_readable_helpers(text)
        text = _replace_vector_literals(text)
        text = _replace_known_enums(text, enum_context)
        text = _replace_boolean_select(text)
        text = _replace_empty_self_args(text)
        text = _replace_local_calls(text, local_functions)
        text = _snake_case_constructor_keywords(text, output_class)
        text = _strip_redundant_boolean_parens(text)
        text = re.sub(r"\bReturnValue=None,?", "", text)
        for original, readable in output_field_names.items():
            if original != "ReturnValue":
                text = re.sub(rf"\b{re.escape(original)}=", f"{readable}=", text)
        result.append(text)
    return _rename_local_temporaries(result)



def _rename_local_temporaries(lines: list[str]) -> list[str]:
    assigned: list[str] = []
    for line in lines:
        match = re.match(r"(?P<indent>\s*)(?P<name>[A-Z][A-Za-z0-9_]*)(?P<suffix>_\d+)?\s*=", line)
        if match and not line.lstrip().startswith("self."):
            name = match.group("name") + (match.group("suffix") or "")
            if name not in assigned:
                assigned.append(name)
    rename_map: dict[str, str] = {}
    used: set[str] = set()
    for name in assigned:
        readable = _snake_case(re.sub(r"^K2_", "", name))
        readable = re.sub(r"_+", "_", readable).strip("_") or "value"
        base = readable
        index = 2
        while readable in used:
            readable = f"{base}_{index}"
            index += 1
        used.add(readable)
        if readable != name:
            rename_map[name] = readable
    if not rename_map:
        return lines
    rewritten: list[str] = []
    for line in lines:
        text = line
        for old, new in sorted(rename_map.items(), key=lambda item: len(item[0]), reverse=True):
            text = re.sub(rf"(?<![\w.]){re.escape(old)}(?![\w])", new, text)
        rewritten.append(text)
    return rewritten


def _strip_redundant_boolean_parens(text: str) -> str:
    return re.sub(r"=\(([^()]+\s*(?:[!<>=]=|<|>)\s*[^()]+)\)", r"=\1", text)

def _replace_inline_struct_dict(text: str) -> str:
    marker = "{'__struct_type__':"
    index = text.find(marker)
    if index < 0:
        return text
    open_index = text.find("{", index)
    if open_index < 0:
        return text
    body = _balanced_brace_body(text, open_index)
    if body is None:
        return text
    try:
        dict_node = ast.parse("{" + body + "}", mode="eval").body
    except SyntaxError:
        return text
    if not isinstance(dict_node, ast.Dict) or not dict_node.keys:
        return text
    first_key = dict_node.keys[0]
    first_value = dict_node.values[0]
    if not isinstance(first_key, ast.Constant) or first_key.value != "__struct_type__":
        return text
    if not isinstance(first_value, ast.Constant):
        return text
    class_name = _struct_class_name(str(first_value.value))
    fields: list[str] = []
    for key_node, value_node in zip(dict_node.keys[1:], dict_node.values[1:]):
        if not isinstance(key_node, ast.Constant):
            return text
        field_name = _snake_case(str(key_node.value))
        value_source = ast.get_source_segment("{" + body + "}", value_node)
        if value_source is None:
            return text
        fields.append(f"{field_name}={value_source}")
    replacement = f"{class_name}({', '.join(fields)})"
    close_index = open_index + len(body) + 2
    return text[:open_index] + replacement + text[close_index:]


def _balanced_brace_body(source: str, open_brace_index: int) -> str | None:
    depth = 0
    quote = ""
    escaped = False
    start = open_brace_index + 1
    for index in range(open_brace_index, len(source)):
        char = source[index]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
            continue
        if char in {'"', "'"}:
            quote = char
            continue
        if char == "{":
            depth += 1
            continue
        if char == "}":
            depth -= 1
            if depth == 0:
                return source[start:index]
    return None


def _replace_make_struct(match: re.Match[str]) -> str:
    args = match.group("args")
    parts = _split_call_args(args)
    if not parts:
        return match.group(0)
    first = parts[0].strip()
    if not (len(first) >= 2 and first[0] in {'\"', "'"} and first[-1] == first[0]):
        return match.group(0)
    try:
        struct_ref = ast.literal_eval(first)
    except Exception:
        return match.group(0)
    class_name = _struct_class_name(str(struct_ref))
    remaining = ", ".join(parts[1:])
    return f"{class_name}({remaining})"



def _replace_class_targets(text: str) -> str:
    class_targets = {
        "/Script/AIModule.AIController": "AIController",
        "/Script/Engine.AnimationAsset": "AnimationAsset",
        "/Script/Engine.AnimSequence": "AnimSequence",
        "/Script/Engine.PlayerController": "PlayerController",
        "/Script/Mover.MoverComponent": "MoverComponent",
    }
    for path, class_name in class_targets.items():
        text = text.replace(repr(path), class_name)
    return text


def _replace_readable_helpers(text: str) -> str:
    text = re.sub(r"std\.MoverComponent_GetVelocity\(self=(?P<arg>[^\)]+)\)", r"mover.velocity(\g<arg>)", text)
    text = re.sub(r"std\.MoverComponent_GetTargetOrientation\(self=(?P<arg>[^\)]+)\)", r"mover.target_orientation(\g<arg>)", text)
    text = re.sub(r"std\.MoverComponent_GetMovementIntent\(self=(?P<arg>[^\)]+)\)", r"mover.movement_intent(\g<arg>)", text)
    text = re.sub(r"std\.K2_GetActorRotation\(\)", "actor.rotation()", text)
    text = re.sub(r"std\.GetActorForwardVector\(\)", "actor.forward_vector()", text)
    text = re.sub(r"std\.vsize_xy\(A=(?P<arg>[^\)]+)\)", r"vectors.size_xy(\g<arg>)", text)
    text = re.sub(
        r"math\.LessLess_VectorRotator\(B=(?P<rotation>actor\.rotation\(\)), A=mover\.velocity\((?P<component>[^\)]+)\)\)",
        r"vectors.rotate(mover.velocity(\g<component>), \g<rotation>)",
        text,
    )
    text = _replace_simple_keyword_helper_call(text, "system.GetConsoleVariableIntValue", "system.console_variable_int", {"VariableName": "name"})
    text = _replace_simple_keyword_helper_call(text, "system.GetConsoleVariableBoolValue", "system.console_variable_bool", {"VariableName": "name"})
    text = _replace_simple_keyword_helper_call(text, "system.GetConsoleVariableFloatValue", "system.console_variable_float", {"VariableName": "name"})
    text = _replace_simple_keyword_helper_call(text, "system.GetConsoleVariableStringValue", "system.console_variable_string", {"VariableName": "name"})
    text = _replace_simple_keyword_helper_call(text, "math.Clamp", "math.clamp", {"Value": "value", "Min": "min_value", "Max": "max_value"})
    text = _replace_simple_keyword_helper_call(text, "math.Add_IntInt", "math.add_int", {"A": "a", "B": "b"})
    text = _replace_simple_keyword_helper_call(text, "math.Subtract_IntInt", "math.subtract_int", {"A": "a", "B": "b"})
    text = _replace_simple_keyword_helper_call(text, "strings.Concat_StrStr", "strings.concat", {"A": "a", "B": "b"})
    text = _replace_simple_keyword_helper_call(text, "strings.Conv_IntToString", "strings.to_string_int", {"InInt": "value"})
    text = _replace_map_range_clamped(text)
    return text


def _replace_simple_keyword_helper_call(text: str, source_name: str, target_name: str, keyword_map: dict[str, str]) -> str:
    pattern = re.compile(rf"(?<![\w.]){re.escape(source_name)}\(")
    while True:
        match = pattern.search(text)
        if match is None:
            return text
        body = _balanced_call_body(text, match.end() - 1)
        if body is None:
            return text
        args = _parse_keyword_args(body)
        if not set(keyword_map).issubset(args):
            return text
        rendered = ", ".join(f"{readable}={args[original]}" for original, readable in keyword_map.items())
        replacement = f"{target_name}({rendered})"
        start = match.start()
        end = match.end() + len(body) + 1
        text = text[:start] + replacement + text[end:]


def _replace_map_range_clamped(text: str) -> str:
    pattern = re.compile(r"math\.MapRangeClamped\(")
    while True:
        match = pattern.search(text)
        if match is None:
            return text
        body = _balanced_call_body(text, match.end() - 1)
        if body is None:
            return text
        args = _parse_keyword_args(body)
        required = {"Value", "InRangeA", "InRangeB", "OutRangeA", "OutRangeB"}
        if not required.issubset(args):
            return text
        replacement = (
            f"math.map_range_clamped("
            f"{args['Value']}, "
            f"in_range=({args['InRangeA']}, {args['InRangeB']}), "
            f"out_range=({args['OutRangeA']}, {args['OutRangeB']})"
            f")"
        )
        start = match.start()
        end = match.end() + len(body) + 1
        text = text[:start] + replacement + text[end:]


def _parse_keyword_args(body: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for part in _split_call_args(body):
        if "=" not in part:
            continue
        name, value = part.split("=", 1)
        result[name.strip()] = value.strip()
    return result

def _replace_vector_literals(text: str) -> str:
    return re.sub(
        r"(?<![\w.])'(?P<x>-?\d+\.\d+),(?P<y>-?\d+\.\d+),(?P<z>-?\d+\.\d+)'",
        lambda match: f"Vector({float(match.group('x')):g}, {float(match.group('y')):g}, {float(match.group('z')):g})",
        text,
    )


def _replace_known_enums(text: str, enum_context: str = "MovementMode") -> str:
    pattern = re.compile(r"'(?P<token>NewEnumerator\d+)'")

    def repl(match: re.Match[str]) -> str:
        return _enum_expr(_enum_context_for_token(text, match.start(), enum_context), match.group("token"))

    return pattern.sub(repl, text)


def _enum_context_for_token(text: str, token_pos: int, default: str) -> str:
    prefix = text[:token_pos]
    candidates = {
        "CameraMode": "CameraMode",
        "MovementDirectionBias": "MovementDirectionBias",
        "MovementDirection": "MovementDirection",
        "RotationMode": "RotationMode",
        "MovementState": "MovementState",
        "MovementMode": "MovementMode",
        "TraversalActionType": "TraversalActionType",
        "Gait": "Gait",
        "Stance": "Stance",
    }
    best_context = default
    best_index = -1
    for marker, context in candidates.items():
        index = prefix.rfind(marker)
        if index > best_index:
            best_index = index
            best_context = context
    return best_context


def _enum_context_for_graph(graph: GraphIR) -> str:
    joined_outputs = " ".join(type_str for _name, type_str in graph.outputs)
    name = graph.graph_name.lower()
    if "E_RotationMode" in joined_outputs or "rotation_mode" in name or "rotationmode" in name:
        return "RotationMode"
    if "E_MovementDirection" in joined_outputs or "movement_direction" in name or "movementdirection" in name:
        return "MovementDirection"
    if "E_MovementState" in joined_outputs or "movement_state" in name or "movementstate" in name:
        return "MovementState"
    if "E_Gait" in joined_outputs or name.endswith("gait"):
        return "Gait"
    if "E_Stance" in joined_outputs or name.endswith("stance"):
        return "Stance"
    return "MovementMode"


def _enum_context_for_expression(text: str, previous: str = "MovementMode") -> str:
    if "CameraMode" in text:
        return "CameraMode"
    if "MovementDirectionBias" in text:
        return "MovementDirectionBias"
    if "RotationMode" in text:
        return "RotationMode"
    if "MovementDirection" in text:
        return "MovementDirection"
    if re.search(r"(?<![A-Za-z])Gait(?![A-Za-z])", text):
        return "Gait"
    if re.search(r"(?<![A-Za-z])Stance(?![A-Za-z])", text):
        return "Stance"
    if "TraversalActionType" in text:
        return "TraversalActionType"
    if "StateMachineState" in text or "ExperimentalStateMachineState" in text:
        return "ExperimentalStateMachineState"
    if "MovementState" in text:
        return "MovementState"
    if "MovementMode" in text:
        return "MovementMode"
    return previous


def _snake_case_constructor_keywords(text: str, class_name: str) -> str:
    pattern = re.compile(rf"(?<![\w.]){re.escape(class_name)}\(")
    output: list[str] = []
    cursor = 0
    while True:
        match = pattern.search(text, cursor)
        if match is None:
            output.append(text[cursor:])
            return "".join(output)
        body = _balanced_call_body(text, match.end() - 1)
        if body is None:
            output.append(text[cursor:])
            return "".join(output)
        end = match.end() + len(body) + 1
        rewritten_parts: list[str] = []
        for part in _split_call_args(body):
            if "=" not in part:
                rewritten_parts.append(part)
                continue
            name, value = part.split("=", 1)
            snake_name = _snake_case(name.strip())
            if snake_name == "return_value":
                continue
            rewritten_parts.append(f"{snake_name}={value.strip()}")
        output.append(text[cursor:match.start()])
        output.append(f"{class_name}({', '.join(rewritten_parts)})")
        cursor = end

def _replace_boolean_select(text: str) -> str:
    pattern = re.compile(r"\(True if \((?P<cond>.*?)\) else False\)")
    return pattern.sub(r"(\g<cond>)", text)


def _replace_empty_self_args(text: str) -> str:
    return re.sub(r"\((self=self)(,\s*)?\)", "(self)", text)


def _replace_local_calls(text: str, local_functions: dict[str, tuple[str, str]]) -> str:
    for graph_name, (_module_name, py_name) in sorted(local_functions.items(), key=lambda item: len(item[0]), reverse=True):
        if not graph_name or graph_name == py_name:
            continue
        pattern = re.compile(rf"(?<![\w.]){re.escape(graph_name)}\((?P<args>[^\)]*)\)")

        def repl(match: re.Match[str]) -> str:
            args = match.group("args").strip()
            if not args:
                return f"{py_name}(self)"
            return f"{py_name}(self, {args})"

        text = pattern.sub(repl, text)
    return text


def _top_import_lines(graph: GraphIR, body_lines: list[str], local_functions: dict[str, tuple[str, str]]) -> list[str]:
    source = "\n".join(body_lines)
    lines = []
    if re.search(r"(?<![\w.])std\.", source):
        lines.append("from .ue_helpers import std")
    std_imports = _used_std_imports(source)
    enum_names = [
        "ExperimentalStateMachineState",
        "Gait",
        "MovementDirection",
        "MovementDirectionBias",
        "CameraMode",
        "MovementMode",
        "MovementState",
        "RotationMode",
        "Stance",
        "TraversalActionType",
    ]
    for enum_name in enum_names:
        if re.search(rf"(?<![\w.]){enum_name}\.", source):
            std_imports.append(enum_name)
    if re.search(r"(?<![\w.])Vector\(", source):
        std_imports.append("Vector")
    std_imports = sorted(set(std_imports))
    enum_imports = [item for item in std_imports if item in enum_names]
    type_imports = [item for item in std_imports if item == "Vector"]
    normal_imports = [item for item in std_imports if item not in {*enum_names, "Vector"}]
    namespace_aliases = _used_namespace_aliases(source)
    helper_imports = sorted(set(normal_imports + namespace_aliases))
    domain_imports = enum_imports + type_imports
    struct_names = _used_struct_names(source, graph)
    dataclass_name = _output_class_name(graph) if _output_class_name(graph) in struct_names else ""
    if dataclass_name and dataclass_name not in domain_imports:
        domain_imports.append(dataclass_name)
    if domain_imports:
        lines.append(f"from .domain_types import {', '.join(domain_imports)}")
    if helper_imports:
        lines.append(f"from .ue_helpers import {', '.join(helper_imports)}")
    local_imports = _human_import_lines(graph, local_functions, source)
    if local_imports:
        lines.append("")
        lines.extend(local_imports)
    return lines


def _human_import_lines(graph: GraphIR, local_functions: dict[str, tuple[str, str]], source: str) -> list[str]:
    imports: list[str] = []
    current_name = _function_name(graph)
    for graph_name, (module_name, py_name) in sorted(local_functions.items(), key=lambda item: item[1][1]):
        if py_name == current_name:
            continue
        if re.search(rf"(?<![\w.]){re.escape(py_name)}\(", source) or re.search(rf"(?<![\w.]){re.escape(graph_name)}\(", source):
            imports.append(f"from .{module_name} import {py_name}")
    return imports



def _dataclass_block(graph: GraphIR, output_class: str, source: str) -> list[str]:
    field_types = _constructor_field_types(source, output_class)
    fields = list(field_types)
    if not fields:
        fields = [_readable_field_name(name) for name, _type in graph.outputs if name != "ReturnValue"]
    lines: list[str] = []
    if not fields:
        return []
    lines.append("@dataclass")
    lines.append(f"class {output_class}:")
    for field_name in fields:
        lines.append(f"    {field_name}: {field_types.get(field_name) or _field_python_type(graph, field_name)}")
    return lines



def _constructor_field_types(source: str, class_name: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    pattern = re.compile(rf"(?<![\w.]){re.escape(class_name)}\(")
    for match in pattern.finditer(source):
        body = _balanced_call_body(source, match.end() - 1)
        if body is None:
            continue
        for part in _split_call_args(body):
            if "=" not in part:
                continue
            name, expr = part.split("=", 1)
            name = name.strip()
            if re.match(r"^[A-Za-z_]\w*$", name) and name not in fields:
                fields[name] = _infer_expr_type(expr.strip())
    return fields



def _infer_expr_type(expr: str) -> str:
    if expr in {"True", "False"} or re.search(r"\b(and|or|not)\b|[!<>=]=|(?<![<>=!])<(?![<>=])|(?<![<>=!])>(?![<>=])", expr):
        return "bool"
    if re.fullmatch(r"[-+]?\d+", expr):
        return "int"
    if re.fullmatch(r"[-+]?(?:\d+\.\d*|\d*\.\d+)", expr):
        return "float"
    if re.match(r"Vector\(", expr):
        return "Vector"
    if len(expr) >= 2 and expr[0] in {"'", "\""} and expr[-1] == expr[0]:
        return "str"
    if re.search(r"\b(map_range_clamped|MapRange|Distance|Radius|Height|Length|Speed|Velocity|Angle|Time|Alpha|Scale|Rate)", expr):
        return "float"
    return "Any"

def _balanced_call_body(source: str, open_paren_index: int) -> str | None:
    depth = 0
    quote = ""
    escaped = False
    start = open_paren_index + 1
    for index in range(open_paren_index, len(source)):
        char = source[index]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
            continue
        if char in {'\"', "'"}:
            quote = char
            continue
        if char == "(":
            depth += 1
            continue
        if char == ")":
            depth -= 1
            if depth == 0:
                return source[start:index]
    return None


def _readable_field_name(name: str) -> str:
    safe = _safe_name(name)
    match = re.match(r"^ReturnValue_(?P<field>.+?)_\d+_[0-9A-Fa-f]{16,}$", safe)
    if match:
        return match.group("field")
    return safe


def _field_python_type(graph: GraphIR, field_name: str) -> str:
    for output_name, type_str in graph.outputs:
        readable = _readable_field_name(output_name)
        if field_name in {output_name, _safe_name(output_name), readable}:
            return _python_type(type_str)
    return "Any"

def _python_type(type_str: str) -> str:
    if type_str in {"bool", "boolean"}:
        return "bool"
    if type_str in {"int", "integer"}:
        return "int"
    if type_str.startswith("real/") or type_str in {"float", "double"}:
        return "float"
    if type_str in {"string", "name", "text"}:
        return "str"
    if "E_RotationMode" in type_str:
        return "RotationMode"
    if "E_MovementDirection" in type_str:
        return "MovementDirection"
    if "E_MovementMode" in type_str:
        return "MovementMode"
    if "E_MovementState" in type_str:
        return "MovementState"
    if "E_Gait" in type_str:
        return "Gait"
    if "E_Stance" in type_str:
        return "Stance"
    return "Any"

def _used_std_imports(source: str) -> list[str]:
    direct_names = {
        "cast": "cast_as as cast",
        "event_payload": "event_payload",
        "input_action": "InputAction as input_action",
        "input_key": "InputKey as input_key",
        "property_value": "property as property_value",
        "replace": "replace",
        "MovementMode": "MovementMode",
        "Vector": "Vector",
    }
    used = [
        import_text
        for alias, import_text in direct_names.items()
        if re.search(rf"(?<![\w.]){alias}\(", source) or re.search(rf"(?<![\w.]){alias}\.", source)
    ]
    return sorted(set(used))



def _used_namespace_aliases(source: str) -> list[str]:
    aliases = [name for name in ("actor", "gameplay", "math", "mover", "strings", "system", "vectors") if re.search(rf"(?<![\w.]){name}\.", source)]
    return sorted(aliases)

def _used_struct_names(source: str, graph: GraphIR) -> list[str]:
    builtin_names = set(dir(builtins))
    candidates = {_output_class_name(graph)}
    for _name, type_str in graph.outputs:
        if type_str.startswith("struct/"):
            candidates.add(_struct_class_name(type_str))
    for match in re.finditer(r"(?<![\w.])([A-Z][A-Za-z0-9_]*)\(", source):
        name = match.group(1)
        if name not in {"True", "False", "None"} and name not in builtin_names:
            candidates.add(name)
    return sorted(
        name
        for name in candidates
        if name not in builtin_names and (name != "Result" or re.search(r"(?<![\w.])Result\(", source))
    )


def _output_class_name(graph: GraphIR) -> str:
    for name, type_str in graph.outputs:
        if name == "ReturnValue" and type_str.startswith("struct/"):
            return _struct_class_name(type_str)
    return "Result"


def _struct_class_name(type_str: str) -> str:
    basename = type_str.rsplit("/", 1)[-1].split(".")[-1]
    if basename.startswith("S_"):
        basename = basename[2:]
    return _pascal_case(basename)


def _pascal_case(value: str) -> str:
    return "".join(part[:1].upper() + part[1:] for part in re.split(r"[^0-9A-Za-z]+", value) if part) or "Result"


def _def_lines(function_name: str, params: list[str], return_annotation: str = "None") -> list[str]:
    suffix = f" -> {return_annotation}:"
    one_line = f"def {function_name}({', '.join(params)}){suffix}"
    if len(one_line) <= 120:
        return [one_line]
    lines = [f"def {function_name}("]
    for param in params:
        lines.append(f"    {param},")
    if params:
        lines[-1] = lines[-1].rstrip(",")
    lines.append(f"){suffix}")
    return lines




def _return_annotation(graph: GraphIR, source: str = "") -> str:
    if graph.kind == "event_graph":
        return "None"
    output_class = _output_class_name(graph)
    if output_class != "Result" and re.search(rf"(?<![\w.]){re.escape(output_class)}\(", source):
        return output_class
    meaningful = [(name, type_str) for name, type_str in graph.outputs if name != "ReturnValue"]
    return_values = [(name, type_str) for name, type_str in graph.outputs if name == "ReturnValue"]
    if len(meaningful) > 1:
        return output_class
    if meaningful:
        return _python_type(meaningful[0][1])
    if return_values:
        return _python_type(return_values[0][1])
    return "None"

def _params(graph: GraphIR) -> list[str]:
    if graph.kind == "event_graph":
        return ["self"]
    return ["self", *_param_name_map(graph).values()]


def _param_name_map(graph: GraphIR) -> dict[str, str]:
    mapping: dict[str, str] = {}
    used = {"self"}
    for name, _type in graph.inputs:
        original = _safe_name(name)
        if original in {"execute", "then"}:
            continue
        readable = _snake_case(original)
        if not readable or readable in {"execute", "then"}:
            readable = original
        base = readable
        index = 2
        while readable in used:
            readable = f"{base}_{index}"
            index += 1
        used.add(readable)
        mapping[original] = readable
    return mapping


def _function_name(graph: GraphIR) -> str:
    if graph.kind == "event_graph":
        return "event_graph"
    return _snake_case(graph.graph_name)


def _graph_file_name(graph: GraphIR) -> str:
    return f"{_graph_module_name(graph)}.py"


def _graph_module_name(graph: GraphIR) -> str:
    safe = re.sub(r"[^0-9A-Za-z_]+", "_", graph.graph_name).strip("_") or "Graph"
    if graph.kind == "event_graph":
        return f"evt_{safe}"
    return f"fn_{safe}"


def _snake_case(value: str) -> str:
    text = re.sub(r"[^0-9A-Za-z]+", "_", value).strip("_")
    text = re.sub(r"(?<=[A-Z])(?=[A-Z][a-z])", "_", text)
    text = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", text)
    text = re.sub(r"_+", "_", text)
    return text.lower() or "graph"


def _safe_name(value: str) -> str:
    text = re.sub(r"\W+", "_", value).strip("_") or "param"
    if text[0].isdigit():
        text = f"param_{text}"
    if keyword.iskeyword(text):
        text = f"{text}_"
    return text


def _split_call_args(source: str) -> list[str]:
    parts: list[str] = []
    start = 0
    depth = 0
    quote = ""
    escaped = False
    for index, char in enumerate(source):
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
            continue
        if char in {'\"', "'"}:
            quote = char
            continue
        if char in "([{":
            depth += 1
            continue
        if char in ")]}":
            depth = max(0, depth - 1)
            continue
        if char == "," and depth == 0:
            parts.append(source[start:index].strip())
            start = index + 1
    tail = source[start:].strip()
    if tail:
        parts.append(tail)
    return parts
