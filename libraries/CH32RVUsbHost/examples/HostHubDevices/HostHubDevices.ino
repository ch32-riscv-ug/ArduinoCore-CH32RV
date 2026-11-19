// en: Devices behind a hub: on each connection or disconnection, the whole tree is listed - every device with its
// en: address, the hub and port it hangs on, its speed and whether a driver took it; a hub shows its port count.
// en: Unplugging one device leaves the others listed and working.
// ja: ハブの先の機器: つないだり抜いたりするたびに、木の全体を表示する。機器ごとにアドレス、つながっているハブと口、
// ja: 速さ、ドライバーが付いたか。ハブは口の数も。1 台を抜いても、ほかは一覧に残って動き続ける。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USBFS */

#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;
static bool changed;

static const char *speedName(uint8_t speed)
{
  switch (speed) {
  case CH32RV_USB_HOST_SPEED_LOW: return "low";
  case CH32RV_USB_HOST_SPEED_FULL: return "full";
  case CH32RV_USB_HOST_SPEED_HIGH: return "high";
  default: return "super";
  }
}

static void listTree()
{
  CH32RVUsbHostDeviceInfo devices[8];
  const size_t n = usb.getDevices(devices, 8);
  Serial.printf("%u device(s)\n", (unsigned)n);
  for (size_t i = 0; i < n; i++) {
    const CH32RVUsbHostDeviceInfo &d = devices[i];
    Serial.printf("  dev %u %04x:%04x %s speed, ", d.address, d.vid, d.pid, speedName(d.speed));
    if (d.parentAddress == 0) {
      Serial.print("on the root port");
    } else {
      // en: portId is (hub address << 4) | port behind a hub.
      // ja: ハブの先の portId は (ハブのアドレス << 4) | 口。
      Serial.printf("on hub %u port %u", d.parentAddress, d.portId & 0x0f);
    }
    if (d.isHub) {
      CH32RVUsbHostHubInfo hub;
      if (usb.getHubInfo(d.address, hub)) {
        Serial.printf(", a hub with %u ports", hub.portCount);
      }
    } else {
      Serial.print(d.supported ? ", driver attached" : ", no driver");
    }
    Serial.printf(" (%s)\n", d.product[0] ? d.product : "no name");
  }
}

void setup()
{
  Serial.begin(115200);
  usb.onDeviceConnected([](const CH32RVUsbHostDeviceInfo &d) {
    Serial.printf("connected: dev %u %04x:%04x\n", d.address, d.vid, d.pid);
    changed = true;
  });
  usb.onDeviceDisconnected([](const CH32RVUsbHostDeviceInfo &d) {
    Serial.printf("disconnected: dev %u %04x:%04x\n", d.address, d.vid, d.pid);
    changed = true;
  });
  if (!usb.begin()) {
    Serial.print("usb.begin() failed: ");
    Serial.println(usb.lastErrorName());
  }
}

void loop()
{
  deliverEvents();
  if (changed) {
    changed = false;
    listTree();
  }
}
