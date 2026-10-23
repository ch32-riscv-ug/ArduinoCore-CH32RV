// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。

// en: The board or build must identify the actual LED pin. / ja: 実際のLED端子はボード定義またはビルド設定で指定する。
#ifndef LED_BUILTIN
#error "no LED_BUILTIN on this board - build with --build-property build.extra_flags=-DLED_BUILTIN=PC13 (use your board's LED pad)"
#endif

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop()
{
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
}
