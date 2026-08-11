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
CAP_STLS = {
    ROOT / "hardware" / "audio-cap" / "stl" / "audio-cap-base.stl":
        ((-42.0, 42.0), (-19.0, 19.0), (0.0, 20.0)),
    ROOT / "hardware" / "audio-cap" / "stl" / "audio-cap-lid.stl":
        ((-42.0, 42.0), (-19.0, 19.0), (0.0, 4.0)),
    ROOT / "hardware" / "audio-cap" / "stl" / "audio-cap-14pin-fit-gauge.stl":
        ((-12.0, 12.0), (-5.0, 5.0), (0.0, 2.0)),
}


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
                actual = (min(v[axis] for v in flat), max(v[axis] for v in flat))
                self.assertEqual(actual, expected, path.name)
            edges = collections.Counter()
            for triangle in triangles:
                for index in range(3):
                    edge = tuple(sorted((triangle[index], triangle[(index + 1) % 3])))
                    edges[edge] += 1
            self.assertEqual(set(edges.values()), {2}, path.name)


if __name__ == "__main__":
    unittest.main()
