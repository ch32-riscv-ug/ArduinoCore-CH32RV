/* SerialEcho - read a line from the serial monitor and send it back.
 *
 * Wiring: the board's USART pins to a USB-serial adapter, or a WCH-LinkE,
 * which has one built in. Which pads Serial uses is printed at startup, so
 * this example also tells you where to put the wires.
 */
void setup()
{
    Serial.begin(115200);
    while (!Serial) {
    }
    Serial.println();
    Serial.println("SerialEcho ready - type a line and press enter");
}

void loop()
{
    if (Serial.available() == 0) {
        return;
    }
    /* readStringUntil() stops at the newline and drops it. */
    String line = Serial.readStringUntil('\n');
    line.trim();
    Serial.print("you said: ");
    Serial.println(line);
}
