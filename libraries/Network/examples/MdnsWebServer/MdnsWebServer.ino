// en: A small web server reachable as http://ch32rv.local/, announced over mDNS.
// ja: mDNSで名前を公開し、http://ch32rv.local/ で開ける小さなWebサーバー。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: ETH_MAC */

#include <Network.h>

static const char *HOSTNAME = "ch32rv";      // en: the name before .local / ja: .localの前の名前
static const uint32_t REQUEST_TIMEOUT_MS = 3000;

static NetworkServer server(80);
static uint32_t served;

/* en: Read the request line ("GET /path HTTP/1.1") and skip the rest of the header.
 * ja: リクエスト行（"GET /path HTTP/1.1"）を読み、残りのヘッダーは読み飛ばす。 */
static bool readRequest(NetworkClient &client, char *path, size_t size)
{
    char line[96];
    size_t used = 0;
    bool first = true;
    path[0] = 0;
    const uint32_t t0 = millis();
    while (client.connected() && millis() - t0 < REQUEST_TIMEOUT_MS) {
        const int c = client.read();
        if (c < 0) {
            delay(1);
            continue;
        }
        if (c != '\n') {
            if (used < sizeof line - 1) {
                line[used++] = (char)c;
            }
            continue;
        }
        line[used] = 0;
        if (used && line[used - 1] == '\r') {
            line[--used] = 0;
        }
        if (first) {
            first = false;
            const char *p = strchr(line, ' ');
            const char *end = p ? strchr(p + 1, ' ') : nullptr;
            if (!p || !end) {
                return false;
            }
            const size_t n = (size_t)(end - p - 1) < size - 1 ? (size_t)(end - p - 1) : size - 1;
            memcpy(path, p + 1, n);
            path[n] = 0;
        } else if (!used) {
            return path[0] != 0;                 // en: the empty line ends the header / ja: 空行でヘッダーが終わる
        }
        used = 0;
    }
    return false;
}

static void answer(NetworkClient &client, const char *path)
{
    if (strcmp(path, "/") != 0) {
        client.print("HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\n\r\nnot found\r\n");
        return;
    }
    served++;
    client.print("HTTP/1.0 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n");
    client.print("<!doctype html><title>CH32RV</title><h1>Hello from ");
    client.print(HOSTNAME);
    client.print(".local</h1><p>Up for ");
    client.print(millis() / 1000);
    client.print(" s, page served ");
    client.print(served);
    client.print(" times, address ");
    client.print(ETH.localIP());
    client.print("</p>\r\n");
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
    server.begin();
    // en: After the address: mDNS answers for the interface's current address.
    // ja: アドレスを得てから始める。mDNSはインタフェースの今のアドレスで答える。
    if (MDNS.begin(HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.print("http://");
        Serial.print(HOSTNAME);
        Serial.print(".local/  (");
    } else {
        Serial.print("mDNS did not start; use the address (");
    }
    Serial.print(ETH.localIP());
    Serial.println(')');
}

void loop()
{
    NetworkClient client = server.accept();
    if (!client) {
        return;
    }
    char path[48];
    if (readRequest(client, path, sizeof path)) {
        Serial.print(client.remoteIP());
        Serial.print(" GET ");
        Serial.println(path);
        answer(client, path);
    } else {
        Serial.println("Request incomplete or timed out");
    }
    client.stop();
}
