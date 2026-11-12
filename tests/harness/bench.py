"""The case names run_hardware.py knows. A target is named by its address alone (harness/place.py); the bench file
(tests/fixtures/bench.*.toml, CH32_TEST_CONFIG) is gone since 2026-11-05."""

CASES = {"runtime", "uart", "gpio", "hid", "power", "usb", "usb-classes", "usb-media", "eth", "logic", "pd", "synckit",
         "flash", "wire", "softwire", "softspi", "spi", "sd", "periph", "signals", "system",
         "console-rtt", "console-dmdata", "console-sdi", "analog", "selfcheck", "selfbus", "selfsys",
         "usb-peer", "usb-peer-classes", "usb-peer-media", "usb-peer-net", "usb-peer-fs",
         "usb-peer-host", "usb-peer-host-classes", "usb-peer-host-input",
         "usb-peer-host-storage", "usb-peer-input"}
# Cases with a USB peer on the DUT's own USB port, an ESP32-S3 or an RP2040 (tests/peer/usb).
PEER_CASES = {"usb-peer", "usb-peer-classes", "usb-peer-media", "usb-peer-net", "usb-peer-fs",
              "usb-peer-host", "usb-peer-host-classes", "usb-peer-host-input", "usb-peer-host-storage",
              "usb-peer-input"}
