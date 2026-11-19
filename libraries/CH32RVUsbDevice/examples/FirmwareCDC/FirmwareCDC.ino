// en: Receive a new sketch over a USB serial port and install it:
//       host "FW <size>\n" -> device "READY\n" -> <size> bytes -> device "OK\n" -> restart into it
//     The PC side is firmware_cdc.py. The image is checked before it replaces this sketch.
// ja: USB のシリアルのポートで新しいスケッチを受け取り、入れ替える:
//       ホスト "FW <大きさ>\n" -> デバイス "READY\n" -> その数のバイト -> デバイス "OK\n" -> 入れ替えて起動
//     PC の側は firmware_cdc.py。イメージは、このスケッチと入れ替える前に確かめる。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USB_DEVICE */

#include <CH32RV.h>
#include <CH32RVUsbDevice.h>
#include <Update.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceCdcSerial port(device, "Firmware");

static size_t expected, received;
static char command[32];
static uint8_t commandLength;

static void fail(const char *reason)
{
  Update.abort();
  expected = received = 0;
  commandLength = 0;
  port.printf("ERR %s\n", reason);
  port.flush();
  Serial.printf("Update failed: %s\n", reason);
}

// en: The "FW <size>" line, a byte at a time.
// ja: "FW <大きさ>" の行を 1 バイトずつ。
static void readCommand()
{
  while (port.available() > 0) {
    const int byte = port.read();
    if (byte < 0) {
      return;
    }
    if (byte != '\n' && byte != '\r') {
      if (commandLength < sizeof(command) - 1) {
        command[commandLength++] = char(byte);
      }
      continue;
    }
    if (commandLength == 0) {
      continue;
    }
    command[commandLength] = '\0';
    commandLength = 0;
    unsigned long size = 0;
    if (sscanf(command, "FW %lu", &size) != 1 || size == 0) {
      fail("expected FW <size>");
      return;
    }
    // en: The real size first: an image too big is refused before the host sends a byte of it.
    // ja: 先に本当の大きさ: 大きすぎるイメージは、ホストが 1 バイトも送る前に断る。
    if (!Update.begin(size)) {
      fail(Update.errorString());
      return;
    }
    expected = size;
    received = 0;
    Serial.printf("Receiving %lu bytes (up to %u)\n", size, unsigned(Update.capacity()));
    port.print("READY\n");
    port.flush();
    return;
  }
}

// en: The image, from loop(): a flash erase takes milliseconds, and while it runs the receive ring fills and the
//     host waits (NAK) - nothing is lost.
// ja: イメージは loop() で。flash の消去は数 ms かかり、その間は受信の輪が埋まってホストが待つ（NAK）ので、失われない。
static void readImage()
{
  static uint8_t buffer[256];
  while (port.available() > 0 && received < expected) {
    const size_t want = min(expected - received, sizeof(buffer));
    const size_t got = port.read(buffer, want);
    if (got == 0) {
      return;
    }
    if (Update.write(buffer, got) != got) {
      fail(Update.errorString());
      return;
    }
    received += got;
  }
  if (received < expected) {
    return;
  }
  // en: end() checks the image before it is allowed to replace this sketch.
  // ja: end() はイメージを確かめてから、このスケッチと入れ替えられる状態にする。
  if (!Update.end()) {
    fail(Update.errorString());
    return;
  }
  expected = 0;
  port.print("OK\n");
  port.flush();
  Serial.println("Verified - restarting into the new sketch");
  delay(100);                     // en: let the host read the reply / ja: ホストが返事を読む間
  CH32RV.restart();
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  CH32RVUsbBulkStreamConfig serialConfig;
  serialConfig.rxCapacity = 2048;   // en: keeps USB moving while a page is erased / ja: 消去の間も USB を止めない
  serialConfig.txCapacity = 128;    // en: only short replies go back / ja: 返すのは短い返事だけ
  if (!port.configure(serialConfig) || !device.begin()) {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.printf("Ready: up to %u bytes. python3 firmware_cdc.py <port> <sketch.bin>\n", unsigned(Update.capacity()));
}

void loop()
{
  if (expected == 0) {
    readCommand();
  } else {
    readImage();
  }
}
