// en: Stop the USB device and start it again with other buffer sizes at run time: send r on the UART to switch the
//     vendor function's rings between 128 and 2048 bytes. The host sees the device leave and come back. Meanwhile
//     the vendor bulk data is echoed, nothing dropped.
// ja: 動いている途中で USB デバイスを止め、別のバッファの大きさで始め直す: UART に r を送ると、ベンダーの機能の輪を
//     128 と 2048 バイトで切り替える。ホストには機器が抜けて戻ったように見える。その間ベンダーの bulk のデータは
//     捨てずにエコーする。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USB_DEVICE */

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceVendor vendor(device);
static size_t capacity = 128;
static uint8_t pending[64];
static size_t length, sent;

static bool start(size_t bytes)
{
  CH32RVUsbBulkStreamConfig buffers;
  buffers.rxCapacity = buffers.txCapacity = bytes;
  // en: configure() works only while the device is stopped.
  // ja: configure() は止まっている間だけ効く。
  if (!vendor.configure(buffers) || !device.begin()) {
    Serial.printf("start failed: %s\n", device.lastErrorName());
    return false;
  }
  Serial.printf("Started: RX=%u TX=%u\n", unsigned(vendor.readCapacity()), unsigned(vendor.writeCapacity()));
  return true;
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  start(capacity);
  Serial.println("Send r on the UART to switch 128 / 2048-byte rings.");
}

void loop()
{
  if (Serial.available() && Serial.read() == 'r') {
    // en: The host sees a disconnect and a new enumeration; what was in the rings is dropped.
    // ja: ホストには切断と列挙のやり直しに見える。輪に残っていたものは捨てる。
    device.end();
    length = sent = 0;
    capacity = capacity == 128 ? 2048 : 128;
    start(capacity);
  }
  if (!device.ready()) {
    length = sent = 0;
    return;
  }
  if (!length) {
    length = vendor.read(pending, sizeof(pending));
    sent = 0;
  }
  if (length) {
    sent += vendor.write(pending + sent, length - sent);
    if (sent == length) {
      length = sent = 0;
    }
  }
}
