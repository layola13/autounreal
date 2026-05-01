from __future__ import annotations

from enum import StrEnum


class MovementMode(StrEnum):
    ON_GROUND = "NewEnumerator4"
    IN_AIR = "NewEnumerator5"
    SLIDING = "NewEnumerator6"
    TRAVERSING = "NewEnumerator7"

    NEW_ENUMERATOR_0 = "NewEnumerator0"
    NEW_ENUMERATOR_1 = "NewEnumerator1"
    NEW_ENUMERATOR_2 = "NewEnumerator2"
    NEW_ENUMERATOR_3 = "NewEnumerator3"
    NEW_ENUMERATOR_4 = "NewEnumerator4"
    NEW_ENUMERATOR_5 = "NewEnumerator5"
    NEW_ENUMERATOR_6 = "NewEnumerator6"
    NEW_ENUMERATOR_7 = "NewEnumerator7"

class Gait(StrEnum):
    WALK = "NewEnumerator0"
    RUN = "NewEnumerator1"
    SPRINT = "NewEnumerator2"


class RotationMode(StrEnum):
    ORIENT_TO_MOVEMENT = "NewEnumerator0"
    STRAFE = "NewEnumerator1"
    AIM = "NewEnumerator2"


class Stance(StrEnum):
    STAND = "NewEnumerator0"
    CROUCH = "NewEnumerator1"


class MovementState(StrEnum):
    MOVING = "NewEnumerator0"
    IDLE = "NewEnumerator4"


class MovementDirection(StrEnum):
    FORWARD = "NewEnumerator0"
    RIGHT = "NewEnumerator1"
    BACKWARD = "NewEnumerator2"
    LEFT = "NewEnumerator3"
    FORWARD_RIGHT = "NewEnumerator4"
    FORWARD_LEFT = "NewEnumerator5"


class CameraMode(StrEnum):
    DEFAULT = "NewEnumerator0"
    ORBIT = "NewEnumerator1"
    AIM = "NewEnumerator2"
    TWIN_STICK = "NewEnumerator3"


class MovementDirectionBias(StrEnum):
    LEFT_FOOT_FORWARD = "NewEnumerator0"
    RIGHT_FOOT_FORWARD = "NewEnumerator1"


class ExperimentalStateMachineState(StrEnum):
    IDLE = "NewEnumerator0"
    LOOP = "NewEnumerator1"
    LOCOMOTION = "NewEnumerator2"
    AIR = "NewEnumerator3"
    TRANSITION = "NewEnumerator4"
    BREAK = "NewEnumerator5"
    SLIDE = "NewEnumerator6"
    TRANSITION_TO_SLIDE = "NewEnumerator8"
    SLIDE_LOOP = "NewEnumerator9"


class TraversalActionType(StrEnum):
    VAULT = "NewEnumerator0"
    HURDLE = "NewEnumerator1"
    MANTLE = "NewEnumerator2"
    TRAVERSE = "NewEnumerator3"

