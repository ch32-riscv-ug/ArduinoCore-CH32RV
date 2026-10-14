/* SerialEcho - read a line from the serial monitor and send it back.
 *
 * Wiring: the two pads below to a USB-serial adapter, or a WCH-LinkE, which
 * has one built in. Change them to the pads your board wires: Serial takes
 * whichever enabled USART has them as a route. RX and TX are the chip's
 * default pads for its first USART.
 */
static const int RX_PIN = RX;
static const int TX_PIN = TX;

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
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
