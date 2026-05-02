# BpyDecompiler

BpyDecompiler turns ExportBpy Blueprint packages into a maintainable Python source layer and compiles that human source back to standard ExportBpy DSL BPY.

## Goal

The long-term goal is to treat Unreal Blueprint assets like a source-code project:

- **BPY** is the import/export IR and compiled artifact.
- **human py** is the main editable source layer for humans and LLM coding agents.
- **`.bpy_meta.json`** stores lossless Unreal/Blueprint graph, node, pin, and metadata anchors that should not pollute readable Python code.
- The target closed loop is:
  - UE Blueprint -> ExportBpy BPY
  - BPY -> human py + metadata
  - Human or LLM edits human py
  - human py + metadata -> BPY
  - BPY -> import UE
  - UE re-export BPY
  - Diff remains controlled, explainable, and as close to zero-loss as possible.

## Current Architecture

BpyDecompiler currently emits two layers from an ExportBpy package:

- **Upper/debug layer**: a Python representation that stays close to BPY graph/node structure for diagnostics and lossless fallback.
- **Human layer**: a UE-style Python class surface, e.g. `class SandboxCharacterMoverABP(AnimInstance)`, intended to look and navigate more like normal Python source.

The human layer includes:

- `blueprint.py` for readable class and method logic.
- `class_defaults.py` for Blueprint, variable, and component defaults.
- `domain_types.py` and `std/` stubs for navigable UE-like types and APIs.
- `.bpy_meta.json` as the lossless sidecar used by the compiler.

## Lossless Metadata Anchors

A key fix was added for strict BPY regeneration:

- `GraphCstIR.meta_text` stores the original `_meta.py` text for each graph.
- The parser reads both semantic `META` data and the original `_meta.py` source text.
- The human emitter writes `meta_text` into `.bpy_meta.json`.
- The structural BPY emitter restores original `_meta.py` text verbatim when compiling human py back to DSL BPY.
- Sidecar graphs that originally had no `_meta.py` no longer generate extra meta files.

This makes metadata behave like hidden macro anchors: readable human code stays clean, while Unreal-specific metadata remains exactly recoverable.

## ABP Validation Snapshot

Latest ABP-only validation used:

- Source BPY: `tmp/source_bpy/SandboxCharacter_Mover_ABP`
- Human source: `tmp/bpydecompiler_abp_only/SandboxCharacter_Mover_ABP_human`
- Compiled DSL BPY: `tmp/bpydecompiler_abp_only/SandboxCharacter_Mover_ABP_human_compiled_bpy`
- Full diff report: `tmp/bpydecompiler_abp_only/_aux/full_py_diff_report.txt`

Validation result for all Python source files, excluding runtime `__pycache__` artifacts:

```text
source_count=190
compiled_count=190
missing=0
extra=0
differing=0
```

Unit tests:

```bat
python -m unittest Plugins.autounreal.autounreal.BpyDecompiler.tests.test_smoke -v
```

Latest result:

```text
Ran 15 tests in 79.720s
OK
```

## UE Import Snapshot

The strict-diff compiled ABP package was imported through UnrealMCP:

- Input: `tmp/bpydecompiler_abp_only/SandboxCharacter_Mover_ABP_human_compiled_bpy`
- Target asset: `/Game/Blueprints/Test/SandboxCharacter_Mover_ABP_humandsl04`
- Import status: `success`
- Compile status: `compiled=true`

The only warning was that ChooserTable meta files were absent, so the importer kept original ChooserTable references. No retargeted child Chooser TPOSE risk was detected.


## UnrealMCP Integration

`export_asset_humanpy(...)` and `export_blueprint_humanpy(...)` should use BpyDecompiler as the implementation path. The MCP wrapper performs:

1. Call stable `export_blueprint_bpy` / `export_asset_bpy` through the Unreal bridge into a scratch `_bpy_reference_export/` directory.
2. Run `BpyDecompiler.api.decompile_blueprint(...)` on that ExportBpy package.
3. Write both review layers under the requested output root:
   - `<BlueprintName>_human/` for the editable human Python package.
   - `<BlueprintName>_upper/` for the diagnostic/lossless upper package.
4. Return `producer = "BpyDecompiler"`, `format = "humanpy_directory"`, `output_path = <...>/blueprint.py`, `upper_dir`, and `bpy_reference_dir`.

This replaces the abandoned direct C++ `ExportHumanPy` path. Direct C++ Blueprint serialization remains useful for ExportBpy itself, but human py export should not maintain a second lossy graph emitter.
`compile_human_bpy(human_dir, output_dir)` is the matching MCP compile-back entry. It calls `BpyDecompiler.compiler.emit_bpy_package_from_human(...)` and writes a strict ExportBpy BPY directory that can be passed to `import_blueprint_from_bpy(...)`. Keep this as a separate explicit step so export, human editing, compile-back, and UE import can each be validated independently.

Acceptance checks used for the MCP wrapper:

```bat
python -m py_compile Plugins/autounreal/autounreal/UnrealMCP/Python/unreal_mcp_server_advanced.py
python -m unittest Plugins.autounreal.autounreal.BpyDecompiler.tests.test_smoke -v
```

A direct wrapper smoke call with the UnrealMCP Python venv produced:

```text
success=true
producer=BpyDecompiler
output_dir=tmp/mcp_bpydecompiler_direct_call/SandboxCharacter_Mover_ABP_human
upper_dir=tmp/mcp_bpydecompiler_direct_call/SandboxCharacter_Mover_ABP_upper
diagnostics=[]
```

After changing the server file, restart the MCP server process so the live `export_blueprint_humanpy` tool reloads this wrapper.

## Important Constraints

- Never overwrite original `ExportedBlueprints` packages during compile-back tests.
- Write generated BPY into `tmp/` or another explicit scratch directory.
- Keep auxiliary/debug outputs in a subdirectory such as `_aux/` so the main human and compiled BPY outputs stay easy to review.
- Human py should not expose Blueprint wire/exec-flow style logic as the primary editable surface.
- BPY regeneration must be validated with strict diff, not only Python compile or unit tests.

## Useful Commands

ABP-only regeneration and strict diff helper from the latest validation:

```bat
python tmp/bpydecompiler_abp_only/_aux/run_abp_only_roundtrip.py
python tmp/bpydecompiler_abp_only/_aux/full_py_diff.py
```

Run the smoke test suite:

```bat
python -m unittest Plugins.autounreal.autounreal.BpyDecompiler.tests.test_smoke -v
```

Import the latest compiled ABP package with UnrealMCP or equivalent importer into a scratch asset path such as:

```text
/Game/Blueprints/Test/SandboxCharacter_Mover_ABP_humandsl04
```

## Next Milestones

- Promote the hidden metadata-anchor idea into explicit, reviewable Python decorators/macros when useful, similar in spirit to lightweight framework annotations.
- Continue improving `blueprint.py` so it reads less like Blueprint IR and more like hand-written Python while preserving strict reversibility.
- Expand strict full-file diff checks from ABP-only validation to more Blueprint classes and representative animation/state-machine assets.
- Add an automated human py -> BPY -> UE import -> UE re-export -> diff gate once the importer/exporter path is stable enough for unattended validation.

