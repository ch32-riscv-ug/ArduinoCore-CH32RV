/* The ESP32-S3 as a USB device for tests/peer/usb/host/classes: MIDI, a
 * vendor bulk interface with a vendor request, and a smart card reader (CCID) -
 * the S3's four IN endpoints. The DUT (CH32RVUsbHost) is the host.
 *
 * Commands, one per line on Serial (UART0); answers start "S3D ":
 *   ?                    S3D READY <mounted 0/1>
 *   midisend <ch> <n> <v>  S3D MIDISENT <ok>
 *   card                 S3D CARD <APDUs answered>
 *
 * Without a command: a MIDI packet comes back with its note raised by 12, the
 * vendor interface echoes every byte, vendor request 0x10 (IN) answers
 * "ESP32S3", and the card answers an APDU with its bytes reversed, then 90 00. */
#include <Arduino.h>
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceMidi midi(device);
EspUsbDeviceVendor vendor(device);
EspUsbDeviceCcid ccid(device);

static const uint8_t atr[] = {0x3B, 0x88, 0x80, 0x01, 'E', 'S', 'P', '3', '2', 'S', '3', 'C', 0x00};
static volatile uint32_t apdus;
static char line[120];
static size_t used;

static void run(char *cmd)
{
  char *arg = strchr(cmd, ' ');
  if (arg) *arg++ = '\0';
  else arg = cmd + strlen(cmd);
  if (!strcmp(cmd, "?")) Serial.printf("S3D READY %u\n", device.ready() ? 1 : 0);
  else if (!strcmp(cmd, "midisend"))
  {
    unsigned ch = 0, n = 0, v = 0;
    sscanf(arg, "%u %u %u", &ch, &n, &v);
    Serial.printf("S3D MIDISENT %u\n", midi.noteOn(ch, n, v) ? 1 : 0);
  }
  else if (!strcmp(cmd, "card")) Serial.printf("S3D CARD %lu\n", (unsigned long)apdus);
  else Serial.printf("S3D UNKNOWN %s\n", cmd);
}

void setup()
{
  Serial.begin(115200);
  vendor.onControlRequest([](const EspUsbDeviceVendorControlRequest &r) -> bool
                          {
                            static const char answer[] = "ESP32S3";
                            if ((r.bmRequestType & 0x80) && r.bRequest == 0x10)
                            {
                              if (r.stage != ESP_USB_DEVICE_CONTROL_STAGE_SETUP) return true;
                              return vendor.sendControlResponse(r, answer, min((size_t)r.wLength, sizeof answer - 1));
                            }
                            return false; });
  ccid.insertCard(atr, sizeof atr);
  ccid.onApdu([](const uint8_t *apdu, size_t length, uint8_t *response, size_t capacity) -> size_t
              {
                if (length + 2 > capacity) return 0;
                for (size_t i = 0; i < length; i++) response[i] = apdu[length - 1 - i];
                response[length] = 0x90;
                response[length + 1] = 0x00;
                apdus++;
                return length + 2; });
  EspUsbDeviceConfig config;
  config.product = "S3 peer classes";
  config.manufacturer = "EspUsbDevice";
  config.serialNumber = "S3PEERCLS";
  if (!device.begin(config))
    Serial.printf("S3D BEGIN_FAILED %s\n", device.lastErrorName());
  Serial.println("S3D BOOTED");
}

void loop()
{
  EspUsbDeviceMidiPacket p;
  while (midi.readPacket(p))
  {
    p.byte2 = (uint8_t)(p.byte2 + 12);
    midi.writePacket(p);
  }
  uint8_t buf[64];
  while (vendor.available())
  {
    const size_t n = vendor.read(buf, sizeof buf);
    vendor.write(buf, n);
    vendor.flush();
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
