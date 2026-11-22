/* The ESP32-S3 (or the ESP32-P4 on its OTG HS port) as a USB network device
 * for tests/peer/usb/host/net: a CDC-NCM function (EspUsbDeviceNet) as raw
 * Ethernet frames, no IP stack. The DUT (CH32RVUsbHost) is the host.
 *
 * Commands, one per line on Serial; answers start "S3D ":
 *   ?                    S3D READY <mounted 0/1> <link 0/1>
 *   netsend <n>          S3D NETSENT <frames sent>   (broadcast, EtherType 0x88B5, the index in the payload)
 *   netstat              S3D NETRX <frames> <frames of 0x88B5>
 *   netmac               S3D NETMAC <its MAC, 12 hex>
 *   ipstart              S3D IPSTART <ok> <address>   (the IP stack: 192.168.7.1, a DHCP server, a TCP echo on 7;
 *                        the raw frames above are not counted after it) */
#include <Arduino.h>
#include <EspUsbDevice.h>
#include <Network.h>

EspUsbDevice device;
EspUsbDeviceNet net(device);

static volatile uint32_t frames, ours;
static NetworkServer echo(7);
static NetworkClient echoClient;
static bool ipUp;
static char line[120];
static size_t used;

static void run(char *cmd)
{
  char *arg = strchr(cmd, ' ');
  if (arg) *arg++ = '\0';
  else arg = cmd + strlen(cmd);
  if (!strcmp(cmd, "?")) Serial.printf("S3D READY %u %u\n", device.ready() ? 1 : 0, net.linkUp() ? 1 : 0);
  else if (!strcmp(cmd, "netsend"))
  {
    unsigned long n = strtoul(arg, nullptr, 10);
    if (!n) n = 1;
    unsigned long sent = 0;
    for (unsigned long i = 0; i < n; i++)
    {
      uint8_t f[60] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x50, 0x45, 0x45, 0x52, 0x01, 0x88, 0xB5};
      f[14] = (uint8_t)i;
      f[15] = (uint8_t)(i >> 8);
      const uint32_t t0 = millis();
      bool ok;
      while (!(ok = net.sendFrame(f, sizeof f)) && millis() - t0 < 500) delay(1);   /* every transmit buffer out */
      sent += ok ? 1 : 0;
    }
    Serial.printf("S3D NETSENT %lu\n", sent);
  }
  else if (!strcmp(cmd, "netstat")) Serial.printf("S3D NETRX %lu %lu\n", (unsigned long)frames, (unsigned long)ours);
  else if (!strcmp(cmd, "netmac"))
  {
    const uint8_t *m = net.macAddress();
    Serial.printf("S3D NETMAC %02X%02X%02X%02X%02X%02X\n", m[0], m[1], m[2], m[3], m[4], m[5]);
  }
  else if (!strcmp(cmd, "ipstart"))
  {
    if (!ipUp) ipUp = net.beginNetwork();
    if (ipUp) echo.begin();
    Serial.printf("S3D IPSTART %u %s\n", ipUp ? 1 : 0, net.localIP().toString().c_str());
  }
  else Serial.printf("S3D UNKNOWN %s\n", cmd);
}

void setup()
{
  Serial.begin(115200);
  EspUsbNetConfig nc;
  nc.name = "S3 peer net";
  if (net.configure(nc) != ESP_OK) Serial.println("S3D NET_CONFIG_FAILED");
  net.ipConfig(IPAddress(192, 168, 7, 1), IPAddress(192, 168, 7, 1), IPAddress(255, 255, 255, 0));
  net.dhcpServer(true);   /* for ipstart; nothing runs until beginNetwork() */
  net.onFrame([](const uint8_t *data, size_t length)
              {
                frames++;
                if (length >= 14 && data[12] == 0x88 && data[13] == 0xB5) ours++; });
  EspUsbDeviceConfig config;
  config.product = "S3 peer net";
  config.manufacturer = "EspUsbDevice";
  config.serialNumber = "S3PEERNET";
#if CONFIG_IDF_TARGET_ESP32P4
  config.controller = EspUsbController::HighSpeed;   /* the OTG HS port, wired to the DUT's USBHS */
#endif
  if (!device.begin(config))
    Serial.printf("S3D BEGIN_FAILED %s\n", device.lastErrorName());
  Serial.println("S3D BOOTED");
}

void loop()
{
  if (ipUp)
  {
    if (!echoClient || !echoClient.connected()) echoClient = echo.accept();
    while (echoClient && echoClient.available())
    {
      uint8_t b[64];
      const int n = echoClient.read(b, sizeof b);
      if (n > 0) echoClient.write(b, n);
    }
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
