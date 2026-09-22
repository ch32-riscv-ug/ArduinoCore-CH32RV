"""Fixture profiles for the OEP manual runners: which probe port, which DUT board, which console pins,
and how the DUT pads are wired to the probe GPIOs. One place, so a runner takes `--target x035|v003`
instead of carrying its own pin table.

x035: ESP32-P4 fixture (E143 wiring) with CH32X035F8U6. v003: classic ESP32 jig (E132 wiring, remeasured
2026-09-22 with the OEP probe) with UIAPduino Pro Micro CH32V003 V1.4. The classic ESP32 probe has no
fixture.capture (no PARLIO), so `capture` is False there and runners must skip wire decoding.
"""

TARGETS = {
    "x035": {
        "port": "/run/board-identify/by-id/esp32-series-30eda0e31108",
        "fqbn": "ch32-riscv-ug:ch32v:CH32X035:pnum=ANY",
        "serial_index": 4,
        "define": "OEP_TARGET_X035",
        "uart_rx": 12, "uart_tx": 6,                   # DUT USART4 PB0 (TX) -> probe 12, PB1 (RX) <- probe 6
        "capture": True, "capture_max_hz": 20_000_000,   # PARLIO
        "gpio": {"PA0": 46, "PA1": 47, "PA2": 48, "PA3": 49, "PA4": 53, "PA5": 4, "PA6": 11, "PA7": 5,
                 "PB3": 13, "PB11": 9, "PB12": 14, "PC14": 10, "PC15": 15, "PC16": 52, "PC17": 50},
        "adc": {"PA0": 46, "PA1": 47, "PA2": 48, "PA3": 49, "PA4": 53, "PA5": 4, "PA6": 11, "PA7": 5},
        "adc_absent": ["PA3", "PA7"],                    # errata x035-adc-ch-i2c-unavailable on this part
        "external_pullup": [],
        "adc_high_min": 1000,                            # probe rail == DUT VDD (both 3.3 V)
        "i2c": {"scl": 52, "sda": 50, "route": 2},       # DUT PC16/PC17
        "spi": {"sck": 4, "mosi": 5, "miso": 11, "cs": 53, "dut_cs": "PA4"},
        "pwm": 47,                                       # DUT PA1
    },
    "v003": {
        "port": "/run/board-identify/by-id/esp32-d0wd-v3-0070070d9394",
        "fqbn": "ch32-riscv-ug:ch32v:UIAPDUINO_V003_V14",
        "serial_index": 1,
        "define": "OEP_TARGET_V003",
        "uart_rx": 22, "uart_tx": 21,                   # DUT USART1 PD5 (TX) -> probe 22, PD6 (RX) <- probe 21
        "capture": True, "capture_max_hz": 2_000_000,    # GPIO sampler on core 0 (0.4..2 MHz, 1 byte/sample)
        "gpio": {"PA1": 25, "PA2": 26, "PC0": 5, "PC1": 19, "PC2": 18, "PC3": 17, "PC4": 33, "PC5": 27,
                 "PC6": 4, "PC7": 14, "PD0": 13, "PD2": 32},
        "adc": {"PA1": 25, "PA2": 26, "PC4": 33, "PD2": 32},   # A1 A0 A2 A3; PD3/PD4 (USB) unwired, PD5/PD6 console
        "adc_absent": [],
        "external_pullup": ["PC1", "PC2"],                # board I2C pull-ups R4/R5 (2.2 k) hold SDA/SCL high
        "adc_high_min": 850,                             # ESP32 high reads ~915/1023 on the V003 (rail vs VDD unresolved, 2026-09-22)
        "i2c": {"scl": 18, "sda": 19, "route": 0},       # DUT PC2/PC1, the variant's default route
        "spi": {"sck": 27, "mosi": 4, "miso": 14, "cs": 17, "dut_cs": "PC3"},   # classic ESP32 slave: MISO right up to 3 MHz SCK, 1 bit late at 6 MHz (2026-09-22)
        "pwm": 25,                                       # DUT PA1
    },
}


def add_target_argument(parser, default="x035"):
    parser.add_argument("--target", choices=sorted(TARGETS), default=default, help="fixture profile (see targets.py)")


def build_defines(target: dict) -> list[str]:
    return [f"-D{target['define']}=1"]
