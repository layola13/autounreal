# Copyright sonygodx@gmail.com. All Rights Reserved.
"""
ue_bp_dsl/types.py — Pin-type constants and helpers for the Blueprint DSL.

Use these constants in node() defaults or variable declarations to avoid
raw magic strings.

Example::

    from ue_bp_dsl.types import PC_Boolean, PC_Float, PC_Object, make_type

    var("bIsAlive",  PC_Boolean)
    var("Speed",     PC_Float)
    var("Owner",     make_type(PC_Object, sub="Actor"))
"""

from __future__ import annotations

# ── Primitive pin categories (match UEdGraphSchema_K2::PC_* constants) ────────

PC_Boolean    = "bool"
PC_Byte       = "byte"
PC_Int        = "int"
PC_Int64      = "int64"
PC_Float      = "float"
PC_Double     = "double"
PC_Real       = "real"
PC_String     = "string"
PC_Name       = "name"
PC_Text       = "text"
PC_Vector     = "struct/ScriptStruct'/Script/CoreUObject.Vector'"
PC_Rotator    = "struct/ScriptStruct'/Script/CoreUObject.Rotator'"
PC_Transform  = "struct/ScriptStruct'/Script/CoreUObject.Transform'"
PC_Color      = "struct/ScriptStruct'/Script/CoreUObject.LinearColor'"
PC_Object     = "object"
PC_Class      = "class"
PC_SoftObject = "softobject"
PC_SoftClass  = "softclass"
PC_Interface  = "interface"
PC_Delegate   = "delegate"
PC_MCDelegate = "mcdelegate"
PC_Exec       = "exec"
PC_Wildcard   = "wildcard"
PC_Enum       = "byte"   # enums are exposed as byte in K2

# ── Container types ───────────────────────────────────────────────────────────

CONTAINER_NONE  = "None"
CONTAINER_ARRAY = "Array"
CONTAINER_SET   = "Set"
CONTAINER_MAP   = "Map"


def make_type(
    category:   str,
    *,
    sub:        str = "",
    container:  str = CONTAINER_NONE,
) -> str:
    """
    Build a DSL type string understood by UBPDirectImporter::ParsePinType.

    :param category:  One of the PC_* constants above
    :param sub:       Sub-category object path (class name, struct name, …)
    :param container: CONTAINER_ARRAY / CONTAINER_SET / CONTAINER_MAP / CONTAINER_NONE
    :return:          Type string, e.g. "object/Actor" or "array:object/Actor"
    """
    base = f"{category}/{sub}" if sub else category
    if container != CONTAINER_NONE:
        return f"{container.lower()}:{base}"
    return base
