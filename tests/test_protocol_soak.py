import importlib.util
import pathlib
import sys
import unittest


TOOLS = pathlib.Path(__file__).parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location("protocol_soak", TOOLS / "protocol_soak.py")
assert SPEC and SPEC.loader
soak = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = soak
SPEC.loader.exec_module(soak)


class FakeSerial:
    def __init__(self):
        self.responses = []
        self.writes = 0

    def write(self, payload):
        request_id = payload.decode("ascii").split()[1]
        self.responses.extend([b"ASYNC test\n", f"MS16/1 {request_id} OK pong=1\n".encode()])
        self.writes += 1

    def flush(self):
        pass

    def readline(self):
        return self.responses.pop(0) if self.responses else b""


class ProtocolSoakTests(unittest.TestCase):
    def test_correlates_responses_and_counts_async(self):
        device = FakeSerial()
        completed, asynchronous = soak.run_soak(device, 50, 0.1)
        self.assertEqual(completed, 50)
        self.assertEqual(asynchronous, 50)
        self.assertEqual(device.writes, 50)

    def test_command_cycle(self):
        self.assertEqual(soak.command_for(0), ["ping"])
        self.assertEqual(soak.command_for(1), ["status"])
        self.assertEqual(soak.command_for(2), ["tempo", 128])


if __name__ == "__main__":
    unittest.main()
