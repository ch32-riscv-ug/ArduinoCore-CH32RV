/* The RP2040 as a USB device for tests/peer/usb/host/input: two HID
 * interfaces and two CDC ports. The DUT (CH32RVUsbHost) is the host; the
 * RP2040 runs TinyUSB's device stack (Adafruit TinyUSB Library).
 *   HID 0  gamepad (report ID 1), system control (ID 2) and a vendor
 *          collection (ID 3) with 8-byte input, output and feature reports
 *   HID 1  a keyboard with the boot protocol (subclass 1), no report ID
 *   CDC 0  echoes every byte;  CDC 1  echoes every byte plus one
 *
 * Commands, one per line on the RTT console; answers start "PD ":
 *   ?                        PD READY <mounted 0/1>
 *   gamepad <x> <y> <buttons>  PD GAMEPAD <ok>      (then the same report with all zero)
 *   system <value>           PD SYSTEM <ok>        (1 power down, 2 sleep, 3 wake up; then 0)
 *   vin <hex>                PD VIN <ok>           (vendor input report, ID 3)
 *   key <keycode>            PD KEY <ok>           (on the keyboard: pressed, then released)
 *   feature <hex>            PD FEATURE            (what GET_REPORT(feature, 3) answers from now on)
 *   last                     PD LAST <output hex> <feature hex>  (the last vendor output / feature set)
 *   proto                    PD PROTO <keyboard protocol: 0 boot, 1 report>
 *   leds                     PD LEDS <keyboard LED bits>
 *   line <port>              PD LINE <port> <baud> <data bits> <parity> <stop bits> <dtr> <rts>
 *   send <port> <hex>        PD SENT <ok>          (bytes out of that CDC port)
 *   desclen                  PD DESCLEN <HID 0 report descriptor bytes> <HID 1 report descriptor bytes>
 *   replug <ms>              PD REPLUG             (detach, wait, attach) */
#include <Adafruit_TinyUSB.h>
#include <RTTStream.h>

RTTStream rtt;

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

/* Adafruit TinyUSB's RP2040 port runs the USB task in an interrupt, and only when it can take __usb_mutex.
 * Every TinyUSB call from loop() holds it (UsbLock), or the task comes in while loop() holds a FIFO's own
 * lock and waits for it - a second at a bus reset, long enough for the host to give up on the device. */
extern mutex_t __usb_mutex;
struct UsbLock
{
  UsbLock() { mutex_enter_blocking(&__usb_mutex); }
  ~UsbLock() { mutex_exit(&__usb_mutex); }
};

enum { RID_GAMEPAD = 1, RID_SYSTEM, RID_VENDOR };

static const uint8_t descriptor0[] = {
    TUD_HID_REPORT_DESC_GAMEPAD(HID_REPORT_ID(RID_GAMEPAD)),
    TUD_HID_REPORT_DESC_SYSTEM_CONTROL(HID_REPORT_ID(RID_SYSTEM)),
    HID_USAGE_PAGE_N(HID_USAGE_PAGE_VENDOR, 2),
    HID_USAGE(0x01),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
      HID_REPORT_ID(RID_VENDOR)
      HID_LOGICAL_MIN(0x00),
      HID_LOGICAL_MAX_N(0xff, 2),
      HID_REPORT_SIZE(8),
      HID_REPORT_COUNT(8),
      HID_USAGE(0x02),
      HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
      HID_USAGE(0x03),
      HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
      HID_USAGE(0x04),
      HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END,
};
static const uint8_t descriptor1[] = {TUD_HID_REPORT_DESC_KEYBOARD()};

Adafruit_USBD_HID hid0(descriptor0, sizeof descriptor0, HID_ITF_PROTOCOL_NONE, 2, true);
Adafruit_USBD_HID hid1(descriptor1, sizeof descriptor1, HID_ITF_PROTOCOL_KEYBOARD, 2, false);
Adafruit_USBD_CDC serial1;

static uint8_t feature[8] = {'F', 'E', 'A', 'T', 'U', 'R', 'E', '0'};
static uint8_t lastOutput[8], lastFeature[8];
static volatile uint8_t leds;
static char line[200];
static size_t used;

static int hexval(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static size_t parseHex(const char *s, uint8_t *out, size_t cap)
{
  size_t n = 0;
  while (s[0] && s[1] && n < cap && hexval(s[0]) >= 0 && hexval(s[1]) >= 0)
  {
    out[n++] = (uint8_t)(hexval(s[0]) << 4 | hexval(s[1]));
    s += 2;
  }
  return n;
}

static void printHex(const uint8_t *d, size_t n)
{
  for (size_t i = 0; i < n; i++) say("%02x", d[i]);
}

/* A report once the host has taken the one before (up to 500 ms); the lock only around the calls. */
static bool report(Adafruit_USBD_HID &h, uint8_t id, const void *data, uint16_t length)
{
  const uint32_t t0 = millis();
  for (;;)
  {
    {
      UsbLock lock;
      if (h.ready()) return h.sendReport(id, data, length);
    }
    if (millis() - t0 > 500) return false;
    delay(1);
  }
}

static bool gamepad(int x, int y, uint32_t buttons)
{
  hid_gamepad_report_t r = {};
  r.x = (int8_t)x;
  r.y = (int8_t)y;
  r.buttons = buttons;
  hid_gamepad_report_t zero = {};
  return report(hid0, RID_GAMEPAD, &r, sizeof r) && report(hid0, RID_GAMEPAD, &zero, sizeof zero);
}

static bool key(uint8_t keycode)
{
  uint8_t r[8] = {0, 0, keycode};
  uint8_t none[8] = {};
  return report(hid1, 0, r, sizeof r) && report(hid1, 0, none, sizeof none);
}

static void onSet0(uint8_t id, hid_report_type_t type, const uint8_t *buffer, uint16_t length)
{
  if (id == 0 && length)  /* through the OUT endpoint: the ID is the first byte */
  {
    id = buffer[0];
    buffer++;
    length--;
  }
  if (id != RID_VENDOR) return;
  uint8_t *to = type == HID_REPORT_TYPE_FEATURE ? lastFeature : lastOutput;
  memset(to, 0, 8);
  memcpy(to, buffer, length > 8 ? 8 : length);
}

static uint16_t onGet0(uint8_t id, hid_report_type_t type, uint8_t *buffer, uint16_t length)
{
  if (id != RID_VENDOR || type != HID_REPORT_TYPE_FEATURE || length < sizeof feature) return 0;
  memcpy(buffer, feature, sizeof feature);
  return sizeof feature;
}

static void onSet1(uint8_t, hid_report_type_t type, const uint8_t *buffer, uint16_t length)
{
  if (type == HID_REPORT_TYPE_OUTPUT && length) leds = buffer[0];
}

static uint16_t onGet1(uint8_t, hid_report_type_t, uint8_t *, uint16_t) { return 0; }

static void run(char *cmd)
{
  char *arg = strchr(cmd, ' ');
  if (arg) *arg++ = '\0';
  else arg = cmd + strlen(cmd);
  if (!strcmp(cmd, "?")) say("PD READY %u\n", TinyUSBDevice.mounted() ? 1 : 0);
  else if (!strcmp(cmd, "gamepad"))
  {
    int x = 0, y = 0;
    unsigned long b = 0;
    sscanf(arg, "%d %d %lu", &x, &y, &b);
    say("PD GAMEPAD %u\n", gamepad(x, y, (uint32_t)b) ? 1 : 0);
  }
  else if (!strcmp(cmd, "system"))
  {
    const uint8_t v = (uint8_t)strtoul(arg, nullptr, 0), zero = 0;
    say("PD SYSTEM %u\n", report(hid0, RID_SYSTEM, &v, 1) && report(hid0, RID_SYSTEM, &zero, 1) ? 1 : 0);
  }
  else if (!strcmp(cmd, "vin"))
  {
    uint8_t d[8] = {};
    parseHex(arg, d, sizeof d);
    say("PD VIN %u\n", report(hid0, RID_VENDOR, d, sizeof d) ? 1 : 0);
  }
  else if (!strcmp(cmd, "key")) say("PD KEY %u\n", key((uint8_t)strtoul(arg, nullptr, 0)) ? 1 : 0);
  else if (!strcmp(cmd, "feature"))
  {
    memset(feature, 0, sizeof feature);
    parseHex(arg, feature, sizeof feature);
    say("PD FEATURE\n");
  }
  else if (!strcmp(cmd, "last"))
  {
    say("PD LAST ");
    printHex(lastOutput, 8);
    say(" ");
    printHex(lastFeature, 8);
    say("\n");
  }
  else if (!strcmp(cmd, "proto"))
  {
    uint8_t p;
    {
      UsbLock lock;
      p = tud_hid_n_get_protocol(1);
    }
    say("PD PROTO %u\n", p);
  }
  else if (!strcmp(cmd, "leds")) say("PD LEDS %u\n", leds);
  else if (!strcmp(cmd, "line"))
  {
    const uint8_t port = (uint8_t)strtoul(arg, nullptr, 10);
    cdc_line_coding_t c = {};
    uint8_t state;
    {
      UsbLock lock;
      tud_cdc_n_get_line_coding(port, &c);
      state = tud_cdc_n_get_line_state(port);
    }
    say("PD LINE %u %lu %u %u %u %u %u\n", port, (unsigned long)c.bit_rate, c.data_bits, c.parity, c.stop_bits,
        state & 1, (state >> 1) & 1);
  }
  else if (!strcmp(cmd, "send"))
  {
    char *rest = nullptr;
    const uint8_t port = (uint8_t)strtoul(arg, &rest, 10);
    uint8_t d[64];
    const size_t n = parseHex(rest && *rest ? rest + 1 : rest, d, sizeof d);
    bool ok;
    {
      UsbLock lock;
      ok = tud_cdc_n_write(port, d, n) == n;
      tud_cdc_n_write_flush(port);
    }
    say("PD SENT %u\n", ok ? 1 : 0);
  }
  else if (!strcmp(cmd, "desclen")) say("PD DESCLEN %u %u\n", (unsigned)sizeof descriptor0, (unsigned)sizeof descriptor1);
  else if (!strcmp(cmd, "replug"))
  {
    const uint32_t ms = strtoul(arg, nullptr, 10);
    {
      UsbLock lock;
      TinyUSBDevice.detach();
    }
    delay(ms ? ms : 300);
    {
      UsbLock lock;
      TinyUSBDevice.attach();
    }
    say("PD REPLUG\n");
  }
  else say("PD UNKNOWN %s\n", cmd);
}

void setup()
{
  if (!TinyUSBDevice.isInitialized()) TinyUSBDevice.begin(0);
  TinyUSBDevice.setManufacturerDescriptor("ch32rv-tests");
  TinyUSBDevice.setProductDescriptor("Pico peer input");
  TinyUSBDevice.setSerialDescriptor("PICOPEERINPUT");
  Serial.begin(115200);
  serial1.begin(115200);
  hid0.setReportCallback(onGet0, onSet0);
  hid0.begin();
  hid1.setReportCallback(onGet1, onSet1);
  hid1.begin();
  /* The core enumerated with its CDC before setup(): again, with every interface. */
  if (TinyUSBDevice.mounted())
  {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
  say("PD BOOTED\n");
}

void loop()
{
  {
    UsbLock lock;
    for (uint8_t port = 0; port < 2; port++)
    {
      uint8_t buf[64];
      const uint32_t n = tud_cdc_n_read(port, buf, sizeof buf);
      if (!n) continue;
      if (port == 1)
        for (uint32_t i = 0; i < n; i++) buf[i]++;
      tud_cdc_n_write(port, buf, n);
      tud_cdc_n_write_flush(port);
    }
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
