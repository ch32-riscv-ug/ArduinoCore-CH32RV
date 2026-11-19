// en: A PIOC program in this sketch's pulses.S puts n pulses on IO0 for each command n and answers 0x5A.
// ja: このスケッチの pulses.S の PIOC のプログラムが、コマンド n ごとに IO0 へ n 個のパルスを出し、0x5A を返す。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: CH32RV_PIOC_BASE */

#include <CH32RVPioc.h>

// en: The image pulses.S makes (PIOC_BEGIN pulses / PIOC_END pulses).
// ja: pulses.S が作るイメージ（PIOC_BEGIN pulses / PIOC_END pulses）。
PIOC_IMAGE(pulses);
CH32RVPioc pioc;

// en: IO0's pad: one the PIOC reaches (the variant's CH32RV_PIOC_PADS); PA0 on CH32H417.
// ja: IO0 の端子。PIOC が届く端子（variant の CH32RV_PIOC_PADS）。CH32H417 なら PA0。
static const int IO0_PIN = PA0;

void setup()
{
    Serial.begin(115200);
    if (!pioc.begin(PIOC_IMAGE_OF(pulses), IO0_PIN)) {
        Serial.println("PIOC: not on this part or pad");
    }
}

void loop()
{
    static uint8_t n = 1;
    pioc.command(n);                       // en: cmd.in / ja: cmd.in に書く
    const int status = pioc.waitDone(100); // en: cmd.out, -1 on a time-out / ja: cmd.out、時間切れは -1
    Serial.print(n);
    Serial.print(" pulses -> 0x");
    Serial.print(status, HEX);
    Serial.print(", commands so far ");
    Serial.println(pioc.read(1));          // en: d1 / ja: d1
    n = n == 8 ? 1 : n + 1;
    delay(500);
}
