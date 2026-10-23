// en: Accumulate UART bytes into lines using a fixed-size ring buffer.
// ja: 固定容量のリングバッファでUARTの受信を一行ずつ組み立てる。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVSyncKit.h>

// en: LoopOnly is valid because this ring is not shared with an ISR.
// ja: このRingはISRと共有しないためLoopOnlyを使う。
static CH32RVSyncKit::Ring<64, CH32RVSyncKit::LoopOnly> line;
static uint32_t overflowed = 0;

static void handleLine()
{
  char text[65];
  const size_t n = line.read(text, sizeof text - 1);
  // en: Terminate only the bytes actually read.
  // ja: 実際に読み出した長さの位置へ終端を置く。
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
    // en: A newline finishes the application line; individual UART arrivals do not define its boundary.
    // ja: 改行で一行の完成とする。UARTで一度に届いた量を行の区切りにはしない。
    if (c == '\n')
    {
      handleLine();
    }
    else if (c != '\r' && !line.write((uint8_t)c))
    {
      overflowed++;
    }
  }
  if (overflowed)
  {
    Serial.print("OVERFLOW dropped=");
    Serial.println(overflowed);
    overflowed = 0;
  }
}
