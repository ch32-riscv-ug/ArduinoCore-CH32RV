/* Blink - the smallest program that proves the board runs.
 *
 * Wiring: none, if the board has an LED on LED_BUILTIN.
 *
 * On the generic series boards this core ships, LED_BUILTIN is a placeholder:
 * it names the lowest-numbered pad that exists on every part in the series,
 * because a series is not a board and cannot know where an LED sits. Point it
 * at the right pad for your board on the command line:
 *
 *   arduino-cli compile --build-property build.extra_flags=-DLED_BUILTIN=PC13
 */
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
