// en: Fetch a page over HTTP by host name and report the status, the size and each kind of failure.
// ja: ホスト名でHTTPのページを取得し、状態コード・大きさ・失敗の種類を表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: ETH_MAC */

#include <Network.h>

static const char *HOST = "example.com";   // en: or a PC on your network / ja: 手元のPCでもよい
static const uint16_t PORT = 80;
static const char *PATH = "/";
static const uint32_t TIMEOUT_MS = 10000;
static const uint32_t PERIOD_MS = 30000;

static uint32_t lastRun;
static bool first = true;

static void fetch()
{
    IPAddress ip;
    // en: Name lookup first, so a DNS failure is told apart from a refused connection.
    // ja: 先に名前を引く。DNSの失敗と、接続を断られたことを区別するため。
    if (Network.hostByName(HOST, ip) != 1) {
        Serial.print("DNS: cannot resolve ");
        Serial.println(HOST);
        return;
    }
    NetworkClient client;
    if (!client.connect(ip, PORT)) {
        Serial.print("Connect to ");
        Serial.print(ip);
        Serial.println(" failed");
        return;
    }
    // en: HTTP/1.0: the body comes as it is (no chunked encoding) and the server closes at its end.
    // ja: HTTP/1.0で頼む。本文はそのまま（chunkedにならない）届き、終わるとサーバーが閉じる。
    client.print("GET ");
    client.print(PATH);
    client.print(" HTTP/1.0\r\nHost: ");
    client.print(HOST);
    client.print("\r\n\r\n");

    // en: The status line, then count the body. Stop at the deadline even if the server never closes.
    // ja: 状態行を読み、本文の大きさを数える。サーバーが閉じなくても期限で止める。
    char line[64];
    size_t used = 0;
    int status = 0;
    bool header = true;
    uint32_t body = 0;
    const uint32_t t0 = millis();
    while ((client.connected() || client.available()) && millis() - t0 < TIMEOUT_MS) {
        const int c = client.read();
        if (c < 0) {
            delay(1);
            continue;
        }
        if (!header) {
            body++;
            continue;
        }
        if (c == '\n') {
            line[used] = 0;
            if (!status && strncmp(line, "HTTP/1.", 7) == 0) {
                status = atoi(line + 9);
            }
            if (used <= 1) {                     // en: the empty line ends the header / ja: 空行でヘッダーが終わる
                header = false;
            }
            used = 0;
        } else if (used < sizeof line - 1) {
            line[used++] = (char)c;
        }
    }
    const bool timedOut = millis() - t0 >= TIMEOUT_MS;
    client.stop();
    Serial.print(HOST);
    Serial.print(" (");
    Serial.print(ip);
    Serial.print("): ");
    if (!status) {
        Serial.println(timedOut ? "no answer before the deadline" : "closed without an HTTP status");
        return;
    }
    Serial.print("HTTP ");
    Serial.print(status);
    Serial.print(", ");
    Serial.print(body);
    Serial.print(" bytes in ");
    Serial.print(millis() - t0);
    Serial.println(timedOut ? " ms (cut at the deadline)" : " ms");
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
}

void loop()
{
    if (!ETH.hasIP()) {
        return;                                  // en: wait for the network to come back / ja: 復帰を待つ
    }
    if (first || millis() - lastRun >= PERIOD_MS) {
        first = false;
        lastRun = millis();
        fetch();
    }
}
