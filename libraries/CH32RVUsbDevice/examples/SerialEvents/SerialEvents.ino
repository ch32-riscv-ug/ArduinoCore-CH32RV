/* Wiring, operation and expected output: README.md / README.ja.md. */
/* SerialEvents - where the USB callbacks run: only inside deliverEvents().
 *
 * The callbacks echo what the PC sends and log the port's line state and
 * coding on the UART. Every 10 s loop() is busy for 3 s with delay(): USB
 * keeps working (the PC can still send), but no callback runs. What arrived
 * meanwhile waits in the port's buffer and comes in one onRx() call when
 * loop() delivers again.
 * docs/sketch-guide.ja.md walks through the model. */
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
  Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
  SerialUSB.onRx(onRx);
  SerialUSB.onLineState(onLineState);
  SerialUSB.onLineCoding(onLineCoding);
  SerialUSB.begin();                          /* USB now runs by itself, even inside delay() */
  Serial.println("SerialEvents ready");
}

void loop()
{
  /* The one place the callbacks above run. */
  const size_t ran = deliverEvents();
  if (ran)
  {
    Serial.printf("DELIVER ran=%u\r\n", (unsigned)ran);
  }

  static uint32_t lastBusy = millis();
  if (millis() - lastBusy >= 10000)
  {
    Serial.println("BUSY 3 s: type now, the callbacks wait");
    delay(3000);                              /* USB is serviced in here; callbacks are not */
    Serial.println("BUSY end");
    lastBusy = millis();
  }
}
