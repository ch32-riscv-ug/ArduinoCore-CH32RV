/* The ESP32-S3 as a USB device for tests/peer/usb/host/hid: a composite of a
 * keyboard, a mouse, consumer control and a vendor HID (one HID interface,
 * report IDs), CDC serial and a FAT RAM disk - the S3's four IN endpoints.
 * The DUT (a CH32 running CH32RVUsbHost) is the host. EspUsbDevice is TinyUSB
 * on the ESP32-S3 OTG port; the DUT's host stack is TinyUSB too, but a
 * different controller and our own host driver.
 *
 * Commands, one per line on Serial (UART0); answers start "S3D ":
 *   ?                    S3D READY <mounted 0/1>
 *   type <text>          S3D TYPED <ok>
 *   mouse <dx> <dy> <b>  S3D MOUSE <ok>
 *   consumer <usage>     S3D CONSUMER <ok>   (pressed, then released)
 *   leds                 S3D LEDS <LED bits the host last set>
 *   file                 S3D FILE <first line of HOST.TXT, or NONE>
 *   cdcsend <hex>        S3D CDCSENT <ok>
 *
 * Without a command: CDC echoes every byte, a vendor output report comes back
 * as an input report with its first byte incremented. The disk holds
 * README.TXT ("EspUsbDevice peer"). */
#include <Arduino.h>
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceHidMouse mouse(device);
EspUsbDeviceHidConsumerControl consumer(device);
EspUsbDeviceHidVendor vendor(device);
EspUsbDeviceCdcSerial cdc(device);
EspUsbDeviceMsc msc(device);

static uint8_t storage[64 * 1024];
static EspUsbDeviceMscFatRamDisk disk(storage, sizeof storage);
static uint8_t vendorEcho[64];
static volatile size_t vendorEchoLength;
static char line[200];
static size_t used;

/* EspUsbDevice 2.6.0's sendHidReport() ignores its timeout: a report while
 * the previous one still waits for the host's IN fails at once. Every report
 * here is retried until the host has taken the one before (up to 500 ms). */
#define RETRY(expr) ({ bool ok_; const uint32_t t0_ = millis(); \
                       while (!(ok_ = (expr)) && millis() - t0_ < 500) delay(1); ok_; })

static bool typeText(const char *text)
{
  for (const char *p = text; *p; p++)
    if (!RETRY(keyboard.pressKey(*p)) || !RETRY(keyboard.releaseAll()))
      return false;
  return true;
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
  if (!strcmp(cmd, "?")) Serial.printf("S3D READY %u\n", device.ready() ? 1 : 0);
  else if (!strcmp(cmd, "type")) Serial.printf("S3D TYPED %u\n", typeText(arg) ? 1 : 0);
  else if (!strcmp(cmd, "mouse"))
  {
    int dx = 0, dy = 0, b = 0;
    sscanf(arg, "%d %d %d", &dx, &dy, &b);
    const bool ok = RETRY(mouse.move(dx, dy, 0, b)) && RETRY(mouse.move(0, 0, 0, 0));
    Serial.printf("S3D MOUSE %u\n", ok ? 1 : 0);
  }
  else if (!strcmp(cmd, "consumer"))
  {
    const uint16_t usage = (uint16_t)strtoul(arg, nullptr, 0);
    Serial.printf("S3D CONSUMER %u\n", RETRY(consumer.press(usage)) && RETRY(consumer.release()) ? 1 : 0);
  }
  else if (!strcmp(cmd, "leds")) Serial.printf("S3D LEDS %u\n", keyboard.ledState().leds);
  else if (!strcmp(cmd, "file"))
  {
    char text[80] = {};
    const size_t n = disk.readFile("HOST.TXT", (uint8_t *)text, sizeof text - 1);
    char *end = strpbrk(text, "\r\n");
    if (end) *end = '\0';
    Serial.printf("S3D FILE %s\n", n ? text : "NONE");
  }
  else if (!strcmp(cmd, "cdcsend"))
  {
    uint8_t d[96];
    size_t n = 0;
    for (const char *s = arg; s[0] && s[1] && n < sizeof d; s += 2) d[n++] = (uint8_t)(hexval(s[0]) << 4 | hexval(s[1]));
    Serial.printf("S3D CDCSENT %u\n", cdc.write(d, n) == n ? 1 : 0);
    cdc.flush();
  }
  else Serial.printf("S3D UNKNOWN %s\n", cmd);
}

void setup()
{
  Serial.begin(115200);
  disk.format("S3PEER");
  disk.addTextFile("README.TXT", "EspUsbDevice peer\r\n");
  /* The RAM disk's attach() installs the block callbacks but leaves the
   * medium absent: without these TEST UNIT READY keeps failing, and TinyUSB's
   * MSC host retries it forever, so the whole device never mounts. */
  msc.mediaPresent(true);
  msc.isWritable(true);
  disk.attach(msc);
  vendor.onOutputReport([](const EspUsbDeviceHidReport &r)
                        {
                          const size_t n = r.length > sizeof vendorEcho ? sizeof vendorEcho : r.length;
                          memcpy(vendorEcho, r.data, n);
                          if (n) vendorEcho[0]++;
                          vendorEchoLength = n; });
  EspUsbDeviceConfig config;
  config.product = "S3 peer hid";
  config.manufacturer = "EspUsbDevice";
  config.serialNumber = "S3PEERHID";
  if (!device.begin(config))
    Serial.printf("S3D BEGIN_FAILED %s\n", device.lastErrorName());
  Serial.println("S3D BOOTED");
}

void loop()
{
  while (cdc.available()) cdc.write((uint8_t)cdc.read());
  if (vendorEchoLength)
  {
    vendor.sendInput(vendorEcho, vendorEchoLength);
    vendorEchoLength = 0;
  }
  while (Serial.available())
  {
    const char c = (char)Serial.read();
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
  delay(1);
}
