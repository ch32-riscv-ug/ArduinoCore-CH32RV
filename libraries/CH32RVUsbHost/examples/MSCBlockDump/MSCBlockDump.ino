// en: Read USB storage identity and capacity, then dump its first block.
// ja: USBストレージの識別情報と容量を読み、先頭ブロックを表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USBFS */

#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;
static uint8_t block[512];
static bool dumped;

void setup() {
  Serial.begin(115200);
  usb.onDeviceDisconnected([](const CH32RVUsbHostDeviceInfo &) { dumped = false; });
  // en: Start the host controller; connected peripherals are discovered later through USB processing.
  // ja: Host controllerを開始する。接続機器は、その後のUSB処理で検出・列挙される。
  if (!usb.begin()) {
    Serial.print("usb.begin() failed: ");
    Serial.println(usb.lastErrorName());
  }
}

void loop() {
  // en: Run registered notification handlers here; USB transfer processing runs separately.
  // ja: 登録した通知関数をここで実行する。USB転送そのものの処理とは別。
  deliverEvents();
  if (dumped || !usb.mscReady()) {
    return;
  }
  // en: Mark this device as already attempted so loop() does not dump its block repeatedly.
  // ja: 毎回同じblockを表示し続けないよう、この接続では試行済みと覚える。
  dumped = true;
  CH32RVUsbHostMscInquiry inquiry;
  uint32_t blocks = 0, size = 0;
  if (!usb.mscWaitReady() || !usb.mscInquiry(inquiry) || !usb.mscCapacity(blocks, size)) {
    Serial.print("not ready: ");
    Serial.println(usb.lastErrorName());
    return;
  }
  Serial.print(inquiry.vendor);
  Serial.print(' ');
  Serial.print(inquiry.product);
  Serial.print(": ");
  Serial.print(blocks);
  Serial.print(" blocks of ");
  Serial.println(size);
  // en: Read only when the reported block fits this destination.
  // ja: 報告されたblockが保存先に収まる場合だけ読む。
  if (size == sizeof block && usb.mscReadBlocks(0, block, 1)) {
    for (size_t i = 0; i < sizeof block; i += 16) {
      ch32rvUsbHostPrintHex(block + i, 16);
      Serial.println();
    }
  }
}
