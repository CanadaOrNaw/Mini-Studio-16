import importlib.util
import pathlib
import sys
import unittest


TOOLS = pathlib.Path(__file__).parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location("hardware_smoke", TOOLS / "hardware_smoke.py")
assert SPEC and SPEC.loader
smoke = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = smoke
SPEC.loader.exec_module(smoke)


class FakeSerial:
    def __init__(self, sd_state="PASS"):
        self.responses = []
        self.sd_state = sd_state
        self.commands = []

    def write(self, payload):
        words = payload.decode("ascii").strip().split()
        request_id = words[1]
        command = words[2:]
        self.commands.append(command)
        self.responses.append(b"BOOT_ASYNC retained\n")
        if command == ["sd_test"]:
            self.responses.append(
                f"MS16/1 {request_id} OK sd_test=started\n".encode()
            )
            self.responses.append(
                (f"SDDIAG state={self.sd_state} write=900KB/s read=1400KB/s "
                 "rr6=1100KB/s maxWrite=12000us maxRead=9000us "
                 "minHeap=80000 errors=0\n").encode()
            )
        else:
            self.responses.append(
                f"MS16/1 {request_id} OK probe=1\n".encode()
            )

    def flush(self):
        pass

    def readline(self):
        return self.responses.pop(0) if self.responses else b""


class HardwareSmokeTests(unittest.TestCase):
    def test_read_only_pass(self):
        device = FakeSerial()
        report = smoke.run_smoke(device, command_timeout=0.1)
        self.assertEqual(report["result"], "PASS")
        self.assertEqual(len(report["probes"]), len(smoke.READ_ONLY_PROBES))
        self.assertIsNone(report["sd_diagnostic"])
        self.assertEqual(len(report["asynchronous_lines"]), len(smoke.READ_ONLY_PROBES))

    def test_sd_pass_is_parsed(self):
        report = smoke.run_smoke(FakeSerial(), 0.1, True, 0.1)
        self.assertEqual(report["sd_diagnostic"]["state"], "PASS")
        self.assertEqual(report["sd_diagnostic"]["errors"], "0")

    def test_sd_failure_is_fatal(self):
        with self.assertRaises(RuntimeError):
            smoke.run_smoke(FakeSerial("FAIL"), 0.1, True, 0.1)


if __name__ == "__main__":
    unittest.main()
