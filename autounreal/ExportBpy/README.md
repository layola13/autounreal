# ExportBpy AnimBlueprint Roundtrip Notes

This plugin exports Unreal Blueprint assets to BPY text and imports BPY text back into Blueprint assets. The most recent validation focused on AnimBlueprint roundtrip correctness for `SandboxCharacter_Mover_ABP`, especially preventing TPOSE regressions and preserving active state-machine behavior.

## Fixed Roundtrip Issues

### 1. TPOSE regression guard

The first priority was to make sure an imported AnimBlueprint never silently falls back to TPOSE. The roundtrip validation now checks the exported BPY before import and the re-exported BPY after import, so a broken graph can be caught without relying only on viewport screenshots.

Validation target used during the fix:

- Source asset: `/Game/Blueprints/SandboxCharacter_Mover_ABP.SandboxCharacter_Mover_ABP`
- Import target pattern: `/Game/Blueprints/Test/SandboxCharacter_Mover_ABP_<test_name>.SandboxCharacter_Mover_ABP_<test_name>`
- Known good validation import: `/Game/Blueprints/Test/SandboxCharacter_Mover_ABP_statefix07.SandboxCharacter_Mover_ABP_statefix07`

### 2. State machine transition metadata

The imported state machine originally reached Idle again, but the State Controller stayed visually inactive or partially inactive. The root cause was missing transition shared-rule metadata in BPY export. `UAnimStateTransitionNode` now exports the state-machine metadata required by Unreal to restore shared transition behavior:

- `bSharedRules`
- `SharedRulesName`
- `SharedRulesGuid`
- `SharedColor`
- `bSharedCrossfade`
- `SharedCrossfadeName`
- `SharedCrossfadeGuid`
- `SharedCrossfadeIdx`

This restored the On State Entry bindings and allowed the State Controller flow to become active after import.

### 3. Dynamic Chooser dependency handling

An earlier regression came from treating Chooser dependencies as if they always belonged to `SandboxCharacter_CMC_ABP` and `CHT_PoseSearchDatabases`. That was wrong for `SandboxCharacter_Mover_ABP`, which uses Mover-specific classes and Chooser tables such as relaxed pose-search database tables.

The import/export validation now infers Chooser dependencies dynamically from the BPY data instead of hardcoding CMC asset names. This prevents importing the correct Mover AnimBlueprint with the wrong CMC Chooser database context.

### 4. Strict BPY validation script

`Content/Python/bpy_anim_roundtrip_check.py` was added/strengthened as the required preflight and post-import checker. It verifies important AnimBlueprint roundtrip invariants directly from BPY files, including:

- State machine graph content is present.
- State Controller dependencies are present.
- Chooser table references are dynamically consistent with the exported asset.
- Shared transition rule metadata is present.
- Shared transition color metadata is present.
- OffsetRootBone, MotionMatching, BlendStack, OrientationWarping and StrideWarping keep the fallback/runtime `Node` fields that Unreal still needs when pins are bound through property access.

Do not weaken this checker to make an import pass. If it fails, fix the exporter or importer so the BPY faithfully represents the source asset.

### 5. Sideways locomotion regression

The imported `SandboxCharacter_Mover_ABP` could avoid TPOSE and still play sideways. The root cause was importer-side loss of serialized AnimGraph runtime struct fields after node reconstruction/compile. Unreal can rebuild bound anim nodes from property access metadata, but runtime fallback data such as `OffsetRootBone`, `MotionMatching`, `BlendStack`, `OrientationWarping` and `StrideWarping` values must still survive the roundtrip.

The importer now replays serialized AnimGraph runtime structs after binding/reconstruction, after pin defaults, during defaults contract replay and after compile. The exporter also preserves bound anim-node fallback values in BPY so the post-import export can prove these fields survived.

Validated Demo chain:

- Source exports: `tmp/sideways_chain_fix02/orig/`
- Imported targets: `/Game/Blueprints/Demo/AC_TraversalLogic_RTFix02`, `/Game/Blueprints/Demo/SandboxCharacter_Mover_ABP_RTFix02`, `/Game/Blueprints/Demo/SandboxCharacter_Mover_RTFix02`
- Re-exported targets: `tmp/sideways_chain_fix02/demo_export/`
- Key ABP guards passed: `RotationMode=Accumulate`, `TranslationMode=Interpolate`, `TranslationHalflife=0.200000`, `MaxTranslationError=30.000000`, `MotionMatching BlendTime=0.500000`, `BlendStack AnimationTime=-1.000000`, `OrientationWarping WarpingSpace=RootBoneTransform`, `Get_StrafeWarpDirection` binding and `StrideWarping Speed2D` binding.
- CBP reference remap proof: Demo CBP uses `/Game/Blueprints/Demo/SandboxCharacter_Mover_ABP_RTFix02` for `AnimClass` and `/Game/Blueprints/Demo/AC_TraversalLogic_RTFix02` for the traversal component, with no remaining references to the original ABP or traversal component.

## Validation Workflow

Use the original asset as the source, import into a fresh test asset name, then re-export the imported result and compare structure.

1. Export the original AnimBlueprint to BPY.
2. Run `bpy_anim_roundtrip_check.py` on the exported BPY before import.
3. Import into a new asset under `/Game/Blueprints/Test/`.
4. Re-export the imported asset to another BPY folder.
5. Run `bpy_anim_roundtrip_check.py` again on the re-exported BPY.
6. Compare source BPY and re-exported BPY for state-machine transition counts and shared-rule metadata.
7. Optionally open UE and visually confirm the State Controller flow is active.

A successful structural check during this fix showed:

- Transition count matched: `29 = 29`
- Shared-rule transition count matched: `14 = 14`
- Shared transition property mismatches: `0`
- Imported Blueprint functions matched source count: `63`
- Imported Blueprint interface count matched source count: `1`

## Manual Review Notes

Screenshots are useful for final confidence, but they are not enough for regression prevention. The canonical pass/fail signal should come from BPY-level validation first. Visual review should confirm that:

- The AnimBlueprint is not in TPOSE.
- Idle state plays correctly.
- The State Controller node is not gray/inactive.
- Expected transition conditions show active colored/highlighted flow similar to the original Blueprint.

## Development Rules Learned

- Do not compare against `SandboxCharacter_CMC_ABP` when testing `SandboxCharacter_Mover_ABP`.
- Do not reuse old polluted test assets for validation; import into a fresh asset name.
- Do not hardcode project-specific Chooser table names in importer checks.
- Do not relax validation to bypass missing data.
- Always separate export bugs from import bugs by checking BPY before and after import.
- Before closing UE during automated validation, call UnrealMCP `save_all`.
- After every UE import, call UnrealMCP `save_all` before closing or restarting UE.
- Check `UnrealEditor` memory during long import loops; if memory is high, `save_all`, close UE, and restart before continuing.
- Prefer UnrealMCP live compile/trigger compile where possible; restart UE only when the editor or bridge is stuck.

## Relevant Files

- `Source/ExportBpy/Private/BPDirectExporter.cpp` exports transition shared-rule and shared-crossfade metadata.
- `Source/ExportBpy/Private/BPDirectImporter.cpp` handles dynamic Chooser preflight and import behavior.
- `Content/Python/bpy_anim_roundtrip_check.py` validates AnimBlueprint BPY roundtrip invariants.
- `Content/Python/asset_importer.py` performs dynamic target-aware Chooser context preflight.
