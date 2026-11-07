// en: One FAT volume for the sketch and a PC: the sketch writes files, the PC sees them as a drive, and after the PC ejects it the sketch lists what the PC left.
// ja: スケッチとPCで1つのFATボリュームを使う。スケッチがファイルを書き、PCはドライブとして見て、PCが取り外したらスケッチがPCの残したものを一覧する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USB_DEVICE, ram=48K */

#include <CH32RVUsbDevice.h>
#include <CH32RVUsbDeviceMscBlock.h>
#include <FatFS.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceMsc msc(device);

// en: 64 blocks (32 KB) is the smallest FAT volume FatFs makes.
// ja: 64ブロック（32 KB）がFatFsで作れる最小のFATボリューム。
static uint8_t storage[64 * 512];
fs::RamBlockDevice disk(storage, sizeof(storage) / 512);
CH32RVUsbDeviceMscBlockStorage shared(disk);
FatFS fat;

static bool ejected = false;
static unsigned rounds = 0;

static void listRoot()
{
  File root = fat.open("/");
  for (File e = root.openNextFile(); e; e = root.openNextFile())
  {
    Serial.printf("  %s%s  %u\n", e.name(), e.isDirectory() ? "/" : "", static_cast<unsigned>(e.size()));
  }
}

// en: The sketch has the volume only while the PC does not: mount, write, unmount, then show it.
// ja: スケッチがボリュームを持つのはPCが持っていない間だけ。マウントして書き、外してから見せる。
static void writeAndShow()
{
  if (!fat.begin(disk, true))
  {
    Serial.println("MOUNT_FAILED");
    return;
  }
  Serial.println("FILES");
  listRoot();
  File f = fat.open("/FROM_CH32.TXT", FILE_APPEND);
  f.printf("round %u: %lu ms after reset\r\n", rounds++, millis());
  f.close();
  fat.end();
  msc.mediaPresent(true);
  Serial.println("SHOWN_TO_PC");
}

void setup()
{
  // en: This Serial is hardware UART for diagnostics, separate from the USB connection being demonstrated.
  // ja: このSerialはログ用のハードウェアUART。ここで試すUSB通信の接続とは別。
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  shared.onEject([]()
                 {
                   // en: Only note it here; take the volume back in loop().
                   // ja: ここでは記録だけ。ボリュームはloop()で取り戻す。
                   ejected = true;
                 });
  msc.vendorID("CH32RV");
  msc.productID("FS RAM DISK");
  msc.productRevision("1.0");
  if (!shared.attach(msc))
  {
    Serial.println("MSC_ATTACH_FAILED");
    return;
  }

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RV FS RAM Disk";
  config.serialNumber = "ch32rv-fs-ramdisk";
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  writeAndShow();
}

void loop()
{
  // en: Run the eject notification registered above.
  // ja: 上で登録した取り外しの通知をここで実行する。
  deliverEvents();
  if (!ejected)
  {
    delay(10);
    return;
  }
  ejected = false;
  // en: The PC let go: take the medium away from it before mounting.
  // ja: PCが手放したので、マウントする前に媒体をPCから外す。
  msc.mediaPresent(false);
  Serial.println("EJECTED");
  writeAndShow();
}
