// en: Fetch a new sketch over HTTP and install it (as arduino-esp32's HTTPUpdate): GET UPDATE_PATH from UPDATE_HOST,
//     the length from Content-Length, the bytes into Update, checked, then a restart into it. Asks once at start.
// ja: HTTP で新しいスケッチを取ってきて入れ替える（arduino-esp32 の HTTPUpdate と同じ役割）: UPDATE_HOST から
//     UPDATE_PATH を GET し、長さは Content-Length、バイトは Update へ。確かめてから入れ替えて起動する。起動時に 1 回。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: ETHERNET */

#include <CH32RV.h>
#include <Network.h>
#include <Update.h>

// en: The PC that serves the image (README: python3 -m http.server).
// ja: イメージを配る PC（README: python3 -m http.server）。
static const char *UPDATE_HOST = "192.168.1.10";
static const uint16_t UPDATE_PORT = 8000;
static const char *UPDATE_PATH = "/new.bin";

static void update()
{
    NetworkClient client;
    if (!client.connect(UPDATE_HOST, UPDATE_PORT)) {
        Serial.println("no connection to the update server");
        return;
    }
    client.print("GET ");
    client.print(UPDATE_PATH);
    client.print(" HTTP/1.0\r\nHost: ");
    client.print(UPDATE_HOST);
    client.print("\r\n\r\n");
    client.setTimeout(5000);
    // en: The header: the status line, then Content-Length, up to the empty line.
    // ja: ヘッダー: 状態の行、Content-Length、空行まで。
    const String status = client.readStringUntil('\n');
    if (status.indexOf(" 200") < 0) {
        Serial.print("server says: ");
        Serial.println(status);
        return;
    }
    long length = -1;
    for (;;) {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) {
            break;
        }
        if (line.startsWith("Content-Length:") || line.startsWith("content-length:")) {
            length = line.substring(15).toInt();
        }
    }
    if (length <= 0) {
        Serial.println("no Content-Length");
        return;
    }
    Serial.printf("image %ld bytes, room for %u\r\n", length, unsigned(Update.capacity()));
    if (!Update.begin((size_t)length)) {
        Update.printError(Serial);
        return;
    }
    const uint32_t t0 = millis();
    const size_t written = Update.writeStream(client);
    client.stop();
    Serial.printf("%u bytes in %lu ms\r\n", unsigned(written), (unsigned long)(millis() - t0));
    // en: end() checks the image; only then may it replace this sketch.
    // ja: end() がイメージを確かめる。それからでないと入れ替えない。
    if (!Update.end()) {
        Update.printError(Serial);
        return;
    }
    Serial.println("OK, restarting into it");
    Serial.flush();
    CH32RV.restart();
}

void setup()
{
    Serial.begin(115200);
    delay(100);
    if (!ETH.begin()) {
        Serial.println("ETH.begin() failed");
        return;
    }
    while (!ETH.hasIP()) {
        delay(100);
    }
    Serial.print("Address ");
    Serial.println(ETH.localIP());
    update();
}

void loop()
{
}
