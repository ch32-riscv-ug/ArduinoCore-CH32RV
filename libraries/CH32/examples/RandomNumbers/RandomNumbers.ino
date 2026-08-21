/* RandomNumbers - random(), and why it repeats.
 *
 * Wiring: none, though an unconnected analog pin makes the seed better.
 *
 * random() is a software generator: from the same seed it produces the same
 * sequence every run. That is a feature when you are debugging and a problem
 * when you wanted dice. randomSeed() is how you choose which.
 */
void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }

    /* Same seed, same numbers - run this twice and compare. */
    randomSeed(1);
    Serial.print("seed 1: ");
    for (int i = 0; i < 5; i++) {
        Serial.print(random(100));
        Serial.print(' ');
    }
    Serial.println();

    /* A floating analog input drifts, so its low bits differ between runs.
     * It is not a good source of randomness, but it is the classic one. */
    randomSeed((unsigned long)analogRead(A0) ^ micros());
    Serial.print("seeded from noise: ");
    for (int i = 0; i < 5; i++) {
        Serial.print(random(100));
        Serial.print(' ');
    }
    Serial.println();
}

void loop()
{
    /* random(min, max) excludes max, like the rest of Arduino. */
    Serial.print("d6: ");
    Serial.println(random(1, 7));
    delay(1000);
}
