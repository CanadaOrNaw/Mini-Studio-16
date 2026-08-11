import unittest

from tools.check_firmware_size import parse_size_output


class FirmwareSizeTests(unittest.TestCase):
    def test_parse_esp32_sections(self):
        result = parse_size_output(
            "firmware.elf  :\n"
            "section             size       addr\n"
            ".iram0.vectors       1024  1074266112\n"
            ".iram0.text         12000  1074267136\n"
            ".dram0.data          2048  1073414144\n"
            ".dram0.bss          32000  1073416192\n"
            ".noinit                64  1073448192\n"
            ".flash.text         70000  1107427328\n"
            ".flash.rodata       14928  1061158912\n"
            ".flash_rodata_dummy 999999  0\n"
        )
        self.assertEqual(result["static_ram"], 34112)
        self.assertEqual(result["flash_estimate"], 100000)
        self.assertEqual(result["dram_bss"], 32000)

    def test_reject_berkeley_summary(self):
        with self.assertRaises(ValueError):
            parse_size_output("100 20 30 150 96 firmware.elf")


if __name__ == "__main__":
    unittest.main()
