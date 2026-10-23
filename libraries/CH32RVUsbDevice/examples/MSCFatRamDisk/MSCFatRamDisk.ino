// en: Expose a FAT RAM disk and read a PC-written file after eject.
// ja: FAT RAMディスクを公開し、取り外し後にPCが書いたファイルを読む。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: ram=16K */

#include <CH32RVUsbDevice.h>

// en: The device groups USB settings; the objects declared below add functions for the host.
// ja: deviceがUSB全体をまとめ、下のオブジェクト宣言でホストに見せる機能を追加する。
CH32RVUsbDevice device;
CH32RVUsbDeviceMsc msc(device);

// en: Keep the backing array alive for the entire exposed-medium lifetime.
// ja: 媒体として公開している間、保存先の配列を有効に保つ。
static uint8_t storage[10 * 1024];
CH32RVUsbDeviceMscFatRamDisk disk(storage, sizeof(storage));

static bool scanRequested = false;

void setup()
{
  // en: This Serial is hardware UART for diagnostics, separate from the USB connection being demonstrated.
  // ja: このSerialはログ用のハードウェアUART。ここで試すUSB通信の接続とは別。
  Serial.begin(115200, SERIAL_8N1, RX, TX);
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
                 // en: The eject notification schedules a scan; read files later in loop().
                 // ja: 取り外し通知では走査を予約し、ファイルは後でloop()から読む。
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
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice FAT RAM Disk";
  config.serialNumber = "ch32rv-fat-ramdisk";

  // en: Start all configured functions together; success starts the stack, while the host configures it afterward.
  // ja: 設定した機能をまとめて開始する。成功は内部の開始を示し、ホストの認識はその後に進む。
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
  // en: Run registered notification handlers here; USB transfer processing runs separately.
  // ja: 登録した通知関数をここで実行する。USB転送そのものの処理とは別。
  deliverEvents();
  // en: Wait for the host-eject signal before inspecting the file, rather than racing host writes.
  // ja: ホストの書き込みと競合しないよう、取り外しの合図を受けてからファイルを読む。
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
  // en: Reserve and append a terminator after the actual read length.
  // ja: 終端分を確保し、実際に読めた長さの位置へ終端を置く。
  buffer[readSize] = '\0';

  Serial.printf("CONFIG_SIZE %u\n", static_cast<unsigned>(size));
  Serial.printf("CONFIG_BEGIN\n%s\nCONFIG_END\n", reinterpret_cast<char *>(buffer));
}
