import json
from pathlib import Path
import tempfile
import unittest

from tools.merge_dual_firmware import (
    FLASH_SIZE,
    build_command,
    load_layout,
    verify_merged_image,
    write_report,
)


VALID_CSV = """\
# Name, Type, SubType, Offset, Size, Flags
nvs,data,nvs,0x9000,0x5000,
otadata,data,ota,0xe000,0x2000,
normal,app,ota_0,0x10000,0x2f0000,
usbhost,app,ota_1,0x300000,0x2f0000,
"""


class MergeDualFirmwareTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.normal = self.root / "normal"
        self.host = self.root / "host"
        self.normal.mkdir()
        self.host.mkdir()
        for directory, firmware in ((self.normal, b"normal-app"),
                                    (self.host, b"host-app")):
            (directory / "bootloader.bin").write_bytes(b"same-bootloader")
            (directory / "partitions.bin").write_bytes(b"same-partitions")
            (directory / "firmware.bin").write_bytes(firmware)
        self.boot_app0 = self.root / "boot_app0.bin"
        self.boot_app0.write_bytes(b"ota-initial-state")
        self.csv = self.root / "partitions.csv"
        self.csv.write_text(VALID_CSV, encoding="utf-8")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_layout_and_merge_offsets(self) -> None:
        layout = load_layout(self.csv)
        self.assertEqual(layout.normal.offset, 0x10000)
        self.assertEqual(layout.usbhost.offset, 0x300000)
        self.assertEqual(layout.normal.size, 0x2F0000)
        output = self.root / "dual.bin"
        command = build_command(self.normal, self.host, output,
                                self.boot_app0, self.csv)
        self.assertIn("esp32s3", command)
        self.assertEqual(command[command.index("0x10000") + 1],
                         str(self.normal / "firmware.bin"))
        self.assertEqual(command[command.index("0x300000") + 1],
                         str(self.host / "firmware.bin"))
        self.assertEqual(command[command.index("0xe000") + 1], str(self.boot_app0))

    def test_report_is_deterministic_and_bounded(self) -> None:
        output = self.root / "dual.bin"
        merged = bytearray(b"\xff" * (0x300000 + len(b"host-app")))
        merged[0x10000:0x10000 + len(b"normal-app")] = b"normal-app"
        merged[0x300000:0x300000 + len(b"host-app")] = b"host-app"
        output.write_bytes(merged)
        report = self.root / "layout.json"
        write_report(report, self.normal, self.host, self.csv, output)
        parsed = json.loads(report.read_text(encoding="utf-8"))
        self.assertEqual(parsed["flash_size"], FLASH_SIZE)
        self.assertEqual(parsed["normal"]["binary_size"], len(b"normal-app"))
        self.assertEqual(parsed["usbhost"]["binary_size"], len(b"host-app"))
        self.assertEqual(len(parsed["merged_sha256"]), 64)

    def test_verifier_rejects_wrong_role_placement(self) -> None:
        output = self.root / "dual.bin"
        output.write_bytes(b"\xff" * (0x300000 + len(b"host-app")))
        with self.assertRaisesRegex(ValueError, "normal firmware"):
            verify_merged_image(output, self.normal, self.host, self.csv)

    def test_rejects_common_binary_mismatch(self) -> None:
        (self.host / "partitions.bin").write_bytes(b"wrong")
        with self.assertRaisesRegex(ValueError, "partition tables differ"):
            build_command(self.normal, self.host, self.root / "dual.bin",
                          self.boot_app0, self.csv)

    def test_rejects_oversize_image(self) -> None:
        (self.host / "firmware.bin").write_bytes(b"x" * (0x2F0000 + 1))
        with self.assertRaisesRegex(ValueError, "exceeds its OTA slot"):
            build_command(self.normal, self.host, self.root / "dual.bin",
                          self.boot_app0, self.csv)

    def test_rejects_missing_wrong_and_overlapping_slots(self) -> None:
        cases = (
            (VALID_CSV.replace("usbhost,app,ota_1", "host,app,ota_1"),
             "missing partition"),
            (VALID_CSV.replace("usbhost,app,ota_1", "usbhost,app,ota_2"),
             "usbhost must be app/ota_1"),
            (VALID_CSV.replace("0x300000,0x2f0000", "0x300001,0x2f0000"),
             "not 64 KiB aligned"),
            (VALID_CSV.replace("0x300000,0x2f0000", "0x200000,0x2f0000"),
             "overlap"),
            (VALID_CSV.replace("usbhost,app,ota_1,0x300000,0x2f0000",
                               "usbhost,app,ota_1,0x300000,0x600000"),
             "exceeds 8 MB flash"),
        )
        for index, (contents, message) in enumerate(cases):
            with self.subTest(index=index):
                path = self.root / f"bad-{index}.csv"
                path.write_text(contents, encoding="utf-8")
                with self.assertRaisesRegex(ValueError, message):
                    load_layout(path)


if __name__ == "__main__":
    unittest.main()
