// en: A logger over USB CDC that survives the host going away: a reading every 200 ms is kept in a small ring while
// en: no terminal has the port open, and sent oldest first once one opens it again - with how many were lost to the
// en: ring's size. Bus events (reset, configured, suspended, resumed) are logged on the hardware UART.
// ja: ホストがいなくなっても続く USB CDC の記録係。200 ms ごとの値を、端末がポートを開いていない間は小さな輪に貯め、
// ja: 開き直されたら古い順に送る（輪の大きさで失った数も）。バスの出来事（リセット、設定、休止、再開）はハードウェアの
// ja: UART に表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。
/* requires: USB_DEVICE */

#include <CH32RVUsbDevice.h>

static constexpr size_t KEEP = 32;          // en: readings kept while closed / ja: 閉じている間に貯める数
static uint32_t ring[KEEP];
static size_t head, count;
static uint32_t lost, sequence, lastReading;
static bool wasOpen;

static const char *busName(CH32RVUsbBusEvent e)
{
  switch (e) {
  case CH32RVUsbBusEvent::Reset: return "reset";
  case CH32RVUsbBusEvent::Configured: return "configured";
  case CH32RVUsbBusEvent::Unconfigured: return "unconfigured";
  case CH32RVUsbBusEvent::Suspended: return "suspended";
  default: return "resumed";
  }
}

static void keep(uint32_t value)
{
  if (count == KEEP) {
    lost++;                                  // en: the oldest goes / ja: いちばん古いものを捨てる
    head = (head + 1) % KEEP;
    count--;
  }
  ring[(head + count) % KEEP] = value;
  count++;
}

void setup()
{
  // en: The log goes to the hardware UART: the USB port comes and goes.
  // ja: 表示はハードウェアの UART。USB のポートは現れたり消えたりする。
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  USBDevice.onBusEvent([](CH32RVUsbBusEvent e) { Serial.printf("bus: %s\n", busName(e)); });
  // en: Never wait long for a host that stopped reading: 50 ms, then the bytes are dropped like on an open line.
  // ja: 読まなくなったホストを長く待たない: 50 ms で、あとは線が開いたままのときと同じく捨てる。
  SerialUSB.setTxTimeoutMs(50);
  SerialUSB.begin();
  Serial.println("UsbReconnect ready");
}

void loop()
{
  if (millis() - lastReading >= 200) {
    lastReading = millis();
    const uint32_t reading = sequence++;     // en: stands for a measurement / ja: 測った値の代わり
    if (SerialUSB.connected()) {
      SerialUSB.printf("reading %lu\r\n", (unsigned long)reading);
    } else {
      keep(reading);
    }
  }

  // en: The port opened (DTR up): send what was kept, oldest first, then go on live.
  // ja: ポートが開いた（DTR が上がった）: 貯めたものを古い順に送ってから、そのまま続ける。
  const bool open = SerialUSB.connected();
  if (open && !wasOpen) {
    SerialUSB.printf("resumed: %u kept, %lu lost while closed\r\n", (unsigned)count, (unsigned long)lost);
    while (count && SerialUSB.connected()) {
      SerialUSB.printf("reading %lu (kept)\r\n", (unsigned long)ring[head]);
      head = (head + 1) % KEEP;
      count--;
    }
    Serial.printf("port opened: backlog sent, %lu lost\n", (unsigned long)lost);
    lost = 0;
  } else if (!open && wasOpen) {
    Serial.println("port closed: keeping readings");
  }
  wasOpen = open;
}
