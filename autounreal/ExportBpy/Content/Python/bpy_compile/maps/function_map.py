# Copyright sonygodx@gmail.com. All Rights Reserved.
# -*- coding: utf-8 -*-

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, Tuple


@dataclass(frozen=True)
class FunctionSpec:
    ue_ref: str
    params: Dict[str, str] = field(default_factory=dict)
    returns: Tuple[str, ...] = ("ReturnValue",)
    return_types: Tuple[str, ...] = ("unknown",)
    pure: bool = True
    node_class: str = "K2Node_CallFunction"
    extra_props: Dict[str, Any] = field(default_factory=dict)
    default_values: Dict[str, Any] = field(default_factory=dict)
    param_order: Tuple[str, ...] = ()


FUNCTION_MAP: Dict[str, FunctionSpec] = {
    "abs": FunctionSpec(
        ue_ref="KismetMathLibrary::Abs",
        params={"a": "A"},
        return_types=("float",),
        pure=True,
        param_order=("a",),
    ),
    "capsule_trace_single": FunctionSpec(
        ue_ref="KismetSystemLibrary::CapsuleTraceSingle",
        params={
            "start": "Start",
            "end": "End",
            "radius": "Radius",
            "half_height": "HalfHeight",
            "trace_channel": "TraceChannel",
            "trace_complex": "bTraceComplex",
            "actors_to_ignore": "ActorsToIgnore",
            "debug_type": "DrawDebugType",
            "ignore_self": "bIgnoreSelf",
            "trace_color": "TraceColor",
            "trace_hit_color": "TraceHitColor",
            "draw_time": "DrawTime",
        },
        returns=("ReturnValue", "OutHit"),
        return_types=("bool", "struct/HitResult"),
        pure=False,
        param_order=("start", "end", "radius", "half_height"),
    ),
    "draw_debug_sphere": FunctionSpec(
        ue_ref="KismetSystemLibrary::DrawDebugSphere",
        params={
            "center": "Center",
            "radius": "Radius",
            "segments": "Segments",
            "line_color": "LineColor",
            "duration": "Duration",
            "thickness": "Thickness",
        },
        pure=False,
        param_order=("center", "radius"),
    ),
    "get_owner": FunctionSpec(
        ue_ref="GetOwner",
        return_types=("object/Actor",),
        pure=True,
    ),
    "k2_get_actor_location": FunctionSpec(
        ue_ref="Actor::K2_GetActorLocation",
        params={"self": "self"},
        return_types=("Vector",),
        pure=True,
    ),
    "vector_distance": FunctionSpec(
        ue_ref="KismetMathLibrary::Vector_Distance",
        params={"a": "V1", "b": "V2"},
        return_types=("float",),
        pure=True,
        param_order=("a", "b"),
    ),
    "vsize": FunctionSpec(
        ue_ref="KismetMathLibrary::VSize",
        params={"a": "A"},
        return_types=("float",),
        pure=True,
        param_order=("a",),
    ),
    "vsize_xy": FunctionSpec(
        ue_ref="KismetMathLibrary::VSizeXY",
        params={"a": "A"},
        return_types=("float",),
        pure=True,
        param_order=("a",),
    ),
    "has_authority": FunctionSpec(
        ue_ref="Actor::HasAuthority",
        params={"self": "self"},
        return_types=("bool",),
        pure=True,
    ),
    "ignore_component_when_moving": FunctionSpec(
        ue_ref="PrimitiveComponent::IgnoreComponentWhenMoving",
        params={
            "self": "self",
            "component": "Component",
            "should_ignore": "bShouldIgnore",
        },
        pure=False,
        param_order=("self", "component"),
    ),
    "set_movement_mode": FunctionSpec(
        ue_ref="CharacterMovementComponent::SetMovementMode",
        params={
            "self": "self",
            "new_movement_mode": "NewMovementMode",
            "new_custom_mode": "NewCustomMode",
        },
        pure=False,
        param_order=("self", "new_movement_mode"),
    ),
    "queue_next_mode": FunctionSpec(
        ue_ref="MoverComponent::QueueNextMode",
        params={
            "self": "self",
            "desired_mode_name": "DesiredModeName",
            "should_reenter": "bShouldReenter",
        },
        pure=False,
    ),
    "crouch": FunctionSpec(
        ue_ref="CharacterMoverComponent::Crouch",
        params={"self": "self"},
        pure=False,
    ),
    "un_crouch": FunctionSpec(
        ue_ref="CharacterMoverComponent::UnCrouch",
        params={"self": "self"},
        pure=False,
    ),
    "make_vector": FunctionSpec(
        ue_ref="KismetMathLibrary::MakeVector",
        params={
            "self": "self",
            "x": "X",
            "y": "Y",
            "z": "Z",
        },
        returns=("ReturnValue",),
        return_types=("struct/Vector",),
        pure=True,
        param_order=("x", "y", "z"),
    ),
    "add_impulse": FunctionSpec(
        ue_ref="CharacterMoverComponent::AddImpulse",
        params={
            "self": "self",
            "impulse": "Impulse",
            "velocity_change": "bVelocityChange",
        },
        pure=False,
        param_order=("self", "impulse"),
    ),
    "get_movement_mode_name": FunctionSpec(
        ue_ref="MoverComponent::GetMovementModeName",
        params={"self": "self"},
        returns=("ReturnValue",),
        return_types=("name",),
        pure=True,
    ),
    "map_find": FunctionSpec(
        ue_ref="BlueprintMapLibrary::Map_Find",
        params={
            "self": "self",
            "target_map": "TargetMap",
            "key": "Key",
        },
        returns=("Value",),
        return_types=("unknown",),
        pure=True,
    ),
    "get_velocity": FunctionSpec(
        ue_ref="Actor::GetVelocity",
        params={"self": "self"},
        return_types=("struct/Vector",),
        pure=True,
    ),
}
