import collections
import importlib.util
import json
import math
import struct
import subprocess
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LAYOUT = ROOT / "hardware" / "button-layout.json"
SVG = ROOT / "hardware" / "mini-studio-16-button-layout.svg"
STL = ROOT / "hardware" / "stl" / "mini-studio-16-bench-cradle.stl"
CAP_BASE = ROOT / "hardware" / "audio-cap" / "stl" / "audio-cap-base.stl"
CAP_LID = ROOT / "hardware" / "audio-cap" / "stl" / "audio-cap-lid.stl"
CAP_GAUGE = ROOT / "hardware" / "audio-cap" / "stl" / "audio-cap-14pin-fit-gauge.stl"
CAP_STLS = {
    CAP_BASE: ((-42.0, 42.0), (-19.0, 19.0), (0.0, 20.0)),
    CAP_LID: ((-42.0, 42.0), (-19.0, 19.0), (0.0, 8.2)),
    CAP_GAUGE: ((-12.0, 12.0), (-5.0, 5.0), (0.0, 2.0)),
}


def _stl_vertices(path):
    data = path.read_bytes()
    count = struct.unpack_from("<I", data, 80)[0]
    vertices = set()
    offset = 84
    for _ in range(count):
        values = struct.unpack_from("<12fH", data, offset)
        for i in range(3):
            vertices.add(tuple(round(v, 4) for v in values[3 + i * 3:6 + i * 3]))
        offset += 50
    return vertices


class HardwareAssetTests(unittest.TestCase):
    def test_button_map_is_complete_and_unique(self):
        layout = json.loads(LAYOUT.read_text(encoding="utf-8"))
        self.assertEqual(layout["schema"], 1)
        self.assertEqual([len(row) for row in layout["rows"]], [14, 14, 14, 14])
        keys = [key for row in layout["rows"] for key in row]
        ids = [key["id"] for key in keys]
        self.assertEqual(len(ids), 56)
        self.assertEqual(len(ids), len(set(ids)))
        for name, context in layout["contexts"].items():
            self.assertTrue(context["title"], name)
            self.assertTrue(context["summary"], name)
            self.assertTrue(set(context["keys"]).issubset(ids), name)

    def test_svg_contains_every_declared_key(self):
        layout = json.loads(LAYOUT.read_text(encoding="utf-8"))
        root = ET.parse(SVG).getroot()
        groups = {
            element.attrib["data-key"]
            for element in root.iter()
            if "data-key" in element.attrib
        }
        declared = {key["id"] for row in layout["rows"] for key in row}
        self.assertEqual(groups, declared)
        self.assertEqual(root.attrib["viewBox"], "0 0 1280 610")

    def test_committed_assets_are_reproducible(self):
        subprocess.run(
            [sys.executable, str(ROOT / "tools" / "generate_hardware_assets.py"), "--check"],
            cwd=ROOT,
            check=True,
        )

    def test_cradle_stl_is_watertight_and_bounded(self):
        data = STL.read_bytes()
        count = struct.unpack_from("<I", data, 80)[0]
        self.assertEqual(len(data), 84 + count * 50)
        self.assertGreater(count, 100)
        triangles = []
        offset = 84
        for _ in range(count):
            values = struct.unpack_from("<12fH", data, offset)
            vertices = [tuple(values[3 + i * 3:6 + i * 3]) for i in range(3)]
            self.assertTrue(all(math.isfinite(value) for vertex in vertices for value in vertex))
            self.assertEqual(len(set(vertices)), 3)
            triangles.append(vertices)
            offset += 50

        flat = [vertex for triangle in triangles for vertex in triangle]
        for axis, expected in enumerate(((-44.0, 44.0), (-29.0, 29.0), (0.0, 6.0))):
            actual = (min(v[axis] for v in flat), max(v[axis] for v in flat))
            self.assertEqual(actual, expected)

        edges = collections.Counter()
        for triangle in triangles:
            for index in range(3):
                edge = tuple(sorted((triangle[index], triangle[(index + 1) % 3])))
                edges[edge] += 1
        self.assertEqual(set(edges.values()), {2})

    def test_audio_cap_stls_are_two_part_watertight_models_plus_gauge(self):
        design = json.loads((ROOT / "hardware" / "audio-cap" / "design.json").read_text())
        self.assertEqual(design["acceptance"]["enclosure_parts"], 2)
        self.assertEqual(len(design["enclosure"]["parts"]), 2)
        for path, expected_bounds in CAP_STLS.items():
            data = path.read_bytes()
            count = struct.unpack_from("<I", data, 80)[0]
            self.assertEqual(len(data), 84 + count * 50, path.name)
            self.assertGreater(count, 50, path.name)
            triangles = []
            offset = 84
            for _ in range(count):
                values = struct.unpack_from("<12fH", data, offset)
                vertices = [tuple(values[3 + i * 3:6 + i * 3]) for i in range(3)]
                self.assertEqual(len(set(vertices)), 3, path.name)
                triangles.append(vertices)
                offset += 50
            flat = [vertex for triangle in triangles for vertex in triangle]
            for axis, expected in enumerate(expected_bounds):
                # Round away float32 storage error (e.g. 8.2 -> 8.19999980...).
                actual = (round(min(v[axis] for v in flat), 4),
                          round(max(v[axis] for v in flat), 4))
                self.assertEqual(actual, expected, path.name)
            edges = collections.Counter()
            for triangle in triangles:
                for index in range(3):
                    edge = tuple(sorted((triangle[index], triangle[(index + 1) % 3])))
                    edges[edge] += 1
            self.assertEqual(set(edges.values()), {2}, path.name)

    def test_cap_internal_bays_fit_the_specified_modules(self):
        # Outer bounds alone once hid a bay that was 0.6 mm shorter than the
        # ATOM it must hold; assert interior spans against design.json.
        gen = self._generator()
        design = json.loads((ROOT / "hardware" / "audio-cap" / "design.json").read_text())
        modules = {module["id"]: module for module in design["modules"]}
        atom = modules["controller"]["size_mm"]
        adc = modules["line_adc"]["size_mm_max"]
        self.assertGreaterEqual(gen.CAP_ATOM_BAY_SPAN_MM, atom[0] + 0.4)
        self.assertGreaterEqual(gen.CAP_ADC_BAY_SPAN_MM, adc[0] + 0.4)
        # ATOM locating brackets leave clearance on both sides of the module.
        self.assertGreater(gen.CAP_ATOM_BRACKET_Y0 * 2, atom[1])
        self.assertLess(gen.CAP_ATOM_BRACKET_Y1, gen.CAP_WALL_INNER_Y)
        # Lip and fingers stay clear of the widest module.
        self.assertGreater(gen.CAP_FINGER_Y0 * 2, adc[1])

    def test_cap_lid_mechanism_exists_in_committed_meshes(self):
        gen = self._generator()
        lid = _stl_vertices(CAP_LID)
        base = _stl_vertices(CAP_BASE)
        # Locating lip materialized: end-lip band vertices below the plate.
        self.assertTrue(any(abs(v[0]) == gen.CAP_LID_LIP_X0
                            and v[2] <= gen.CAP_LID_LIP_TOP for v in lid))
        self.assertTrue(any(abs(v[1]) == gen.CAP_FINGER_Y0
                            and v[2] <= gen.CAP_LID_LIP_TOP for v in lid))
        # Snap nubs materialized: each step's exact protrusion exists, and the
        # widest step engages past the base's inner wall face.
        protrusions = {abs(v[1]) for v in lid if v[2] > gen.CAP_LID_LIP_TOP}
        widest = max(step[2] for step in gen.CAP_NUB_STEPS)
        for _, _, y_out in gen.CAP_NUB_STEPS:
            self.assertIn(y_out, protrusions)
        self.assertGreaterEqual(widest - gen.CAP_WALL_INNER_Y, 0.25)
        self.assertEqual(max(protrusions), widest)
        # Catch windows materialized in the base side walls at the assembled
        # nub position (window corner vertices exist on the wall faces).
        for z_edge in (gen.CAP_WINDOW_Z0, gen.CAP_WINDOW_Z1):
            self.assertTrue(any(abs(v[0]) == gen.CAP_WINDOW_X0
                                and abs(v[1]) >= gen.CAP_WALL_INNER_Y
                                and v[2] == z_edge for v in base))
        # Seated engagement lands inside the window: assembled nub z is
        # (base height + plate) - lid z for each step's span.
        assembled_top = 20.0 + gen.CAP_LID_PLATE_MM
        for z0, z1, y_out in gen.CAP_NUB_STEPS:
            if y_out <= gen.CAP_WALL_INNER_Y:
                continue  # lead-in steps may ride inside the wall clearance
            self.assertGreaterEqual(assembled_top - z1, gen.CAP_WINDOW_Z0)
            self.assertLessEqual(assembled_top - z0, gen.CAP_WINDOW_Z1)

    def test_gauge_pin_one_corner_is_notched(self):
        vertices = {(v[0], v[1]) for v in _stl_vertices(CAP_GAUGE)}
        self.assertNotIn((-12.0, -5.0), vertices)  # pin-1 corner cut away
        for corner in ((-12.0, 5.0), (12.0, -5.0), (12.0, 5.0)):
            self.assertIn(corner, vertices)

    @staticmethod
    def _generator():
        spec = importlib.util.spec_from_file_location(
            "generate_hardware_assets", ROOT / "tools" / "generate_hardware_assets.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module


if __name__ == "__main__":
    unittest.main()
