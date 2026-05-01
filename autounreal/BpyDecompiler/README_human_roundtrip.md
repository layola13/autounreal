# BpyDecompiler Human Roundtrip Notes

## Current supported edit surface

`*_human` is a readable Python source package backed by `.bpy_meta.json`.
The metadata keeps lossless Blueprint graph/node/pin structure while `blueprint.py`
and `class_defaults.py` provide a human-editable view.

Currently supported human edits:

- `CLASS_DEFAULTS` entries in `class_defaults.py`.
- `COMPONENT_DEFAULTS[component][property]` entries in `class_defaults.py`.
- Simple `VARIABLE_DEFAULTS` values in `class_defaults.py` (`bool`, `int`, `float`, `None`).

The compiler only rewrites BPY CST lines when a human value differs from the
metadata baseline. If there is no edit, original CST text is preserved to keep
`human -> bpy` byte-stable for compared non-meta `.bp.py` files.

## Unsupported edit surface

`blueprint.py` function logic edits are not enabled yet.

A naive implementation that maps Python `return` statements to `Return__*` nodes
by AST traversal order is unsafe. Blueprint return nodes are ordered by the
original graph/CST, while Python AST traversal follows source nesting order. In
`Get_Gait`, those orders differ, so no-edit compilation can accidentally rewrite
valid return enum pins.

## Required design for logic edits

Before enabling `blueprint.py` logic edits, the human emitter must write stable
source-to-CST anchors. Possible forms:

- hidden sidecar map: `.bpy_meta.json` records `function -> source span -> node id`;
- inline non-invasive comments near editable returns, e.g. `# bpy: Return__0.ReturnValue`;
- structured helper wrappers that carry node identity without reintroducing wire-style code.

Only after such anchors exist should the compiler update graph nodes from edited
`blueprint.py` logic.
