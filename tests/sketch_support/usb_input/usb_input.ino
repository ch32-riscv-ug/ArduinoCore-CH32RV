/* USB device input contracts (tests/peer/usb/device/test_usb_peer_input.py):
 * the HID classes usb_contracts leaves out, merged into one HID interface -
 * an NKRO keyboard, a mouse, a gamepad, consumer control, system control and
 * a custom report (ID 7) - and two CDC ports. The peer host reads them; the
 * debug-probe console (DMSEQ) carries the commands:
 *
 *   ready <nonce>             READY <nonce> <build id> <USB started 0/1> <last error>
 *   mouse <dx> <dy> <wheel> <buttons>   MOUSE <ok>   (then everything released)
 *   pad <x> <y> <hat> <buttons>         PAD <ok>     (then everything released)
 *   consumer <usage>          CONSUMER <ok>   (pressed, then released)
 *   system <usage>            SYSTEM <ok>     (0x81 power off, 0x82 standby, 0x83 wake host; then released)
 *   nkro                      NKRO <ok>       (ten keys held at once, A..J, then released)
 *   custom <hex>              CUSTOM <ok>     (report ID 7, 4 bytes)
 *   lines                     LINES <A baud> <A bits> <A parity> <A stop> <A dtr> <A rts> <B ...>
 *                             (what each port's onLineCoding() / onLineState() last told)
 *   events                    EVENTS <A coding> <A state> <B coding> <B state>  (callbacks run)
 *
 * Over USB, without a command: port A echoes every byte, port B every byte
 * plus one. */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbDevice.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

CH32RVUsbDeviceHidKeyboard keyboard;
CH32RVUsbDeviceHidMouse mouse;
CH32RVUsbDeviceHidGamepad gamepad;
CH32RVUsbDeviceHidConsumerControl consumer;
CH32RVUsbDeviceHidSystemControl systemControl;

/* Vendor page 0xFF10, report ID 7: four bytes in. */
static const uint8_t customDescriptor[] = {
    0x06, 0x10, 0xFF,  /* Usage Page (0xFF10) */
    0x09, 0x01,        /* Usage (1) */
    0xA1, 0x01,        /* Collection (Application) */
    0x85, 0x07,        /*   Report ID (7) */
    0x09, 0x02,        /*   Usage (2) */
    0x15, 0x00,        /*   Logical Minimum (0) */
    0x26, 0xFF, 0x00,  /*   Logical Maximum (255) */
    0x75, 0x08,        /*   Report Size (8) */
    0x95, 0x04,        /*   Report Count (4) */
    0x81, 0x02,        /*   Input (Data, Variable, Absolute) */
    0xC0,              /* End Collection */
};
CH32RVUsbDeviceHidCustom custom(USBDevice, customDescriptor, sizeof customDescriptor, 8);
CH32RVUsbDeviceCdcSerial portB(USBDevice, "Port B");

struct Line {
    CH32RVUsbDeviceCdcLineCoding coding;
    CH32RVUsbDeviceCdcLineState state;
    uint32_t codingEvents, stateEvents;
};
static Line lineA, lineB;

static CH32RVUsbDeviceConfig config;
static char line[80];
static uint8_t used;

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void printLine(const Line &l)
{
    SerialDMSeq.printf(" %lu %u %u %u %u %u", (unsigned long)l.coding.baud, l.coding.dataBits, l.coding.parity,
                       l.coding.stopBits, l.state.dtr ? 1 : 0, l.state.rts ? 1 : 0);
}

static void reply(char *command)
{
    char *arg = strchr(command, ' ');
    if (arg) *arg++ = '\0';
    else arg = command + strlen(command);
    if (strcmp(command, "ready") == 0) {
        SerialDMSeq.printf("READY %s %s %d %s\r\n", arg, BENCH_BUILD_ID, USBDevice.started() ? 1 : 0,
                           USBDevice.lastErrorName());
    } else if (strcmp(command, "mouse") == 0) {
        char *end;
        const int x = (int)strtol(arg, &end, 10), y = (int)strtol(end, &end, 10);
        const int w = (int)strtol(end, &end, 10), b = (int)strtol(end, &end, 10);
        const bool ok = mouse.move((int8_t)x, (int8_t)y, (int8_t)w, (uint8_t)b, 500) && mouse.releaseAll(500);
        SerialDMSeq.printf("MOUSE %d\r\n", ok ? 1 : 0);
    } else if (strcmp(command, "pad") == 0) {
        char *end;
        CH32RVUsbDeviceGamepadReport r;
        r.x = (int8_t)strtol(arg, &end, 10);
        r.y = (int8_t)strtol(end, &end, 10);
        r.hat = (uint8_t)strtol(end, &end, 10);
        r.buttons = strtoul(end, &end, 10);
        const bool ok = gamepad.sendReport(r, 500) && gamepad.releaseAll(500);
        SerialDMSeq.printf("PAD %d\r\n", ok ? 1 : 0);
    } else if (strcmp(command, "consumer") == 0) {
        SerialDMSeq.printf("CONSUMER %d\r\n", consumer.click((uint16_t)strtoul(arg, nullptr, 0), 20) ? 1 : 0);
    } else if (strcmp(command, "system") == 0) {
        SerialDMSeq.printf("SYSTEM %d\r\n", systemControl.click((uint8_t)strtoul(arg, nullptr, 0), 20) ? 1 : 0);
    } else if (strcmp(command, "nkro") == 0) {
        CH32RVUsbDeviceNkroKeyboardReport r;
        for (uint8_t usage = 0x04; usage <= 0x0D; usage++) r.bitmap[usage / 8] |= (uint8_t)(1u << (usage % 8));
        const bool ok = keyboard.sendReport(r, 500) && keyboard.releaseAll(500);
        SerialDMSeq.printf("NKRO %d\r\n", ok ? 1 : 0);
    } else if (strcmp(command, "custom") == 0) {
        uint8_t d[4] = {};
        for (size_t i = 0; i < sizeof d && arg[2 * i] && arg[2 * i + 1]; i++) {
            d[i] = (uint8_t)(hexval(arg[2 * i]) << 4 | hexval(arg[2 * i + 1]));
        }
        SerialDMSeq.printf("CUSTOM %d\r\n", custom.sendReport(d, sizeof d, 7, 500) ? 1 : 0);
    } else if (strcmp(command, "lines") == 0) {
        SerialDMSeq.print("LINES");
        printLine(lineA);
        printLine(lineB);
        SerialDMSeq.print("\r\n");
    } else if (strcmp(command, "events") == 0) {
        SerialDMSeq.printf("EVENTS %lu %lu %lu %lu\r\n", (unsigned long)lineA.codingEvents,
                           (unsigned long)lineA.stateEvents, (unsigned long)lineB.codingEvents,
                           (unsigned long)lineB.stateEvents);
    } else {
        SerialDMSeq.println("ERROR COMMAND");
    }
}

void setup()
{
    SerialDMSeq.begin(115200);
    SerialDMSeq.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
    SerialDMSeq.println(CH32RV.resetReasonName());
    keyboard.enableNkro();
    SerialUSB.onLineCoding([](const CH32RVUsbDeviceCdcLineCoding &c) { lineA.coding = c; lineA.codingEvents++; });
    SerialUSB.onLineState([](const CH32RVUsbDeviceCdcLineState &s) { lineA.state = s; lineA.stateEvents++; });
    portB.onLineCoding([](const CH32RVUsbDeviceCdcLineCoding &c) { lineB.coding = c; lineB.codingEvents++; });
    portB.onLineState([](const CH32RVUsbDeviceCdcLineState &s) { lineB.state = s; lineB.stateEvents++; });
    config.product = "CH32RV usb-input " BENCH_BUILD_ID;
    config.selfPowered = true;       /* the bench powers the DUT; VBUS is not used */
    USBDevice.begin(config);
}

void loop()
{
    deliverEvents();   /* the callbacks set in setup() run here */
    while (SerialUSB.available()) {
        SerialUSB.write(SerialUSB.read());
    }
    while (portB.available()) {
        portB.write((uint8_t)(portB.read() + 1));
    }
    while (SerialDMSeq.available()) {
        const int c = SerialDMSeq.read();
        if (c == '\n') {
            line[used] = 0;
            if (used) {
                reply(line);
            }
            used = 0;
        } else if (c != '\r' && used < sizeof line - 1) {
            line[used++] = (char)c;
        }
    }
}
