// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

#include <CH32RVSyncKit.h>

static const int BUTTON_PIN = PA1;

static volatile uint32_t pressCount = 0;
static volatile uint32_t lastPressMs = 0;
static volatile uint64_t sumOfMicros = 0;

static void onPress()
{
  pressCount = pressCount + 1;
  lastPressMs = millis();
  sumOfMicros = sumOfMicros + micros();
}

void setup()
{
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, FALLING);
  Serial.println("CriticalSection ready");
}

void loop()
{
  uint32_t count, last;
  uint64_t sum;
  {
    // en: Copy the whole snapshot; scope exit restores the previous interrupt state.
    // ja: 一組の値をまとめてコピーし、スコープ終了で元の割り込み状態に戻す。
    CH32RVSyncKit::CriticalSection cs;
    count = pressCount;
    last = lastPressMs;
    sum = sumOfMicros;
  }

  // en: Print from local copies after leaving the protected scope. / ja: 保護区間を抜けてからローカルのコピーを表示する。
  Serial.print("PRESSES count=");
  Serial.print(count);
  Serial.print(" last_ms=");
  Serial.print(last);
  Serial.print(" sum_us_low=");
  Serial.println((unsigned long)(sum & 0xFFFFFFFFu));
  delay(1000);
}
