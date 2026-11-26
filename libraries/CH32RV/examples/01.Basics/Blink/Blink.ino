// en: Blink an LED by switching a GPIO output between High and Low.
// ja: GPIOをHighとLowに切り替えてLEDを点滅させる。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

// en: Only a board that defines LED_BUILTIN builds this as is (the profile below); on a Generic board name the pad.
// ja: そのままビルドできるのは LED_BUILTIN を持つ板だけ（下のプロファイル）。Generic の板では端子を指定する。
/* board: UIAPDUINO_V003_V14 */

// en: The board or build must identify the actual LED pin.
// ja: 実際のLED端子はボード定義またはビルド設定で指定する。
#ifndef LED_BUILTIN
#error "no LED_BUILTIN on this board - build with --build-property build.extra_flags=-DLED_BUILTIN=PC13 (use your board's LED pad)"
#endif

// en: setup() runs once after reset; configure the pin before writing its output.
// ja: setup()はリセット後に一度動く。出力を書き換える前に端子のモードを設定する。
void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop()
{
    // en: loop() repeats: keep this level for 500 ms, then switch to the other level.
    // ja: loop()は繰り返し動く。この状態を500 ms保ってから反対の状態へ切り替える。
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
}
