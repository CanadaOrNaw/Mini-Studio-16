import unittest

from tools.check_firmware_size import parse_size_output


class FirmwareSizeTests(unittest.TestCase):
    def test_parse_gnu_size(self):
        result = parse_size_output(
            "text data bss dec hex filename\n100000 2048 32000 134048 20ba0 firmware.elf\n"
        )
        self.assertEqual(result["static_ram"], 34048)
        self.assertEqual(result["flash_estimate"], 102048)

    def test_reject_bad_total(self):
        with self.assertRaises(ValueError):
            parse_size_output("1 2 3 7 0 bad.elf")


if __name__ == "__main__":
    unittest.main()
