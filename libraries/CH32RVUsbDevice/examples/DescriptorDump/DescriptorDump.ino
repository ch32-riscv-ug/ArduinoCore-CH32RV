/* Print what the host is shown - the device, configuration, BOS, Microsoft
 * OS 2.0, string and HID report descriptors - on the UART, and every control
 * request the host sends, from onAnyControlRequest(). Bus events too.
 *
 * Open the UART monitor (115200) and plug the board in: the descriptors are
 * printed once the host has configured the device, and again on 'd'. */
#include <CH32RVUsbDevice.h>

CH32RVUsbDeviceCdcSerial UsbSerial;
CH32RVUsbDeviceHidKeyboard Keyboard;
CH32RVUsbDeviceVendor Vendor;

static volatile bool dumpNow;

static void hex(const uint8_t *p, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++) {
        Serial.printf("%02x%s", p[i], (i % 16 == 15 || i + 1 == n) ? "\r\n" : " ");
    }
}

static void dumpString(uint8_t index, const char *label)
{
    const uint16_t *s = USBDevice.stringDescriptor(index);
    if (!s) {
        Serial.printf("string %u (%s): none\r\n", index, label);
        return;
    }
    const uint8_t bytes = (uint8_t)(s[0] & 0xFF);
    Serial.printf("string %u (%s): \"", index, label);
    for (uint8_t i = 1; i < bytes / 2; i++) {
        Serial.write((char)(s[i] & 0x7F));
    }
    Serial.println("\"");
}

static void dump()
{
    if (const uint8_t *d = USBDevice.deviceDescriptor()) {
        Serial.printf("device: USB %x.%02x, VID:PID %04x:%04x, EP0 %u\r\n", d[3], d[2], d[8] | d[9] << 8,
                      d[10] | d[11] << 8, d[7]);
        hex(d, d[0]);
    }
    if (const uint8_t *c = USBDevice.configurationDescriptor()) {
        const uint16_t n = (uint16_t)(c[2] | c[3] << 8);
        Serial.printf("configuration: %u bytes, %u interfaces\r\n", n, c[4]);
        hex(c, n);
    }
    if (const uint8_t *b = USBDevice.bosDescriptor()) {
        Serial.printf("BOS: %u bytes\r\n", USBDevice.bosDescriptorLength());
        hex(b, USBDevice.bosDescriptorLength());
    }
    if (const uint8_t *m = USBDevice.microsoftOs20Descriptor()) {
        Serial.printf("Microsoft OS 2.0: %u bytes\r\n", USBDevice.microsoftOs20DescriptorLength());
        hex(m, USBDevice.microsoftOs20DescriptorLength());
    }
    if (const uint8_t *r = USBDevice.hidReportDescriptor()) {
        Serial.printf("HID report: %u bytes\r\n", USBDevice.hidReportDescriptorLength());
        hex(r, USBDevice.hidReportDescriptorLength());
    }
    dumpString(1, "manufacturer");
    dumpString(2, "product");
    dumpString(3, "serial");
}

void setup()
{
    Serial.begin(115200);
    USBDevice.onAnyControlRequest([](const CH32RVUsbDeviceControlRequestInfo &r) {
        Serial.printf("%s %02x %02x %04x %04x %4u -> %s %u\r\n",
                      r.stage == CH32RV_USB_DEVICE_CONTROL_STAGE_SETUP ? "SETUP" : "ACK  ", r.bmRequestType,
                      r.bRequest, r.wValue, r.wIndex, r.wLength, r.handled ? "ok" : "stall", r.responseLength);
    });
    USBDevice.onBusEvent([](CH32RVUsbBusEvent e) {
        static const char *const names[] = {"reset", "configured", "unconfigured", "suspended", "resumed"};
        Serial.printf("bus: %s\r\n", names[(uint8_t)e]);
        if (e == CH32RVUsbBusEvent::Configured) {
            dumpNow = true;
        }
    });
    USBDevice.begin();
}

void loop()
{
    while (Serial.available()) {
        if (Serial.read() == 'd') {
            dumpNow = true;
        }
    }
    if (dumpNow) {
        dumpNow = false;
        dump();
    }
}
