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
    assert f"{prefix}.menu.pnum.ANY=CH32V003F4U6 (fixed on UIAPduino V1.4)" in BOARDS

    platform = (REPO / "platform.txt").read_text(encoding="utf-8")
    pattern = re.search(r"^tools\.ch32rv_hid\.upload\.pattern=(.*)$", platform, re.M)
    assert pattern
    assert "boot hid flash" in pattern.group(1)
    assert "{build.project_name}.bin" in pattern.group(1)
    # The bootloader is picked by the IDE port (hid://<topology>, ch32rv discovery), not by VID:PID.
    assert '--probe "port:{upload.port.address}"' in pattern.group(1)
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
    """The silkscreen numbers 0..17 of the released UIAPduino core, as the board's lookup table: number n is
    the n-th pad listed (ADR-0010; Arduino.h CH32RV_PIN_RESOLVE). Pad names keep their encoded values."""
    expected = [
        "PA1", "PA2",
        "PC0", "PC1", "PC2", "PC3", "PC4", "PC5", "PC6", "PC7",
        "PD0", "PD1", "PD2", "PD3", "PD4", "PD5", "PD6", "PD7",
    ]
    m = re.search(r"^#define CH32RV_BOARD_PINS \{(.*?)\}", PINS, re.M | re.S)
    assert m, "the board's number -> pad table is missing"
    assert re.findall(r"P[A-F]\d+", m.group(1)) == expected
    assert "#define CH32RV_BOARD_PIN_COUNT 18" in PINS
    assert "#define NUM_DIGITAL_PINS CH32RV_BOARD_PIN_COUNT" in PINS
    # Pad names are not renumbered on the board any more.
    assert not re.search(r"^#define\s+P[A-F]\d+\s+\d+\s*$", PINS, re.M)


def test_official_sdio_disconnect_extension_is_source_compatible():
    assert "#define PD_1 ((PinName)0x31u)" in PINS
    assert "static inline void pinV32_DisconnectDebug(PinName pin)" in PINS
    assert "(*pcfr1 & ~0x07000000u) | 0x04000000u" in PINS
