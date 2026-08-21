/* ToneMelody - play a short tune on a passive buzzer.
 *
 * Wiring: a passive buzzer (not the self-oscillating kind) between the pin and
 * ground. An active buzzer will just beep at its own pitch and ignore the
 * notes.
 *
 * tone() drives the pin from a timer interrupt, so any pin works. Which timer
 * the series gave it is in the variant header as CH32_TONE_TIMER; on the small
 * parts it is shared with analogWrite(), and the header names the pads that
 * are affected while a tone plays.
 */
static const uint8_t BUZZER = LED_BUILTIN;   /* change to your buzzer's pin */

/* A fragment of the scale. Frequencies in Hz, durations in milliseconds. */
static const unsigned int notes[] = {262, 294, 330, 349, 392, 440, 494, 523};
static const unsigned long beat = 200;

void setup()
{
    for (unsigned i = 0; i < sizeof notes / sizeof notes[0]; i++) {
        tone(BUZZER, notes[i], beat);
        /* tone() returns immediately; the timer keeps playing in the
         * background, so wait out the note plus a short gap. */
        delay(beat + 50);
    }
    noTone(BUZZER);
}

void loop()
{
}
