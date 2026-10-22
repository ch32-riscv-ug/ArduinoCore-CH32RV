/* Wiring, operation and expected output: README.md / README.ja.md. */
/* RingBuffer - a fixed-size byte stream: lines typed on the UART are
 * collected in a Ring and handled whole. write() takes what fits and says
 * how much; what did not fit is the caller's to keep or drop.
 *
 * Only loop() touches this ring, so it is LoopOnly: no interrupt masking.
 * docs: ../../README.ja.md describes the library. */
#include <CH32RVSyncKit.h>

static CH32RVSyncKit::Ring<64, CH32RVSyncKit::LoopOnly> line;
static uint32_t overflowed = 0;

static void handleLine()
{
  char text[65];
  const size_t n = line.read(text, sizeof text - 1);
  text[n] = 0;
  Serial.print("LINE bytes=");
  Serial.print(n);
  Serial.print(" text=");
  Serial.println(text);
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  Serial.println("RingBuffer ready");
}

void loop()
{
  while (Serial.available())
  {
    const int c = Serial.read();
    if (c == '\n')
    {
      handleLine();
    }
    else if (c != '\r' && !line.write((uint8_t)c))
    {
      overflowed++;   /* full: this byte is dropped, the earlier ones are kept */
    }
  }
  if (overflowed)
  {
    Serial.print("OVERFLOW dropped=");
    Serial.println(overflowed);
    overflowed = 0;
  }
}
