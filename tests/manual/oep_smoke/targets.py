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


# Pico bench, added 2026-09-23. The pin map below was measured, not assumed: with the
# target halted its GPIO registers are reachable, so each side was driven a pin at a time
# and read from the other (probe repo, scratchpad/l103_wiremap.py). Only part of the
# header is wired - no ADC input reaches the probe at all - and PA11/PA12 are the part's
# USB pins, so leave them alone if USB is ever used. See docs/pico-bench.ja.md.
PICO_PROBES = {
    "rp2350": {
        "role": "OEP probe for the CH32L103 (RVSWD, bit-bang)",
        "fqbn": "rp2040:rp2040:sparkfun_promicrorp2350:usbstack=picosdk",
        "example": "oep-probe-arduino/examples/Rp2350L103Probe",
        "bootsel_usb": "2e8a:000f",
        "port": "/dev/serial/by-id/usb-SparkFun_ProMicro_RP2350_9489DD2AE0953650-if00",
        "swdio": 0, "swclk": 1, "nrst": 2,
    },
    "rp2040zero": {
        "role": "ordinary ARM SWD counterpart on the Pro Micro RP2350's SWD header (GP0/GP1)",
        "fqbn": "rp2040:rp2040:waveshare_rp2040_zero:usbstack=picosdk",
        "example": "oep-probe-arduino/examples/Rp2040SwdSurvey",
        "bootsel_usb": "2e8a:0003",
    },
}

# CH32L103C8T6 on the Pro Micro RP2350 (measured 2026-09-23). Not in TARGETS yet: the basic
# suite builds sketches that open Serial on the variant's default USART1 route (PA9/PA10),
# and those land on probe GP20/GP6, which is not a hardware UART pair on the RP2350. Route 1
# (PB6/PB7) does land on one - GP13/GP12, verified with a console sketch - so what is missing
# is a way for a build to pick the route. Everything else here is ready.
L103_JIG = {
    "port": "/dev/serial/by-id/usb-SparkFun_ProMicro_RP2350_9489DD2AE0953650-if00",
    "fqbn": "ch32-riscv-ug:ch32v:CH32L103:pnum=CH32L103C8T6",
    "serial_index": 1,
    "define": "OEP_TARGET_L103",
    "uart_rx": 13, "uart_tx": 12,                     # DUT USART1 route 1: PB6 (TX) -> probe 13, PB7 (RX) <- probe 12
    "capture": False, "capture_max_hz": 0,            # the Pico probe has no fixture.capture yet
    "gpio": {"PA8": 7, "PA9": 20, "PA10": 6, "PA11": 21, "PA12": 5, "PA15": 15,
             "PB3": 4, "PB4": 14, "PB5": 3, "PB8": 11, "PC13": 10},   # PB6/PB7 are the console
    "adc": {}, "adc_absent": [], "external_pullup": [], "adc_high_min": 0,
    "i2c": {"scl": 13, "sda": 12, "route": 0},        # DUT PB6/PB7 - the same wires as the console, so exclusive
    "spi": {"sck": 4, "mosi": 3, "miso": 14, "cs": 15, "dut_cs": "PA15"},   # SPI1 route 1
    "pwm": 7,                                         # DUT PA8 = TIM1_CH1
}


def add_target_argument(parser, default="x035"):
    parser.add_argument("--target", choices=sorted(TARGETS), default=default, help="fixture profile (see targets.py)")


def build_defines(target: dict) -> list[str]:
    return [f"-D{target['define']}=1"]
