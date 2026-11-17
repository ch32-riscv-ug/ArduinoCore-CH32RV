// en: A USB drive to copy a new firmware onto (.uf2, or the .bin in order); the sketch restarts into it.
// ja: 新しいファームウェア（.uf2、または順に書かれる .bin）をコピーする USB ドライブ。コピーが終わるとそれで再起動する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USB_DEVICE, ram=16K */

#include <CH32RVUsbDevice.h>

CH32RVUsbDeviceMsc msc;
// en: Boot sector, FATs, root directory and room for the host's own files; the image goes to flash, not here.
// ja: ブートセクタ、FAT、ルートディレクトリと、ホストが置くファイルの場所。イメージは RAM ではなく flash へ行く。
static uint8_t storage[8 * 1024];
CH32RVUsbDeviceMscFirmwareDisk disk(storage, sizeof storage);

static volatile size_t received;
static volatile uint8_t failure;

void setup() {
  Serial.begin(115200);
  if (!disk.begin("FIRMWARE")) {
    Serial.println("no room to stage an update: the sketch is over half the flash");
    return;
  }
  disk.addTextFile("README.TXT", "Copy a .uf2 (or the sketch's .bin) here.\r\n");
  disk.onProgress([](size_t n) { received = n; });
  disk.onError([](uint8_t error) { failure = error; });
  msc.vendorID("CH32RV");
  msc.productID("Firmware");
  disk.attach(msc);

  CH32RVUsbDeviceConfig config;
  config.product = "CH32RV FirmwareMSC";
  USBDevice.begin(config);
  Serial.print("UF2 family ID 0x");
  Serial.println(CH32RVUsbDeviceMscFirmwareDisk::uf2FamilyId(), HEX);
}

void loop() {
  static size_t shown;
  static uint8_t failed;
  if (received != shown) {
    shown = received;
    Serial.print("received ");
    Serial.println((unsigned long)shown);
  }
  if (failure != failed) {
    failed = failure;
    Serial.print("update error ");
    Serial.println(failed);
  }
  delay(100);
}
