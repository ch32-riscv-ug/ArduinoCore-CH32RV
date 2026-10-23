// en: Echo USB serial bytes and periodically report a counter.
// ja: USBシリアルの受信を返し、一定間隔でカウンタを表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

#include <CH32RVUsbDevice.h>

void setup() {
  // en: Start the predefined USB serial port; the host still needs to configure and open it.
  // ja: 定義済みのUSBシリアルを開始する。利用にはホスト側での認識とポートのopenも必要。
  SerialUSB.begin();
}

void loop() {
  // en: Keep the previous send time and counter between loop() calls; static locals start at zero.
  // ja: staticのローカル変数で前回の送信時刻と回数を保持する。初期値は0。
  static uint32_t last;
  static uint32_t count;

  // en: Read only bytes already buffered, then send each byte back unchanged.
  // ja: すでに受信済みの分だけ読み、一文字ずつ加工せずに送り返す。
  while (SerialUSB.available()) {
    SerialUSB.write(SerialUSB.read());
  }
  // en: DTR signals host use of the port; send periodic text only while that signal is active.
  // ja: DTRはホストがポートの利用を示す信号。その信号が有効な間だけ定期出力する。
  if (SerialUSB && millis() - last >= 1000) {
    last = millis();
    SerialUSB.print("count ");
    SerialUSB.println(count++);
  }
}
