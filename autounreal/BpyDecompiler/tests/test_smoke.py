from __future__ import annotations

import unittest
from pathlib import Path

from Plugins.autounreal.autounreal.BpyDecompiler.parser import parse_blueprint_package
from Plugins.autounreal.autounreal.BpyDecompiler.tools.compileback_diff import validate_compileback_diff
from Plugins.autounreal.autounreal.BpyDecompiler.tools.upper_check import check_upper_dir


ROOT = Path(__file__).resolve().parents[5]
SAMPLES = {
    "CBP": ROOT / "ExportedBlueprints" / "bpy" / "SandboxCharacter_Mover",
    "ABP": ROOT / "ExportedBlueprints" / "bpy" / "SandboxCharacter_Mover_ABP",
}
WORK_DIR = ROOT / "tmp" / "bpydecompiler_unittest_roundtrip"


class BpyDecompilerRoundtripTests(unittest.TestCase):
    def test_parse_cbp_and_abp_packages(self) -> None:
        for label, sample_dir in SAMPLES.items():
            with self.subTest(label=label):
                bp = parse_blueprint_package(sample_dir)
                self.assertTrue(bp.path, label)
                self.assertGreater(len(bp.graphs), 0, label)
                if label == "CBP":
                    self.assertTrue(any(graph.graph_name == "Get_Speed" for graph in bp.graphs))
                if label == "ABP":
                    self.assertTrue(any(graph.graph_name == "AnimGraph" for graph in bp.graphs))

    def test_cbp_compileback_diff_matches_non_meta_bpy(self) -> None:
        result = validate_compileback_diff(SAMPLES["CBP"], WORK_DIR, label="CBP")
        self.assertEqual([], result.missing)
        self.assertEqual([], result.extra)
        self.assertEqual([], result.differing)

    def test_abp_compileback_diff_matches_non_meta_bpy(self) -> None:
        result = validate_compileback_diff(SAMPLES["ABP"], WORK_DIR, label="ABP")
        self.assertEqual([], result.missing)
        self.assertEqual([], result.extra)
        self.assertEqual([], result.differing)


    def test_cbp_human_traversal_is_source_style_python(self) -> None:
        result = validate_compileback_diff(SAMPLES["CBP"], WORK_DIR, label="CBP")
        traversal_path = result.human_dir / "blueprint.py"
        source = traversal_path.read_text(encoding="utf-8")

        self.assertIn("def get_properties_for_animation(self) -> CharacterPropertiesForAnimation:", source)
        self.assertIn("from .domain_types import", source)
        self.assertIn("TraversalCheckInputs", source)
        self.assertIn("case MovementMode.ON_GROUND | MovementMode.SLIDING | MovementMode.TRAVERSING:", source)
        self.assertIn("trace_forward_distance=math.map_range_clamped(", source)
        self.assertIn("trace_origin_offset=Vector(0, 0, 0)", source)
        self.assertNotIn("@std.function", source)
        self.assertNotIn("@bp_function", source)
        self.assertNotIn("def graph", source)
        self.assertNotIn("std.output(", source)
        self.assertNotIn("NewEnumerator", source)
        self.assertNotIn("TraceForwardDirection", source)
        self.assertNotIn("from Plugins.autounreal", source)

        domain_source = (result.human_dir / "domain_types.py").read_text(encoding="utf-8")
        self.assertIn("class ACTraversalLogic(ActorComponent):", domain_source)
        self.assertIn("class ACFoleyEvents(ActorComponent):", domain_source)
        self.assertIn("class TraversalCheckInputs:", domain_source)
        self.assertIn("trace_forward_direction: bool", domain_source)
        self.assertIn("trace_end_offset: Vector", domain_source)


    def test_human_outputs_do_not_use_blueprint_wire_syntax(self) -> None:
        forbidden = (
            "@std.function",
            "@bp_function",
            "std.output(",
            "# Exec flow",
            ".exec",
            ">>",
        )
        for label, sample_dir in SAMPLES.items():
            with self.subTest(label=label):
                result = validate_compileback_diff(sample_dir, WORK_DIR, label=label)
                for path in sorted(result.human_dir.glob("*.py")):
                    if path.name in {"domain_types.py", "ue_helpers.py", "blueprint.py"}:
                        continue
                    source = path.read_text(encoding="utf-8")
                    for pattern in forbidden:
                        self.assertNotIn(pattern, source, f"{path.name} contains {pattern}")
                    self.assertNotIn("from Plugins.autounreal", source, f"{path.name} uses plugin import directly")


    def test_human_blueprint_class_surface_is_ue_style(self) -> None:
        cbp_result = validate_compileback_diff(SAMPLES["CBP"], WORK_DIR, label="CBP")
        source = (cbp_result.human_dir / "blueprint.py").read_text(encoding="utf-8")

        self.assertIn("class SandboxCharacterMover(Pawn):", source)
        self.assertIn("bReplicateMovement: bool", source)
        self.assertIn("def __init__(self) -> None:", source)
        self.assertIn("CharacterMover: CharacterMoverComponent", source)
        self.assertIn("AC_TraversalLogic: ACTraversalLogic", source)
        self.assertIn("self.CharacterMover = self.create_default_subobject(", source)
        self.assertIn("self.AC_TraversalLogic = self.create_default_subobject(ACTraversalLogic", source)
        self.assertIn("def get_movement_direction_thresholds(self) -> MovementDirectionThresholds:", source)
        self.assertNotIn("get_movement_direction_thresholds = _get_movement_direction_thresholds", source)
        self.assertNotIn("from .fn_", source)
        self.assertNotIn("@std.function", source)

        self.assertIn("def get_movement_direction_thresholds(self) -> MovementDirectionThresholds:", source)
        self.assertIn("case RotationMode.ORIENT_TO_MOVEMENT | RotationMode.STRAFE:", source)
        self.assertIn("case MovementDirection.FORWARD | MovementDirection.RIGHT:", source)
        self.assertIn("get_console_variable_int_value", source)
        self.assertIn("get_data_from_collection", source)
        self.assertIn("system.console_variable_int", source)
        self.assertIn("strings.concat", source)
        self.assertIn("math.clamp", source)
        self.assertNotIn("GetConsoleVariableIntValue =", source)
        self.assertNotIn("K2_GetDataFromCollection =", source)
        self.assertNotIn("system.GetConsoleVariableIntValue", source)
        self.assertNotIn("strings.Concat_StrStr", source)
        self.assertNotIn("math.Clamp(", source)

    def test_human_outputs_include_blueprint_defaults(self) -> None:
        cbp_result = validate_compileback_diff(SAMPLES["CBP"], WORK_DIR, label="CBP")
        cbp_defaults = (cbp_result.human_dir / "class_defaults.py").read_text(encoding="utf-8")
        self.assertIn("CLASS_DEFAULTS", cbp_defaults)
        self.assertIn("'bReplicateMovement': False", cbp_defaults)
        self.assertIn("COMPONENT_DEFAULTS", cbp_defaults)
        self.assertIn("'CapsuleHalfHeight': 86.0", cbp_defaults)
        self.assertIn("'CharacterMover'", cbp_defaults)

        abp_result = validate_compileback_diff(SAMPLES["ABP"], WORK_DIR, label="ABP")
        abp_defaults = (abp_result.human_dir / "class_defaults.py").read_text(encoding="utf-8")
        self.assertIn("'TargetSkeleton':", abp_defaults)
        self.assertIn("'PreviewSkeletalMesh':", abp_defaults)

    def test_upper_python_passes_checker(self) -> None:
        for label, sample_dir in SAMPLES.items():
            with self.subTest(label=label):
                result = validate_compileback_diff(sample_dir, WORK_DIR, label=label)
                issues = check_upper_dir(result.upper_dir)
                self.assertEqual([], issues)

    def test_upper_and_human_outputs_have_separate_roles(self) -> None:
        result = validate_compileback_diff(SAMPLES["CBP"], WORK_DIR, label="CBP")
        upper_readme = (result.upper_dir / "README.md").read_text(encoding="utf-8")
        human_source = (result.human_dir / "blueprint.py").read_text(encoding="utf-8")
        upper_function = (result.upper_dir / "fn_Get_CurrentMovementMode.py").read_text(encoding="utf-8")

        self.assertIn("lossless compile-back representation", upper_readme)
        self.assertIn("sibling *_human/blueprint.py", upper_readme)
        self.assertIn("@std.function", upper_function)
        self.assertIn("def graph", upper_function)
        self.assertIn("class SandboxCharacterMover(Pawn):", human_source)
        self.assertNotIn("@std.function", human_source)
        self.assertNotIn("def graph", human_source)


if __name__ == "__main__":
    unittest.main()
