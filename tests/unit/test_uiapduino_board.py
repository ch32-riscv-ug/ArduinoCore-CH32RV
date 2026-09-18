"""The UIAPduino product board keeps its PCB and upload facts intact."""

import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parents[2]
BOARDS = (REPO / "boards.txt").read_text(encoding="utf-8")
PINS = (REPO / "variants/UIAPduino_Pro_Micro_CH32V003_V14/pins_arduino.h").read_text(
    encoding="utf-8"
)


def test_named_board_uses_hid_binary_upload():
    prefix = "UIAPDUINO_V003_V14"
    assert f"{prefix}.name=UIAPduino Pro Micro CH32V003 V1.4" in BOARDS
    assert f"{prefix}.upload.protocol=hid" in BOARDS
    assert f"{prefix}.upload.tool=ch32rv_hid" in BOARDS
    assert f"{prefix}.build.variant=UIAPduino_Pro_Micro_CH32V003_V14" in BOARDS

    platform = (REPO / "platform.txt").read_text(encoding="utf-8")
    pattern = re.search(r"^tools\.ch32rv_hid\.upload\.pattern=(.*)$", platform, re.M)
    assert pattern
    assert "boot hid flash" in pattern.group(1)
    assert "{build.project_name}.bin" in pattern.group(1)
    assert "--usb-id 1209:b803" in pattern.group(1)
    assert ".elf" not in pattern.group(1)


def test_v14_silkscreen_aliases_and_reserved_pins():
    expected = {
        "D0": "PA1", "D1": "PA2", "D2": "PC0", "D3": "PC1",
        "D4": "PC2", "D5": "PC3", "D6": "PC4", "D7": "PC5",
        "D8": "PC6", "D9": "PC7", "D10": "PD0", "D11": "PD1",
        "D12": "PD2", "D13": "PD3", "D14": "PD4", "D15": "PD5",
        "D16": "PD6", "D17": "PD7",
    }
    actual = dict(re.findall(r"^#define\s+(D\d+)\s+(P[A-F]\d+)$", PINS, re.M))
    assert actual == expected
    assert "#define PIN_UIAP_SWIO  PD1" in PINS
    assert "#define PIN_UIAP_RESET PD7" in PINS
    assert "#define PIN_USB_DP     PD3" in PINS
    assert "#define PIN_USB_DM     PD4" in PINS


def test_v14_keeps_official_numeric_pin_contract():
    expected = {
        "PA1": 0, "PA2": 1,
        "PC0": 2, "PC1": 3, "PC2": 4, "PC3": 5, "PC4": 6,
        "PC5": 7, "PC6": 8, "PC7": 9,
        "PD0": 10, "PD1": 11, "PD2": 12, "PD3": 13,
        "PD4": 14, "PD5": 15, "PD6": 16, "PD7": 17,
    }
    actual = {
        name: int(value)
        for name, value in re.findall(
            r"^#define\s+(P[ACD]\d+)\s+(\d+)$", PINS, re.M
        )
    }
    assert actual == expected
    assert "#define CH32_UIAP_ENCODE_PIN(pin)" in PINS
    assert "#define NUM_DIGITAL_PINS 18" in PINS


def test_official_sdio_disconnect_extension_is_source_compatible():
    assert "#define PD_1 ((PinName)0x31u)" in PINS
    assert "static inline void pinV32_DisconnectDebug(PinName pin)" in PINS
    assert "(*pcfr1 & ~0x07000000u) | 0x04000000u" in PINS
