import importlib.util
import pathlib
import struct
import sys
import tempfile
import unittest
import wave


TOOL = pathlib.Path(__file__).parents[1] / "tools" / "split_stems.py"
SPEC = importlib.util.spec_from_file_location("split_stems", TOOL)
assert SPEC and SPEC.loader
split_stems = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = split_stems
SPEC.loader.exec_module(split_stems)


class SplitStemTests(unittest.TestCase):
    def test_split_known_frames(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            source = root / "take.mss"
            frames = [
                (1, 11, 21, 31, 41),
                (2, 12, 22, 32, 42),
                (3, 13, 23, 33, 43),
            ]
            header = split_stems.HEADER.pack(
                b"MS16STEM", 1, 5, 22050, len(frames), b"M123D" + b"\0" * 7
            )
            payload = b"".join(struct.pack("<5h", *frame) for frame in frames)
            source.write_bytes(header + payload)
            outputs = split_stems.split_stem_file(source, root / "out", chunk_frames=2)
            with wave.open(str(outputs["synth2"]), "rb") as reader:
                self.assertEqual(reader.getframerate(), 22050)
                self.assertEqual(reader.getnframes(), 3)
                values = struct.unpack("<3h", reader.readframes(3))
                self.assertEqual(values, (21, 22, 23))

    def test_reject_truncated_payload(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            source = root / "bad.mss"
            source.write_bytes(
                split_stems.HEADER.pack(b"MS16STEM", 1, 5, 22050, 2, b"M123D" + b"\0" * 7)
                + b"\0" * 10
            )
            with self.assertRaises(ValueError):
                split_stems.split_stem_file(source, root / "out")
            self.assertFalse((root / "out").exists())

    def test_recovered_file_allows_partial_trailing_frame(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            source = root / "recover.mss"
            source.write_bytes(
                split_stems.HEADER.pack(b"MS16STEM", 1, 5, 22050, 1,
                                        b"M123D" + b"\0" * 7)
                + struct.pack("<5h", 1, 2, 3, 4, 5)
                + b"\xaa\xbb"
            )
            outputs = split_stems.split_stem_file(source, root / "out")
            with wave.open(str(outputs["drums"]), "rb") as reader:
                self.assertEqual(struct.unpack("<h", reader.readframes(1)), (5,))


if __name__ == "__main__":
    unittest.main()
