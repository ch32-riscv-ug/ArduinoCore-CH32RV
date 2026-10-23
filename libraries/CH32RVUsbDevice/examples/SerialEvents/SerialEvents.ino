// en: Show when USB notifications run by pausing their delivery deliberately.
// ja: 通知の受け取りを意図的に止め、USB通知が動く場所を確認する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVUsbDevice.h>

static void onRx(size_t available)
{
  size_t echoed = 0;
  while (SerialUSB.available())
  {
    SerialUSB.write(SerialUSB.read());
    echoed++;
  }
  Serial.printf("RX available=%u echoed=%u\r\n", (unsigned)available, (unsigned)echoed);
}

static void onLineState(const CH32RVUsbDeviceCdcLineState &state)
{
  Serial.printf("LINE_STATE dtr=%u rts=%u\r\n", state.dtr ? 1 : 0, state.rts ? 1 : 0);
}

static void onLineCoding(const CH32RVUsbDeviceCdcLineCoding &coding)
{
  Serial.printf("LINE_CODING baud=%lu\r\n", (unsigned long)coding.baud);
}

void setup()
{
  // en: This Serial is hardware UART for diagnostics, separate from the USB connection being demonstrated.
  // ja: このSerialはログ用のハードウェアUART。ここで試すUSB通信の接続とは別。
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  // en: Register the function to call when receive notification is delivered; this line does not call it yet.
  // ja: 受信通知を届けるときに呼ぶ関数を登録する。この行で関数が実行されるわけではない。
  SerialUSB.onRx(onRx);
  SerialUSB.onLineState(onLineState);
  SerialUSB.onLineCoding(onLineCoding);
  SerialUSB.begin();
  Serial.println("SerialEvents ready");
}

void loop()
{
  // en: Notification callbacks execute here; the return value counts callbacks, not packets.
  // ja: 登録した通知関数をここで実行する。USB転送そのものの処理とは別。戻り値はpacket数ではなくcallback数。
  const size_t ran = deliverEvents();
  if (ran)
  {
    Serial.printf("DELIVER ran=%u\r\n", (unsigned)ran);
  }

  static uint32_t lastBusy = millis();
  if (millis() - lastBusy >= 10000)
  {
    Serial.println("BUSY 3 s: type now, the callbacks wait");
    // en: USB processing continues, but notification delivery waits until loop() resumes.
    // ja: USB処理は進むが、通知はloop()が再開するまで待つ。
    delay(3000);
    Serial.println("BUSY end");
    lastBusy = millis();
  }
}
