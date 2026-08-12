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
        args = cli.parser().parse_args(["loop", "2", "volume", "65"])
        self.assertEqual(cli.command_words(args), ["loop", 2, "volume", 65])
        args = cli.parser().parse_args(["loop", "3", "solo"])
        self.assertEqual(cli.command_words(args), ["loop", 3, "solo"])
        args = cli.parser().parse_args(["loop-transport", "metronome_on"])
        self.assertEqual(cli.command_words(args), ["loop", "metronome_on"])

    def test_sample_commands(self):
        args = cli.parser().parse_args(["sample-status"])
        self.assertEqual(cli.command_words(args), ["sample", "status"])
        args = cli.parser().parse_args(["sample-assign", "16", "CHORD.wav", "melodic"])
        self.assertEqual(cli.command_words(args),
                         ["sample", 16, "assign", "CHORD.wav", "melodic"])
        args = cli.parser().parse_args(["sample-trigger", "2", "16"])
        self.assertEqual(cli.command_words(args), ["sample", 2, "trigger", 16])
        args = cli.parser().parse_args(["sample-record", "4", "bus", "sliced"])
        self.assertEqual(cli.command_words(args),
                         ["sample", 4, "record", "bus", "sliced"])
        args = cli.parser().parse_args(["sample-stop", "4"])
        self.assertEqual(cli.command_words(args), ["sample", 4, "stop"])

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
        args = cli.parser().parse_args(["midi-status"])
        self.assertEqual(cli.command_words(args), ["midi", "status"])

    def test_project_commands(self):
        args = cli.parser().parse_args(["project-status"])
        self.assertEqual(cli.command_words(args), ["project", "status"])
        args = cli.parser().parse_args(["project", "8", "save"])
        self.assertEqual(cli.command_words(args), ["project", 8, "save"])
        args = cli.parser().parse_args(["project", "1", "load"])
        self.assertEqual(cli.command_words(args), ["project", 1, "load"])

    def test_synth_commands(self):
        args = cli.parser().parse_args(["note-off", "2", "64"])
        self.assertEqual(cli.command_words(args), ["note_off", 2, 64])
        args = cli.parser().parse_args(["synth-status"])
        self.assertEqual(cli.command_words(args), ["synth", "status"])
        args = cli.parser().parse_args(["synth-engine", "3", "fm4"])
        self.assertEqual(cli.command_words(args), ["synth", 3, "engine", "fm4"])
        args = cli.parser().parse_args(
            ["synth-set", "1", "fm.op2.ratio", "675"]
        )
        self.assertEqual(cli.command_words(args),
                         ["synth", 1, "set", "fm.op2.ratio", 675])
        args = cli.parser().parse_args(["synth-dsp-reset"])
        self.assertEqual(cli.command_words(args), ["synth", "dsp_reset"])

    def test_boot_commands(self):
        args = cli.parser().parse_args(["boot-status"])
        self.assertEqual(cli.command_words(args), ["boot", "status"])
        args = cli.parser().parse_args(["boot-mode", "host"])
        self.assertEqual(cli.command_words(args), ["boot", "host"])
        args = cli.parser().parse_args(["boot-mode", "normal"])
        self.assertEqual(cli.command_words(args), ["boot", "normal"])

    def test_three_in_one_commands(self):
        args = cli.parser().parse_args(["chord-play", "7", "8"])
        self.assertEqual(cli.command_words(args), ["chord", "play", 7, 8])
        args = cli.parser().parse_args(["chord-lock", "2", "27"])
        self.assertEqual(cli.command_words(args), ["chord", "lock", 2, 27])
        args = cli.parser().parse_args(["po-lock", "16", "1", "14"])
        self.assertEqual(cli.command_words(args), ["po", "lock", 16, 1, 14])
        args = cli.parser().parse_args(["medo-quantize", "3", "2"])
        self.assertEqual(cli.command_words(args), ["medo", "quantize", 3, 2])
        args = cli.parser().parse_args(["medo-set", "bars", "128"])
        self.assertEqual(cli.command_words(args), ["medo", "set", "bars", 128])
        args = cli.parser().parse_args(["medo-set", "arp_enabled", "1"])
        self.assertEqual(cli.command_words(args), ["medo", "set", "arp_enabled", 1])

    def test_audio_cap_commands(self):
        args = cli.parser().parse_args(["cap-status"])
        self.assertEqual(cli.command_words(args), ["cap", "status"])
        args = cli.parser().parse_args(["cap-pair"])
        self.assertEqual(cli.command_words(args), ["cap", "pair"])
        args = cli.parser().parse_args(["cap-monitor", "73"])
        self.assertEqual(cli.command_words(args), ["cap", "monitor", 73])
        args = cli.parser().parse_args(["cap-disconnect"])
        self.assertEqual(cli.command_words(args), ["cap", "disconnect"])


if __name__ == "__main__":
    unittest.main()
