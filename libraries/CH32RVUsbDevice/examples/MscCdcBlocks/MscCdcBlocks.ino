// en: A raw 8 KB block device (no file system, lost at reset) and, beside it, a serial port that reports how many
//     times the host read and wrote it - once a second, or on ? sent to the port.
// ja: 8 KB の生のブロックの機器（ファイルシステム無し、リセットで消える）と、その横に、ホストが何回読み書きしたかを
//     知らせるシリアルのポート。毎秒、または ? を送ったとき。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USB_DEVICE */

#include <CH32RVUsbDevice.h>
#include <string.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceCdcSerial console(device, "disk statistics");
CH32RVUsbDeviceMsc disk(device);

// en: 8 KB; 4 KB on a part with less than 16 KB of RAM (CH32RV_RAM_SIZE): 10 KB does not hold 8 beside the serial
//     port's buffers.
// ja: 8 KB。RAM が 16 KB 未満の品（CH32RV_RAM_SIZE）は 4 KB（10 KB では、シリアルのバッファと並べて 8 KB は入らない）。
#if CH32RV_RAM_SIZE < 16 * 1024
static constexpr uint32_t BLOCK_BYTES = 512, BLOCK_COUNT = 8;
#else
static constexpr uint32_t BLOCK_BYTES = 512, BLOCK_COUNT = 16;
#endif
static uint8_t storage[BLOCK_BYTES * BLOCK_COUNT];
static volatile uint32_t reads, writes;

static bool inRange(uint32_t lba, uint32_t offset, uint32_t bytes)
{
  return lba < BLOCK_COUNT && offset < BLOCK_BYTES && bytes <= sizeof(storage) - (lba * BLOCK_BYTES + offset);
}

// en: Called while the host waits for the answer (USB processing): keep them short.
// ja: ホストが答えを待つ間（USB の処理の中）に呼ばれる。短く。
static int32_t readBlocks(uint32_t lba, uint32_t offset, void *data, uint32_t bytes)
{
  if (!inRange(lba, offset, bytes)) {
    return -1;
  }
  memcpy(data, storage + lba * BLOCK_BYTES + offset, bytes);
  reads = reads + 1;
  return int32_t(bytes);
}

static int32_t writeBlocks(uint32_t lba, uint32_t offset, uint8_t *data, uint32_t bytes)
{
  if (!inRange(lba, offset, bytes)) {
    return -1;
  }
  memcpy(storage + lba * BLOCK_BYTES + offset, data, bytes);
  writes = writes + 1;
  return int32_t(bytes);
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  disk.onRead(readBlocks);
  disk.onWrite(writeBlocks);
  disk.vendorID("CH32RV");
  disk.productID("RAM blocks");
  disk.mediaPresent(true);
  CH32RVUsbDeviceConfig identity;
  identity.product = "RAM blocks and console";
  if (!disk.begin(BLOCK_COUNT, BLOCK_BYTES) || !device.begin(identity)) {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
  }
}

void loop()
{
  if (!console.connected()) {
    return;
  }
  bool asked = false;
  while (console.available()) {
    asked |= console.read() == '?';
  }
  static uint32_t last;
  if (asked || millis() - last >= 1000) {
    last = millis();
    // en: Calls of the storage callbacks, not USB packets or bytes.
    // ja: 読み書きの関数が呼ばれた回数。USB の packet やバイトの数ではない。
    console.printf("reads=%lu writes=%lu\r\n", (unsigned long)reads, (unsigned long)writes);
  }
}
