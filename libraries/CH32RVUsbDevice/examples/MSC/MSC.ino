/* Wiring, operation and expected output: README.md / README.ja.md. */
/* MSC - ported from EspUsbDevice's example of the same name (renamed
 * EspUsbDevice -> CH32RVUsbDevice, ESP_USB_ -> CH32RV_USB_). Controller support and wiring: see the adjacent README files.
 * docs/usb.ja.md describes the library.
 * The disk is 16 blocks of RAM (8 KB).
 * requires: ram=16K */
#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceMsc msc(device);

static constexpr uint32_t BLOCK_COUNT = 16;
static constexpr uint16_t BLOCK_SIZE = 512;
static uint8_t storage[BLOCK_COUNT * BLOCK_SIZE];
CH32RVUsbDeviceMscRamDisk disk(storage, BLOCK_COUNT, BLOCK_SIZE);

static void prepareDisk()
{
  disk.clear();

  // This is enough for low-level SCSI/block access tests. It is not a FAT image.
  disk.writeByte(0, 510, 0x55);
  disk.writeByte(0, 511, 0xaa);
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  delay(1500);

  prepareDisk();

  msc.vendorID("CH32RV");
  msc.productID("MSC RAM DISK");
  msc.productRevision("1.0");
  msc.mediaPresent(true);
  msc.isWritable(true);
  msc.onStartStop([](uint8_t powerCondition, bool start, bool loadEject)
                  {
                    Serial.printf("MSC_START_STOP pc=%u start=%u eject=%u\n",
                                  powerCondition,
                                  start ? 1 : 0,
                                  loadEject ? 1 : 0);
                    return true;
                  });

  if (!disk.attach(msc))
  {
    Serial.println("MSC_ATTACH_FAILED");
    return;
  }

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;   /* pid.codes test ID: a product needs its own */
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice MSC";
  config.serialNumber = "ch32rv-msc";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.printf("USB MSC RAM disk ready blocks=%lu block_size=%u bytes=%u\n",
                static_cast<unsigned long>(disk.blockCount()),
                disk.blockSize(),
                static_cast<unsigned>(disk.byteSize()));
}

void loop()
{
  delay(1000);
}
