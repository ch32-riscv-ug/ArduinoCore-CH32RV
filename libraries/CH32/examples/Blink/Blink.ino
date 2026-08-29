/* Blink - the smallest program that proves the board runs.
 *
 * Wiring: none, if your board has an LED. Otherwise an LED and a resistor
 * from the pin to ground.
 *
 * A Generic board in this core is a silicon series, not a PCB, so it does not
 * know whether there is an LED or which pad it sits on - and it will not
 * guess, because a blinking pad with no LED on it looks exactly like a dead
 * board. Name the pad yourself:
 *
 *   arduino-cli compile --build-property build.extra_flags=-DLED_BUILTIN=PC13
 *
 * A product-board variant defines LED_BUILTIN for you, and then this compiles
 * with nothing extra. See docs/board-layer-rules.ja.md.
 */
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
