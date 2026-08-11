import math
import struct
import unittest
from pathlib import Path


class FactoryProjectTests(unittest.TestCase):
    def test_factory_v1_matches_migration_validator(self):
        project = (Path(__file__).parent.parent /
                   "factory-sd/groovebox/projects/P1.gbx").read_bytes()
        self.assertEqual(len(project), 1807)
        offset = 0
        magic, version, bpm, loop_start = struct.unpack_from("<IHHB", project, offset)
        offset += 9
        self.assertEqual((magic, version, bpm), (0x31584247, 1, 128))
        self.assertLess(loop_start, 64)
        song = struct.unpack_from("<64B", project, offset)
        offset += 64
        self.assertTrue(all(item == 0xFF or item < 8 for item in song))

        for _ in range(3):
            oscillator, _wavetable, *values = struct.unpack_from(
                "<BB6f", project, offset)
            offset += 26
            self.assertLess(oscillator, 5)
            self.assertTrue(all(math.isfinite(value) for value in values))
            cutoff, resonance, envelope, amp_decay, filter_decay, volume = values
            self.assertTrue(0.0 <= cutoff <= 1.0)
            self.assertTrue(0.0 <= resonance <= 1.0)
            self.assertTrue(0.0 <= envelope <= 1.0)
            self.assertTrue(0.9990 <= amp_decay <= 0.99999)
            self.assertTrue(0.9950 <= filter_decay <= 0.99995)
            self.assertTrue(0.0 <= volume <= 1.0)

        for _ in range(8):
            engine, drum_type, choke, *values = struct.unpack_from(
                "<BBB3f", project, offset)
            offset += 15
            sample_name = project[offset:offset + 32]
            offset += 32
            self.assertLess(engine, 3)
            self.assertLess(drum_type, 4)
            self.assertLess(choke, 4)
            self.assertEqual(sample_name[-1], 0)
            self.assertTrue(all(math.isfinite(value) for value in values))
            volume, tune, decay = values
            self.assertTrue(0.0 <= volume <= 1.0)
            self.assertTrue(-12.0 <= tune <= 12.0)
            self.assertTrue(0.4 <= decay <= 2.5)

        for _ in range(8 * 3 * 16):
            note, octave, flags = struct.unpack_from("<BBB", project, offset)
            offset += 3
            self.assertEqual(flags & ~3, 0)
            self.assertTrue(note == 0 or (note <= 12 and 1 <= octave <= 7))

        offset += 8 * 16  # drum trigger masks
        self.assertEqual(offset, len(project))


if __name__ == "__main__":
    unittest.main()
