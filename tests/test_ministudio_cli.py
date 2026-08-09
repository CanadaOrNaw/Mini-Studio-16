import importlib.util
import pathlib
import sys
import unittest


CLI_PATH = pathlib.Path(__file__).parents[1] / "tools" / "ministudio_cli.py"
SPEC = importlib.util.spec_from_file_location("ministudio_cli", CLI_PATH)
assert SPEC and SPEC.loader
cli = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = cli
SPEC.loader.exec_module(cli)


class MiniStudioCliTests(unittest.TestCase):
    def test_build_request(self):
        self.assertEqual(
            cli.build_request("req_1", ["transport", "start"]),
            b"MS16/1 req_1 transport start\n",
        )

    def test_bad_request_id(self):
        with self.assertRaises(ValueError):
            cli.build_request("bad/id", ["ping"])

    def test_parse_ok(self):
        response = cli.parse_response(
            "MS16/1 9 OK playing=1 bpm=128 master=recording path=/groovebox/recordings/MASTER001.wav"
        )
        self.assertTrue(response.ok)
        self.assertEqual(response.request_id, "9")
        self.assertEqual(response.values["bpm"], "128")
        self.assertEqual(response.values["master"], "recording")

    def test_parse_error(self):
        response = cli.parse_response("MS16/1 10 ERR master_not_recording")
        self.assertFalse(response.ok)
        self.assertEqual(response.message, "master_not_recording")

    def test_reject_unframed_line(self):
        with self.assertRaises(ValueError):
            cli.parse_response("SDDIAG state=PASS")


if __name__ == "__main__":
    unittest.main()
