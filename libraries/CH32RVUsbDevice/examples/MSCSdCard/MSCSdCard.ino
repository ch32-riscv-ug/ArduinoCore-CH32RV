/* Wiring, operation and expected output: README.md / README.ja.md. */
/* MSCSdCard - ported from EspUsbDevice's example of the same name: an SD
 * card on SPI shows up on the PC as a USB drive.
 *
 * Differences from the original: the card is driven directly in SPI mode (no
 * SD / file system library: the PC mounts the card's file system itself), so
 * there is no mount point, and the SPI bus and its pins are named here.
 *
 * Wiring: an SD card socket (3.3 V) on SPI - SCK_PIN, MISO_PIN, MOSI_PIN and
 * CS_PIN below (this series' default SPI pads; change them to yours) - and the
 * UART log on RX / TX. Runs on the series with a USB device port. */
#include <SPI.h>
#include <CH32RVUsbDevice.h>
#include <CH32RVUsbDeviceMscSdCard.h>

static const int SCK_PIN = SCK;
static const int MISO_PIN = MISO;
static const int MOSI_PIN = MOSI;
#ifdef PA4
static const int CS_PIN = PA4;
#else
static const int CS_PIN = PC4;   /* a series without PA4 (CH32V003) */
#endif

CH32RVUsbDevice device;
CH32RVUsbDeviceMsc msc(device);
CH32RVUsbDeviceMscSdCard sdMsc;

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  delay(1500);

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, -1);
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
  config.vid = 0x1209;   /* pid.codes test ID: a product needs its own */
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
  // The callbacks set in setup() run here, and only here.
  deliverEvents();
  delay(1000);
}
