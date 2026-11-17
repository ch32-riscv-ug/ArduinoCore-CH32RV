/* Replace this sketch with a new one sent over Serial.
 *
 * Send the image's length as a decimal line, then its bytes (the .bin the
 * Arduino build makes, flash offset 0):
 *
 *     python3 -c "import sys,serial,os,time; p=serial.Serial(sys.argv[1],115200); \
 *         d=open(sys.argv[2],'rb').read(); p.write(b'%d\n'%len(d)); p.write(d); \
 *         time.sleep(1); print(p.read(p.in_waiting).decode())" /dev/ttyUSB0 sketch.ino.bin
 *
 * The sketch stays as it is until the whole image arrived and checked; then
 * CH32RV.restart() copies it over this sketch and runs it. The largest image
 * it can take is printed at start (Update.capacity()): a sketch about half
 * the flash or less. */
#include <CH32RV.h>
#include <Update.h>

void setup()
{
    Serial.begin(115200);
    Serial.setTimeout(2000);
    Serial.print("Update: up to ");
    Serial.print(Update.capacity());
    Serial.println(" bytes; send the length, then the image");
}

void loop()
{
    if (!Serial.available()) {
        return;
    }
    const long size = Serial.parseInt();
    Serial.read();                                  /* the newline */
    if (size <= 0) {
        return;
    }
    if (!Update.begin((size_t)size)) {
        Update.printError(Serial);
        return;
    }
    Update.writeStream(Serial);
    if (Update.end()) {
        Serial.println("OK, restarting into it");
        Serial.flush();
        CH32RV.restart();                           /* installs the new image */
    }
    Update.printError(Serial);
}
