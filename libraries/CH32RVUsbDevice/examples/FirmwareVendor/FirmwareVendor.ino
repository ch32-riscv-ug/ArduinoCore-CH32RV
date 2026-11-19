// en: A firmware update over a vendor interface (EspUsbDevice's FirmwareVendor): control requests carry the commands,
//     bulk OUT carries the image, the Update library checks it and installs it. The PC side is firmware_vendor.py.
// ja: ベンダーのインタフェースでファームウェアを入れ替える（EspUsbDevice の FirmwareVendor と同じ形）。命令は制御要求、
//     イメージはバルク OUT で運び、Update ライブラリが確かめて入れる。PC の側は firmware_vendor.py。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USB_DEVICE */

#include <CH32RV.h>
#include <CH32RVUsbDevice.h>
#include <Update.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceVendor vendor(device);

// en: The vendor requests (bmRequestType 0x40 / 0xC0, to the device).
// ja: ベンダーの要求（bmRequestType 0x40 / 0xC0、デバイス宛て）。
enum : uint8_t { REQUEST_START = 1, REQUEST_COMMIT = 2, REQUEST_ABORT = 3, REQUEST_STATUS = 4 };

static volatile bool restartPending;
static uint8_t statusReply[8];

static bool onRequest(const CH32RVUsbDeviceVendorControlRequest &r)
{
  if (r.stage != CH32RV_USB_DEVICE_CONTROL_STAGE_SETUP) {
    return true;
  }
  switch (r.bRequest) {
  case REQUEST_START: {
    // en: The size in wValue (low 16 bits) and wIndex (high): an image that does not fit is refused here, and
    //     STATUS then says inactive with the error.
    // ja: 大きさは wValue（下位 16 ビット）と wIndex（上位）。入らないイメージはここで断り、STATUS が止まっていることと
    //     誤りを返す。
    const size_t size = r.wValue | (size_t)r.wIndex << 16;
    Update.abort();
    if (Update.begin(size)) {
      Serial.printf("Receiving %u bytes (up to %u)\n", unsigned(size), unsigned(Update.capacity()));
    } else {
      Serial.printf("Refused: %s\n", Update.errorString());
    }
    return vendor.sendControlResponse(r);
  }
  case REQUEST_COMMIT:
    // en: end() checks the image before it may replace this sketch; a stall tells the host it did not.
    // ja: end() がイメージを確かめてから、このスケッチと入れ替えられるようにする。だめなら stall でホストに伝える。
    if (!Update.isRunning() || !Update.end()) {
      Serial.printf("Commit refused: %s\n", Update.errorString());
      return false;
    }
    restartPending = true;
    return vendor.sendControlResponse(r);
  case REQUEST_ABORT:
    Update.abort();
    return vendor.sendControlResponse(r);
  case REQUEST_STATUS: {
    // en: [0] receiving, [1..3] the Update library's error, [4..7] bytes written (little endian).
    // ja: [0] 受け取り中、[1..3] Update ライブラリの誤り、[4..7] 書いたバイト数（リトルエンディアン）。
    const uint32_t written = (uint32_t)Update.progress();
    statusReply[0] = Update.isRunning() ? 1 : 0;
    statusReply[1] = Update.getError();
    statusReply[2] = statusReply[3] = 0;
    memcpy(statusReply + 4, &written, 4);
    return vendor.sendControlResponse(r, statusReply, r.wLength < sizeof statusReply ? r.wLength : sizeof statusReply);
  }
  }
  return false;   // en: anything else is stalled / ja: ほかは stall
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  CH32RVUsbBulkStreamConfig bulk;
  bulk.rxCapacity = 2048;   // en: keeps USB moving while a page is erased / ja: 消去の間も USB を止めない
  vendor.configure(bulk);
  vendor.onControlRequest(onRequest);
  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RV FirmwareVendor";
  config.serialNumber = "ch32rv-fwvendor";
  if (!device.begin(config)) {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.printf("Ready: up to %u bytes. python3 firmware_vendor.py <sketch.bin>\n", unsigned(Update.capacity()));
}

void loop()
{
  // en: The image, from loop(): a flash erase takes milliseconds, and meanwhile the FIFO fills and the host waits (NAK).
  // ja: イメージは loop() で書く。flash の消去は数 ms かかり、その間は FIFO が埋まってホストが待つ（NAK）。
  static uint8_t buffer[256];
  while (vendor.available() > 0) {
    const size_t got = vendor.read(buffer, sizeof buffer);
    if (!Update.isRunning()) {
      continue;   // en: no START: dropped / ja: START の前のものは捨てる
    }
    if (Update.write(buffer, got) != got) {
      Serial.printf("Write failed: %s\n", Update.errorString());
      Update.abort();
    }
  }
  if (restartPending) {
    Serial.println("Verified - restarting into the new sketch");
    delay(100);   // en: the status stage of COMMIT goes out first / ja: COMMIT のステータス段を先に返す
    CH32RV.restart();
  }
}
