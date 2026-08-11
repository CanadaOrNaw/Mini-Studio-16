from pathlib import Path
import tempfile
import unittest

from tools.merge_firmware import build_command, find_boot_app0


class MergeFirmwareTests(unittest.TestCase):
    def test_discovers_framework_boot_app0_and_uses_s3_offsets(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build"
            build.mkdir()
            for name in ("bootloader.bin", "partitions.bin", "firmware.bin"):
                (build / name).write_bytes(b"test")

            boot_app0 = (
                root
                / "packages"
                / "framework-arduinoespressif32"
                / "tools"
                / "partitions"
                / "boot_app0.bin"
            )
            boot_app0.parent.mkdir(parents=True)
            boot_app0.write_bytes(b"test")

            self.assertEqual(find_boot_app0(root), boot_app0)
            output = root / "merged.bin"
            command = build_command(build, output, boot_app0)
            self.assertIn("esp32s3", command)
            self.assertEqual(command[command.index("0x0000") + 1], str(build / "bootloader.bin"))
            self.assertEqual(command[command.index("0x8000") + 1], str(build / "partitions.bin"))
            self.assertEqual(command[command.index("0xe000") + 1], str(boot_app0))
            self.assertEqual(command[command.index("0x10000") + 1], str(build / "firmware.bin"))

    def test_builds_original_esp32_atom_image_at_0x1000(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build"
            build.mkdir()
            for name in ("bootloader.bin", "partitions.bin", "firmware.bin"):
                (build / name).write_bytes(b"test")
            boot_app0 = root / "boot_app0.bin"
            boot_app0.write_bytes(b"test")

            command = build_command(
                build, root / "atom.bin", boot_app0, chip="esp32",
                flash_size="4MB", flash_freq="40m",
                bootloader_offset="0x1000")
            self.assertIn("esp32", command)
            self.assertNotIn("esp32s3", command)
            self.assertEqual(command[command.index("0x1000") + 1],
                             str(build / "bootloader.bin"))
            self.assertEqual(command[command.index("--flash_size") + 1], "4MB")
            self.assertEqual(command[command.index("--flash_freq") + 1], "40m")


if __name__ == "__main__":
    unittest.main()
