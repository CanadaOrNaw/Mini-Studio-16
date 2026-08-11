import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CAP = ROOT / "hardware" / "audio-cap"


class AudioCapRequirementTests(unittest.TestCase):
    def setUp(self):
        self.design = json.loads((CAP / "design.json").read_text())
        self.bom = json.loads((CAP / "bom.json").read_text())

    def test_eight_non_negotiable_constraints(self):
        acceptance = self.design["acceptance"]
        self.assertEqual(acceptance["external_wired_connections"],
                         ["Cardputer-ADV EXT 2.54-14P"])
        self.assertIn("pin 6 5VOUT", acceptance["power_source"])
        self.assertFalse(acceptance["external_power_connector"])
        self.assertFalse(acceptance["custom_pcb"])
        self.assertFalse(acceptance["soldering_required"])
        self.assertEqual(acceptance["module_strategy"],
                         "off-the-shelf preassembled modules")
        self.assertEqual(acceptance["assembly_connections"],
                         "precrimped/plug-in only")
        self.assertEqual(acceptance["enclosure_parts"], 2)
        self.assertEqual(acceptance["closure"], "compliant snap tabs")

    def test_pin_map_protects_cardputer_buses_and_5vin(self):
        used = {entry["pin"] for entry in self.design["cardputer_pin_map"]}
        self.assertIn(6, used)
        self.assertIn(4, used)
        self.assertNotIn(2, used)
        self.assertTrue({7, 8, 9, 10, 11}.isdisjoint(used))
        self.assertTrue({2, 7, 8, 9, 10, 11}.issubset(
            set(self.design["unused_cardputer_pins"])))

    def test_every_item_has_all_regions_and_https_sources(self):
        regions = set(self.bom["regions"])
        self.assertEqual(regions, {"Canada", "United States", "European Union"})
        for item in self.bom["items"]:
            self.assertEqual(set(item["sources"]), regions, item["id"])
            for sources in item["sources"].values():
                self.assertTrue(sources, item["id"])
                self.assertTrue(all(source["url"].startswith("https://")
                                    for source in sources), item["id"])

    def test_no_board_fabrication_artifacts(self):
        banned_suffixes = {".kicad_pcb", ".kicad_sch", ".gbr", ".drl", ".pos"}
        for path in CAP.rglob("*"):
            self.assertNotIn(path.suffix.lower(), banned_suffixes, str(path))
            self.assertNotIn("gerber", path.name.lower(), str(path))
            self.assertNotIn("pcb", path.name.lower(), str(path))


if __name__ == "__main__":
    unittest.main()
