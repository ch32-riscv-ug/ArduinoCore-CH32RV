// en: TLS with a client certificate (mutual TLS): the board checks the server against your CA, and the server checks
//     the board's own certificate. Talks to a local `openssl s_server` (README); prints whether the handshake passed,
//     the TLS version, and the server's first reply line - or why it failed.
// ja: クライアント証明書の TLS（相互認証）: ボードはサーバーを自分の CA で確かめ、サーバーはボード自身の証明書を
//     確かめる。手元の `openssl s_server` と話し（README）、ハンドシェイクが通ったか、TLS の版、サーバーの最初の
//     返事の行、または失敗の理由を表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: ETHERNET, RNG */

#include <Network.h>
#include <NetworkClientSecure.h>
#include <time.h>

#include "certs.h"     // en: SERVER_HOST, CA_CERT, CLIENT_CERT, CLIENT_KEY - yours / ja: 自分のものに置き換える

static const uint16_t PORT = 4433;      // en: openssl s_server's default / ja: openssl s_server の既定
static const uint32_t PERIOD_MS = 30000;

static NetworkClientSecure client;
static uint32_t lastRun;
static bool first = true;

static void talk()
{
    const uint32_t t0 = millis();
    if (!client.connect(SERVER_HOST, PORT)) {
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
    Serial.print("mutual TLS 0x");
    Serial.print(client.tlsVersion(), HEX);
    Serial.print(" in ");
    Serial.print(millis() - t0);
    Serial.println(" ms");

    // en: `openssl s_server -www` answers a GET with a page about the session.
    // ja: `openssl s_server -www` は GET にセッションのページで答える。
    client.print("GET / HTTP/1.0\r\n\r\n");
    // en: Byte by byte up to the first newline: under TLS 1.3 the server's session tickets come first, and
    //     available() can be true with no application byte yet.
    // ja: 最初の改行まで 1 バイトずつ。TLS 1.3 ではサーバーのセッションチケットが先に来るので、アプリの
    //     バイトがまだ無くても available() が真になることがある。
    String first;
    const uint32_t t1 = millis();
    while ((client.connected() || client.available()) && millis() - t1 < 10000) {
        const int c = client.read();
        if (c < 0) {
            delay(1);
            continue;
        }
        if (c == '\n') {
            break;
        }
        first += (char)c;
    }
    client.stop();
    first.trim();
    Serial.print("server says: ");
    Serial.println(first.length() ? first : String("(nothing)"));
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
    // en: Certificates carry dates: set the clock first (up to 10 s).
    // ja: 証明書には日付がある: 先に時計を合わせる（10 秒まで）。
    configTime(0, 0, "pool.ntp.org");
    struct tm now;
    getLocalTime(&now, 10000);
    // en: The server is checked against your CA; the board shows its own certificate and proves it holds the key.
    //     All three are kept by pointer: they must stay valid while connected.
    // ja: サーバーは自分の CA で確かめる。ボードは自分の証明書を見せ、鍵を持つことを示す。どれもポインタで持たれるので、
    //     接続の間は残しておく。
    client.setCACert(CA_CERT);
    client.setCertificate(CLIENT_CERT);
    client.setPrivateKey(CLIENT_KEY);
}

void loop()
{
    if (first || millis() - lastRun >= PERIOD_MS) {
        first = false;
        lastRun = millis();
        talk();
    }
}
