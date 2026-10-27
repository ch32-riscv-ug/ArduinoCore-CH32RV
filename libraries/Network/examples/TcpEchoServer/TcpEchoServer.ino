// en: A TCP echo server on port 7: what a client sends comes back, one client at a time.
// ja: ポート7のTCPエコーサーバー。クライアントが送ったものをそのまま返す（同時に1台）。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: ETH_MAC */

#include <Network.h>

static const uint16_t PORT = 7;
static const uint32_t IDLE_TIMEOUT_MS = 60000;   // en: drop a silent client / ja: 黙ったクライアントは切る

static NetworkServer server(PORT);
static NetworkClient client;
static uint8_t buf[256];
static uint32_t lastActivity, echoed;
static bool serving;   // en: a client was accepted and not yet reported closed / ja: 受け付けて、まだ切断を表示していない

void setup()
{
    Serial.begin(115200);
    delay(100);
    if (!ETH.begin()) {
        Serial.println("ETH.begin() failed");
        return;
    }
    Serial.println("Waiting for an address...");
    while (!ETH.hasIP()) {
        delay(100);
    }
    server.begin();
    Serial.print("Echo server on ");
    Serial.print(ETH.localIP());
    Serial.print(':');
    Serial.println(PORT);
}

void loop()
{
    if (!client.connected()) {
        if (serving) {
            // en: The previous client went away (a closed client tests false, so keep our own flag).
            // ja: 前のクライアントが切れた（切れたclientは偽になるので、自分のフラグで覚える）。
            Serial.print("Closed after ");
            Serial.print(echoed);
            Serial.println(" bytes");
            client.stop();
            serving = false;
        }
        client = server.accept();
        if (client) {
            serving = true;
            Serial.print("Client ");
            Serial.print(client.remoteIP());
            Serial.print(':');
            Serial.println(client.remotePort());
            lastActivity = millis();
            echoed = 0;
        }
        return;
    }
    // en: TCP has no messages: read() returns what has arrived, in any split.
    // ja: TCPにメッセージの区切りは無い。read()は届いた分を、どこで分かれていても返す。
    const int n = client.read(buf, sizeof buf);
    if (n > 0) {
        size_t sent = 0;
        while (sent < (size_t)n && client.connected()) {
            // en: write() may take only part when the send buffer is full; send the rest later in the loop.
            // ja: 送信バッファが一杯ならwrite()は一部しか受け取らない。残りは続けて送る。
            const size_t w = client.write(buf + sent, (size_t)n - sent);
            sent += w;
            if (!w) {
                delay(1);
            }
        }
        echoed += sent;
        lastActivity = millis();
    } else if (millis() - lastActivity > IDLE_TIMEOUT_MS) {
        Serial.println("Idle too long: closing");
        client.stop();
    }
}
