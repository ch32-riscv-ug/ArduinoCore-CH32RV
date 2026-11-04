// en: Fetch a page over HTTPS, the server's certificate checked against one root, and report the TLS version,
//     the status, the size, and why it failed when it does.
// ja: HTTPSでページを取得する。サーバーの証明書を1つのルートで確かめ、TLSの版・状態コード・大きさと、
//     失敗したときはその理由を表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: ETH_MAC, RNG */

#include <Network.h>
#include <NetworkClientSecure.h>
#include <time.h>

// en: Let's Encrypt's test site for this root; any host whose chain ends at ISRG Root X1 works.
// ja: このルートを試すための Let's Encrypt のサイト。ISRG Root X1 につながるホストならどれでもよい。
static const char *HOST = "valid-isrgrootx1.letsencrypt.org";
static const uint16_t PORT = 443;
static const uint32_t TIMEOUT_MS = 20000;
static const uint32_t PERIOD_MS = 60000;

// en: ISRG Root X1 (valid until 2035-06-04). Kept by pointer: it must stay valid while connected.
// ja: ISRG Root X1（2035-06-04まで有効）。ポインタで持たれるので、接続の間は残しておく。
static const char ROOT_CA[] =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n";

static NetworkClientSecure client;
static uint32_t lastRun;
static bool first = true;

static void fetch()
{
    const uint32_t t0 = millis();
    if (!client.connect(HOST, PORT)) {
        char text[96];
        const int err = client.lastError(text, sizeof text);
        Serial.print("TLS connect failed: ");
        Serial.print(err);
        Serial.print(' ');
        Serial.print(text);
        Serial.print(", verify flags 0x");
        Serial.println(client.verifyFlags(), HEX);
        return;
    }
    Serial.print("TLS 0x");
    Serial.print(client.tlsVersion(), HEX);   // en: 303 is 1.2, 304 is 1.3 / ja: 303 が 1.2、304 が 1.3
    Serial.print(" in ");
    Serial.print(millis() - t0);
    Serial.println(" ms");

    // en: HTTP/1.0: the body comes as it is and the server closes at its end.
    // ja: HTTP/1.0で頼む。本文はそのまま届き、終わるとサーバーが閉じる。
    client.print("GET / HTTP/1.0\r\nHost: ");
    client.print(HOST);
    client.print("\r\n\r\n");

    char line[64];
    size_t used = 0;
    int status = 0;
    bool header = true;
    uint32_t body = 0;
    const uint32_t t1 = millis();
    while ((client.connected() || client.available()) && millis() - t1 < TIMEOUT_MS) {
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
    client.stop();
    if (!status) {
        Serial.println("closed without an HTTP status");
        return;
    }
    Serial.print("HTTP ");
    Serial.print(status);
    Serial.print(", ");
    Serial.print(body);
    Serial.println(" bytes");
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
    // en: The certificates' dates are checked once the clock is set; wait for SNTP a little.
    // ja: 証明書の日付は時計が合ってから確かめる。SNTPを少し待つ。
    configTime(0, 0, "pool.ntp.org");
    for (int i = 0; i < 100 && time(nullptr) < 1735689600; i++) {   // 2025-01-01
        delay(100);
    }
    client.setCACert(ROOT_CA);
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
