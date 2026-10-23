// en: Echo USB vendor bulk data and answer custom control requests.
// ja: USBベンダーbulk通信をエコーし、独自の制御要求に応答する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVUsbDevice.h>

// en: The device groups USB settings; the objects declared below add functions for the host.
// ja: deviceがUSB全体をまとめ、下のオブジェクト宣言でホストに見せる機能を追加する。
CH32RVUsbDevice device;
CH32RVUsbDeviceVendor UsbVendor(device);

static uint32_t lastStatusMs = 0;
static uint32_t rxCount = 0;
static uint32_t controlCount = 0;

static void printHex(const uint8_t *data, size_t length)
{
  for (size_t i = 0; i < length; i++)
  {
    if (i)
    {
      Serial.print(' ');
    }
    Serial.printf("%02x", data[i]);
  }
}

void setup()
{
  // en: This Serial is hardware UART for diagnostics, separate from the USB connection being demonstrated.
  // ja: このSerialはログ用のハードウェアUART。ここで試すUSB通信の接続とは別。
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  delay(1500);

  UsbVendor.onRx([](size_t available)
                 {
                   uint8_t buffer[64];
                   while (available > 0)
                   {
                     // en: Read no more than fits in buffer; use the returned chunk length, not the requested amount.
                     // ja: 配列に収まる分だけ要求し、要求量ではなく実際に読めたchunk長を使う。
                     const size_t chunk = UsbVendor.read(buffer, min(available, sizeof(buffer)));
                     if (chunk == 0)
                     {
                       break;
                     }
                     rxCount += chunk;
                     Serial.print("VENDOR_RX ");
                     Serial.print(chunk);
                     Serial.print(" ");
                     printHex(buffer, chunk);
                     Serial.println();

                     // en: This demo omits write-length checks; retain unsent bytes when adapting it.
                     // ja: この例は送信長の確認を省略。応用時は未送信分を保持する。
                     UsbVendor.write(reinterpret_cast<const uint8_t *>("echo: "), 6);
                     UsbVendor.write(buffer, chunk);
                     UsbVendor.write(reinterpret_cast<const uint8_t *>("\r\n"), 2);
                     UsbVendor.flush();
                     available = UsbVendor.available();
                   }
                 });

  // en: Control requests need an immediate provider response; keep this handler short.
  // ja: 制御要求はその場で応答を用意するprovider。短く処理する。
  UsbVendor.onControlRequest([](const CH32RVUsbDeviceVendorControlRequest &request)
                             {
                               controlCount++;
                               Serial.printf("VENDOR_CONTROL stage=%u type=0x%02x request=0x%02x value=%u index=%u length=%u\n",
                                             request.stage,
                                             request.bmRequestType,
                                             request.bRequest,
                                             request.wValue,
                                             request.wIndex,
                                             request.wLength);

                               static const char info[] = "CH32RVUsbDeviceVendor";
                               // en: Bit 7 selects device-to-host direction; cap the reply at the length requested by the host.
                               // ja: bit 7がDeviceからHostへの方向を示す。返信長はホストの要求長を超えないようにする。
                               if ((request.bmRequestType & 0x80) && request.bRequest == 0x01)
                               {
                                 return UsbVendor.sendControlResponse(request, info, min(static_cast<size_t>(request.wLength), sizeof(info) - 1));
                               }
                               if (!(request.bmRequestType & 0x80) && request.bRequest == 0x02)
                               {
                                 return UsbVendor.sendControlResponse(request);
                               }
                               return false;
                             });

  CH32RVUsbDeviceConfig config;
  config.vid = 0x1209;
  config.pid = 0x0001;
  config.manufacturer = "ch32-riscv-ug";
  config.product = "CH32RVUsbDevice USBVendor";
  config.serialNumber = "ch32rv-vendor";
  config.webusbEnabled = true;
  config.webusbUrl = "example.com/ch32rvusbdevice";

  // en: Start all configured functions together; success starts the stack, while the host configures it afterward.
  // ja: 設定した機能をまとめて開始する。成功は内部の開始を示し、ホストの認識はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB vendor device ready");
}

void loop()
{
  // en: Run registered notification handlers here; USB transfer processing runs separately.
  // ja: 登録した通知関数をここで実行する。USB転送そのものの処理とは別。
  deliverEvents();
  const uint32_t now = millis();
  if (UsbVendor.mounted() && now - lastStatusMs >= 3000)
  {
    lastStatusMs = now;
    UsbVendor.printf("status rx=%lu control=%lu\r\n",
                     static_cast<unsigned long>(rxCount),
                     static_cast<unsigned long>(controlCount));
    UsbVendor.flush();
  }
  delay(1);
}
