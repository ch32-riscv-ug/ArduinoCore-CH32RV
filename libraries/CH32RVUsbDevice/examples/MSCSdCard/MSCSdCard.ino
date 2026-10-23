// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

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

CH32RVUsbDevice device;
CH32RVUsbDeviceMsc msc(device);
CH32RVUsbDeviceMscSdCard sdMsc;

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, -1);
  // en: Initialize the physical medium before attaching MSC and starting USB. / ja: MSCの接続とUSB起動より前に実媒体を初期化する。
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
  // en: Deliver notifications here. / ja: 通知callbackはここで実行する。
  deliverEvents();
  delay(1000);
}
