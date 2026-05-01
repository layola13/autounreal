from __future__ import annotations

from typing import Any

from . import ue


def velocity(component: Any) -> Any:
    return ue.MoverComponent_GetVelocity(self=component)


def target_orientation(component: Any) -> Any:
    return ue.MoverComponent_GetTargetOrientation(self=component)


def movement_intent(component: Any) -> Any:
    return ue.MoverComponent_GetMovementIntent(self=component)

def last_input_command(component: Any) -> Any:
    return ue.MoverComponent_GetLastInputCmd(self=component)


def data_from_collection(collection: Any) -> Any:
    return ue.MoverDataCollectionLibrary_K2_GetDataFromCollection(Collection=collection)


def add_data_to_collection(collection: Any, source: Any) -> Any:
    return ue.MoverDataCollectionLibrary_K2_AddDataToCollection(Collection=collection, SourceAsRawBytes=source)


def set_directional_input(inputs: Any, direction: Any) -> Any:
    return ue.MoverDataModelBlueprintLibrary_SetDirectionalInput(Inputs=inputs, DirectionInput=direction)


def is_crouching(component: Any) -> Any:
    return ue.CharacterMoverComponent_IsCrouching(self=component)


def uncrouch(component: Any) -> Any:
    return ue.un_crouch(self=component)

