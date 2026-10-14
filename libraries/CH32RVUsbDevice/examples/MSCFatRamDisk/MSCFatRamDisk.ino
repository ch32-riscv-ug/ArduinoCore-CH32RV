/* MSCFatRamDisk - ported from EspUsbDevice's example of the same name (renamed
 * EspUsbDevice -> CH32RVUsbDevice, ESP_USB_ -> CH32RV_USB_). Runs on CH32X035/X033.
 * docs/usb.ja.md describes the library.
 * The disk is 10 KB of RAM (EspUsbDevice's is 64 KB; X035 has 20 KB).
 * requires: ram=16K */
#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceMsc msc(device);

// Keep the example small enough for the ESP32-S2's internal DRAM after the
// library-owned TinyUSB class buffers are linked. Larger RAM disks should use
// PSRAM explicitly; persistent or substantially larger media should use SD.
static uint8_t storage[10 * 1024];
CH32RVUsbDeviceMscFatRamDisk disk(storage, sizeof(storage));

static bool scanRequested = false;

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  delay(1500);

  if (!disk.format("CH32RV"))
  {
    Serial.println("FAT_FORMAT_FAILED");
    return;
  }

  disk.addTextFile("README.TXT",
                   "CH32RVUsbDevice FAT RAM disk\r\n"
                   "\r\n"
                   "Copy CONFIG.TXT here, eject the drive, and watch Serial.\r\n");

  disk.onEject([]()
               {
                 scanRequested = true;
                 Serial.println("MSC_EJECT");
               });

  msc.vendorID("CH32RV");
  msc.productID("FAT RAM DISK");
  msc.productRevision("1.0");
  msc.mediaPresent(true);
  msc.isWritable(true);

  if (!disk.attach(msc))
  {
    Serial.println("MSC_ATTACH_FAILED");
    return;
  }

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;   /* pid.codes test ID: a product needs its own */
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice FAT RAM Disk";
  config.serialNumber = "ch32rv-fat-ramdisk";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.printf("USB FAT RAM disk ready blocks=%lu bytes=%u\n",
                static_cast<unsigned long>(disk.blockCount()),
                static_cast<unsigned>(disk.byteSize()));
}

void loop()
{
  if (!scanRequested)
  {
    delay(10);
    return;
  }
  scanRequested = false;

  if (!disk.exists("CONFIG.TXT"))
  {
    Serial.println("CONFIG_NOT_FOUND");
    return;
  }

  const size_t size = disk.fileSize("CONFIG.TXT");
  static uint8_t buffer[256];
  const size_t readSize = disk.readFile("CONFIG.TXT", buffer, min(size, sizeof(buffer) - 1));
  buffer[readSize] = '\0';

  Serial.printf("CONFIG_SIZE %u\n", static_cast<unsigned>(size));
  Serial.printf("CONFIG_BEGIN\n%s\nCONFIG_END\n", reinterpret_cast<char *>(buffer));
}
