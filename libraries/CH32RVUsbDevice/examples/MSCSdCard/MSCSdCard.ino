// en: Expose an SPI SD card as a USB mass-storage device.
// ja: SPI接続のSDカードをUSBストレージとして公開する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: USBFS */

#include <SPI.h>
#include <CH32RVUsbDevice.h>
#include <CH32RVUsbDeviceMscSdCard.h>

static const int SCK_PIN = SCK;
static const int MISO_PIN = MISO;
static const int MOSI_PIN = MOSI;
#ifdef PA4
static const int CS_PIN = PA4;
#else
static const int CS_PIN = PC4;
#endif

// en: The device groups USB settings; the objects declared below add functions for the host.
// ja: deviceがUSB全体をまとめ、下のオブジェクト宣言でホストに見せる機能を追加する。
CH32RVUsbDevice device;
CH32RVUsbDeviceMsc msc(device);
CH32RVUsbDeviceMscSdCard sdMsc;

void setup()
{
  // en: This Serial is hardware UART for diagnostics, separate from the USB connection being demonstrated.
  // ja: このSerialはログ用のハードウェアUART。ここで試すUSB通信の接続とは別。
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  // en: Open the SPI bus first; SD initialization uses it, and MSC later exposes the card blocks.
  // ja: SPIを先に開始し、そのバスでSDを初期化する。最後にMSCがカードのblockを公開する。
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, -1);
  // en: Initialize the physical medium before attaching MSC and starting USB.
  // ja: MSCの接続とUSB起動より前に実媒体を初期化する。
  if (!sdMsc.begin(CS_PIN, SPI, 4000000))
  {
    Serial.println("SD_BEGIN_FAILED");
    return;
  }

  sdMsc.onEject([]()
                { Serial.println("SD_EJECT"); });

  msc.vendorID("CH32RV");
  msc.productID("SD MSC");
  msc.productRevision("1.0");
  msc.mediaPresent(true);
  msc.isWritable(true);

  if (!sdMsc.attach(msc))
  {
    Serial.println("MSC_ATTACH_FAILED");
    return;
  }

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice SD MSC";
  config.serialNumber = "ch32rv-sd-msc";

  // en: Start all configured functions together; success starts the stack, while the host configures it afterward.
  // ja: 設定した機能をまとめて開始する。成功は内部の開始を示し、ホストの認識はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.printf("USB SD MSC ready blocks=%lu block_size=%u\n",
                static_cast<unsigned long>(sdMsc.blockCount()),
                sdMsc.blockSize());
}

void loop()
{
  // en: Run registered notification handlers here; USB transfer processing runs separately.
  // ja: 登録した通知関数をここで実行する。USB転送そのものの処理とは別。
  deliverEvents();
  delay(1000);
}
