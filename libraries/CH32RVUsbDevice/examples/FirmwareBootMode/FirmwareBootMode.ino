// en: Enter the chip's factory USB bootloader from the running sketch (EspUsbDevice's FirmwareBootMode), so the whole
//     flash can be written again with no one touching BOOT: the recovery route behind every other update.
// ja: 動いているスケッチから、石の工場出荷の USB ブートローダーに入る（EspUsbDevice の FirmwareBootMode と同じ）。BOOT に
//     触らずに flash 全体を書き直せる。ほかのすべての入れ替えの道の後ろにある、戻すための道。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USB_DEVICE */

#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceCdcSerial console(device, "Console");

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  // en: Two of the three ways need nothing here: the 1200 bps touch on the CDC port and DFU_DETACH on the DFU runtime
  //     interface (CH32RVUsbDeviceConfig::dfuRuntime, on by default) both end in the bootloader.
  // ja: 3 つのうち 2 つは何も書かなくてよい: CDC のポートの 1200 bps のタッチと、DFU ランタイムのインタフェース
  //     （CH32RVUsbDeviceConfig::dfuRuntime、既定で入る）の DFU_DETACH は、どちらもブートローダーで終わる。
  CH32RVUsbDeviceConfig config;
  config.product = "CH32RV FirmwareBootMode";
  if (!device.begin(config)) {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Ready: 1200 bps touch, dfu-util -e, or 'b' on the Console port");
}

void loop()
{
  // en: The third way: a command of the sketch's own.
  // ja: 3 つ目: スケッチ自身の命令。
  while (console.available() > 0) {
    const int c = console.read();
    if (c == 'b') {
      console.println("entering the bootloader");
      console.flush();
      Serial.println("entering the bootloader");
      delay(50);
      CH32RVUsbDevice::enterBootloader();   // en: does not return / ja: 戻らない
    } else if (c == '?') {
      console.println("b: the bootloader");
    }
  }
}
