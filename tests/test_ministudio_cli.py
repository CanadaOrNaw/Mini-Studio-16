import importlib.util
import pathlib
import sys
import unittest
from types import SimpleNamespace


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

    def test_json_response(self):
        response = cli.parse_response("MS16/1 x OK bpm=120")
        self.assertEqual(
            cli.response_json(response),
            '{"message":"","ok":true,"protocol":"MS16/1","request_id":"x","values":{"bpm":"120"}}',
        )

    def test_port_resolution(self):
        ports = [SimpleNamespace(device="/dev/ttyACM0")]
        self.assertEqual(cli.resolve_port(None, ports), "/dev/ttyACM0")
        self.assertEqual(cli.resolve_port("COM9", []), "COM9")
        with self.assertRaises(ValueError):
            cli.resolve_port(None, [])
        with self.assertRaises(ValueError):
            cli.resolve_port(None, ports + [SimpleNamespace(device="/dev/ttyUSB0")])

    def test_loop_commands(self):
        args = cli.parser().parse_args(["loop-status"])
        self.assertEqual(cli.command_words(args), ["loop", "status"])
        args = cli.parser().parse_args(["loop", "6", "record"])
        self.assertEqual(cli.command_words(args), ["loop", 6, "record"])

    def test_sample_commands(self):
        args = cli.parser().parse_args(["sample-status"])
        self.assertEqual(cli.command_words(args), ["sample", "status"])
        args = cli.parser().parse_args(["sample-assign", "16", "CHORD.wav", "melodic"])
        self.assertEqual(cli.command_words(args),
                         ["sample", 16, "assign", "CHORD.wav", "melodic"])
        args = cli.parser().parse_args(["sample-trigger", "2", "16"])
        self.assertEqual(cli.command_words(args), ["sample", 2, "trigger", 16])

    def test_event_commands(self):
        args = cli.parser().parse_args(["event-status"])
        self.assertEqual(cli.command_words(args), ["event", "status"])
        args = cli.parser().parse_args(["event", "5", "bars", "128"])
        self.assertEqual(cli.command_words(args), ["event", 5, "bars", 128])
        args = cli.parser().parse_args(["event", "3", "arm"])
        self.assertEqual(cli.command_words(args), ["event", 3, "arm"])

    def test_motion_commands(self):
        args = cli.parser().parse_args(["motion-status"])
        self.assertEqual(cli.command_words(args), ["motion", "status"])
        args = cli.parser().parse_args(
            ["motion-map", "4", "shake", "synth3_resonance"]
        )
        self.assertEqual(cli.command_words(args),
                         ["motion", 4, "map", "shake", "synth3_resonance"])


if __name__ == "__main__":
    unittest.main()
