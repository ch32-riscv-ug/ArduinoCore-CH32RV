/* The RP2040 as a USB device for tests/peer/usb/host/hid: one HID interface with
 * a keyboard, a mouse, consumer control and a vendor HID (report IDs 1-4), CDC
 * serial and a RAM disk. The DUT (a CH32 running CH32RVUsbHost) is the host;
 * the RP2040 runs TinyUSB's device stack (Adafruit TinyUSB Library).
 *
 * Commands, one per line on the RTT console (SEGGER RTT over the probe's SWD);
 * answers start "PD ":
 *   ?                    PD READY <mounted 0/1>
 *   type <text>          PD TYPED <ok>
 *   mouse <dx> <dy> <b>  PD MOUSE <ok>        (moved with the buttons, then released)
 *   consumer <usage>     PD CONSUMER <ok>     (pressed, then released)
 *   leds                 PD LEDS <LED bits the host last set>
 *   cdcsend <hex>        PD CDCSENT <ok>
 *
 * Without a command: CDC echoes every byte, a vendor output report comes back
 * as an input report with its first byte incremented. */
#include <Adafruit_TinyUSB.h>
#include <RTTStream.h>

enum { RID_KEYBOARD = 1, RID_MOUSE, RID_CONSUMER, RID_VENDOR };

static const uint8_t report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(RID_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(RID_MOUSE)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(RID_CONSUMER)),
    TUD_HID_REPORT_DESC_GENERIC_INOUT(63, HID_REPORT_ID(RID_VENDOR)),
};

RTTStream rtt;

/* Adafruit TinyUSB's RP2040 port runs the USB task in an interrupt, and only when it can take __usb_mutex.
 * Every TinyUSB call from loop() holds it (UsbLock), or the task comes in while loop() holds a FIFO's own
 * lock and waits for it - a second at a bus reset, long enough for the host to give up on the device. */
extern mutex_t __usb_mutex;
struct UsbLock
{
  UsbLock() { mutex_enter_blocking(&__usb_mutex); }
  ~UsbLock() { mutex_exit(&__usb_mutex); }
};

/* printf to the console. RTTStream's own printf writes hex digits in upper case; the lines are lower case. */
static void say(const char *format, ...)
{
  char text[256];
  va_list ap;
  va_start(ap, format);
  vsnprintf(text, sizeof text, format, ap);
  va_end(ap);
  rtt.print(text);
}
Adafruit_USBD_HID hid(report_descriptor, sizeof report_descriptor, HID_ITF_PROTOCOL_NONE, 2, true);
Adafruit_USBD_MSC msc;

static uint8_t disk[128][512];
static volatile uint8_t leds;
static uint8_t vendorEcho[63];
static volatile size_t vendorEchoLength;
static char line[200];
static size_t used;

/* A report is sent once the host has taken the one before (up to 500 ms). */
static bool report(uint8_t id, const void *data, uint16_t length)
{
  const uint32_t t0 = millis();
  for (;;)
  {
    {
      UsbLock lock;
      if (hid.ready()) return hid.sendReport(id, data, length);
    }
    /* The DUT's host stack runs between its sketch's prints: over a slow
     * console (a CH549 WCH-Link) it can leave the endpoint unpolled for a few
     * hundred ms (V203RB 2026-11-13: TYPED 0 after the first key). */
    if (millis() - t0 > 2000) return false;
    delay(1);
  }
}

static bool key(uint8_t keycode)
{
  uint8_t keys[6] = {keycode};
  uint8_t r[8] = {0, 0};
  memcpy(r + 2, keys, 6);
  return report(RID_KEYBOARD, r, sizeof r);
}

static bool typeText(const char *text)
{
  static const uint8_t ascii[128][2] = {HID_ASCII_TO_KEYCODE};
  for (const char *p = text; *p; p++)
  {
    const uint8_t c = (uint8_t)*p & 0x7F;
    if (ascii[c][0]) return false;  /* letters and digits only: no shift */
    if (!key(ascii[c][1]) || !key(0)) return false;
  }
  return true;
}

static bool mouse(int dx, int dy, uint8_t buttons)
{
  hid_mouse_report_t r = {buttons, (int8_t)dx, (int8_t)dy, 0, 0};
  hid_mouse_report_t zero = {0, 0, 0, 0, 0};
  return report(RID_MOUSE, &r, sizeof r) && report(RID_MOUSE, &zero, sizeof zero);
}

static bool consumer(uint16_t usage)
{
  const uint16_t none = 0;
  return report(RID_CONSUMER, &usage, sizeof usage) && report(RID_CONSUMER, &none, sizeof none);
}

static int hexval(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static void run(char *cmd)
{
  char *arg = strchr(cmd, ' ');
  if (arg) *arg++ = '\0';
  else arg = cmd + strlen(cmd);
  if (!strcmp(cmd, "?")) say("PD READY %u\n", TinyUSBDevice.mounted() ? 1 : 0);
  else if (!strcmp(cmd, "type")) say("PD TYPED %u\n", typeText(arg) ? 1 : 0);
  else if (!strcmp(cmd, "mouse"))
  {
    int dx = 0, dy = 0, b = 0;
    sscanf(arg, "%d %d %d", &dx, &dy, &b);
    say("PD MOUSE %u\n", mouse(dx, dy, (uint8_t)b) ? 1 : 0);
  }
  else if (!strcmp(cmd, "consumer")) say("PD CONSUMER %u\n", consumer((uint16_t)strtoul(arg, nullptr, 0)) ? 1 : 0);
  else if (!strcmp(cmd, "leds")) say("PD LEDS %u\n", leds);
  else if (!strcmp(cmd, "cdcsend"))
  {
    uint8_t d[96];
    size_t n = 0;
    for (const char *s = arg; s[0] && s[1] && n < sizeof d; s += 2) d[n++] = (uint8_t)(hexval(s[0]) << 4 | hexval(s[1]));
    bool ok;
    {
      UsbLock lock;
      ok = Serial.write(d, n) == n;
      Serial.flush();
    }
    say("PD CDCSENT %u\n", ok ? 1 : 0);
  }
  else say("PD UNKNOWN %s\n", cmd);
}

static void onSetReport(uint8_t id, hid_report_type_t type, const uint8_t *buffer, uint16_t length)
{
  if (id == 0 && length)  /* through the OUT endpoint: the ID is the first byte */
  {
    id = buffer[0];
    buffer++;
    length--;
  }
  (void)type;
  if (id == RID_KEYBOARD && length) leds = buffer[0];
  else if (id == RID_VENDOR)
  {
    const size_t n = length > sizeof vendorEcho ? sizeof vendorEcho : length;
    memcpy(vendorEcho, buffer, n);
    if (n) vendorEcho[0]++;
    vendorEchoLength = n;
  }
}

static uint16_t onGetReport(uint8_t, hid_report_type_t, uint8_t *, uint16_t) { return 0; }

void setup()
{
  if (!TinyUSBDevice.isInitialized()) TinyUSBDevice.begin(0);
  TinyUSBDevice.setManufacturerDescriptor("ch32rv-tests");
  TinyUSBDevice.setProductDescriptor("Pico peer hid");
  TinyUSBDevice.setSerialDescriptor("PICOPEERHID");
  Serial.begin(115200);
  hid.setReportCallback(onGetReport, onSetReport);
  hid.begin();
  msc.setID("ch32rv", "Pico RAM disk", "1.0");
  msc.setCapacity(sizeof disk / sizeof disk[0], sizeof disk[0]);
  msc.setReadWriteCallback(
      [](uint32_t lba, void *buffer, uint32_t size) -> int32_t
      {
        if (lba >= sizeof disk / sizeof disk[0]) return -1;
        memcpy(buffer, disk[lba], size);
        return (int32_t)size;
      },
      [](uint32_t lba, uint8_t *buffer, uint32_t size) -> int32_t
      {
        if (lba >= sizeof disk / sizeof disk[0]) return -1;
        memcpy(disk[lba], buffer, size);
        return (int32_t)size;
      },
      [] {});
  msc.setUnitReady(true);
  msc.begin();
  /* The core enumerated with CDC alone before setup(): again, with every interface. */
  if (TinyUSBDevice.mounted())
  {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
  rtt.println("PD BOOTED");
}

void loop()
{
  {
    UsbLock lock;
    while (Serial.available()) Serial.write((uint8_t)Serial.read());
  }
  if (vendorEchoLength)
  {
    report(RID_VENDOR, vendorEcho, sizeof vendorEcho);
    vendorEchoLength = 0;
  }
  while (rtt.available())
  {
    const char c = (char)rtt.read();
    if (c == '\r') continue;
    if (c == '\n')
    {
      line[used] = '\0';
      if (used) run(line);
      used = 0;
    }
    else if (used + 1 < sizeof line)
      line[used++] = c;
  }
  TinyUSB_Device_Task();   /* what the interrupt skipped while loop() held the lock */
}
