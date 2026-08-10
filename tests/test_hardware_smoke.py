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
            command_name = " ".join(command)
            values = {
                "ping": "pong=1 firmware=v3-alpha",
                "status": "playing=0 bpm=128 heapFree=90000 heapLargest=70000 battery=75 project=1",
                "project status": "project=1 occupied=0x01",
                "loop status": "available=1 timeline=0 errors=0",
                "sample status": "available=1 quota=0 remaining=882000 errors=0",
                "event status": "position=0 count=0 capacity=2048",
                "motion status": "available=1 samples=20 gestures=0",
                "midi status": "usbAvailable=1 bleAvailable=1 queueDrops=0 clockDrops=0",
            }[command_name]
            self.responses.append(
                f"MS16/1 {request_id} OK {values}\n".encode()
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

    def test_missing_telemetry_is_fatal(self):
        response = smoke.Response("x", True, {"available": "1"}, "")
        with self.assertRaises(RuntimeError):
            smoke.validate_probe("loop status", response)


if __name__ == "__main__":
    unittest.main()
