// en: A vendor bulk echo that never loses a byte, paired with vendor_bulk_peer.py on the PC. It reads only as much as
// en: the bulk IN side can take right now, so when the PC stops reading, the device stops reading too and the PC's
// en: writes wait - back-pressure instead of dropped data. The UART shows the bytes moved and how often IN was full.
// ja: 1 バイトも失わないベンダーの bulk のエコー。PC の vendor_bulk_peer.py と組む。bulk IN がその時に受けられる分だけ
// ja: 読むので、PC が読むのをやめるとデバイスも読まなくなり、PC の書き込みが待つ（捨てる代わりに押し戻す）。UART には
// ja: 動かしたバイト数と、IN が満杯だった回数を出す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USB_DEVICE */

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceVendor vendor(device);

static uint32_t moved, inFull, lastReport;

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(500);
  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RV VendorBulkPeer";
  config.serialNumber = "ch32rv-vbpeer";
  if (!device.begin(config)) {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("VendorBulkPeer ready: run vendor_bulk_peer.py on the PC");
}

void loop()
{
  // en: Read no more than the IN side takes now: what is not read stays in the OUT endpoint, and the host waits.
  // ja: IN 側がいま受けられる分しか読まない。読まない分は OUT の端点に残り、ホストが待つ。
  const size_t room = vendor.writeAvailable();
  const size_t waiting = (size_t)vendor.available();
  if (waiting && room == 0) {
    inFull++;
  }
  size_t n = waiting < room ? waiting : room;
  uint8_t buffer[64];
  while (n) {
    const size_t chunk = vendor.read(buffer, n < sizeof buffer ? n : sizeof buffer);
    if (chunk == 0) {
      break;
    }
    // en: write() takes all of it: no more than writeAvailable() was read.
    // ja: write() は全部受ける（writeAvailable() 以下しか読んでいない）。
    vendor.write(buffer, chunk);
    moved += chunk;
    n -= chunk;
  }
  vendor.flush();

  if (millis() - lastReport >= 1000) {
    lastReport = millis();
    Serial.printf("moved %lu bytes, IN full %lu times, mounted %u\n", (unsigned long)moved, (unsigned long)inFull,
                  vendor.mounted());
  }
}
