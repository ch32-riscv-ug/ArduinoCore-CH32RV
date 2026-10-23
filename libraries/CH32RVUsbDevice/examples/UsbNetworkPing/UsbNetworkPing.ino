// en: Setup and usage: README.md. / ja: 配線・使い方: README.ja.md。
/* requires: ram=16K */

#include <CH32RVUsbDevice.h>
#include <string.h>

CH32RVUsbDeviceNet net;

static const uint8_t MY_IP[4] = {192, 168, 7, 1};
static uint8_t myMac[6];
static uint8_t reply[1514];
static size_t replyLength;
static uint32_t arps, pings, frames;

static uint16_t checksum(const uint8_t *p, size_t n)
{
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        sum += (uint32_t)(p[i] << 8 | p[i + 1]);
    }
    if (n & 1) {
        sum += (uint32_t)(p[n - 1] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

// en: Build one pending reply here and transmit it later from loop(). / ja: ここでは返信を一つ準備し、送信は後でloop()から行う。
static void onFrame(const uint8_t *f, size_t n)
{
    frames++;
    if (replyLength || n < 42) {
        return;
    }
    const uint16_t type = (uint16_t)(f[12] << 8 | f[13]);
    if (type == 0x0806 && f[21] == 1 && memcmp(f + 38, MY_IP, 4) == 0) {
        memcpy(reply, f + 6, 6);
        memcpy(reply + 6, myMac, 6);
        // en: Preserve EtherType and ARP address-format fields. / ja: EtherTypeとARPのアドレス形式fieldを引き継ぐ。
        memcpy(reply + 12, f + 12, 8);
        // en: ARP reply operation. / ja: ARPの応答opcode。
        reply[21] = 2;
        memcpy(reply + 22, myMac, 6);
        memcpy(reply + 28, MY_IP, 4);
        memcpy(reply + 32, f + 22, 10);
        replyLength = 42;
        arps++;
    } else if (type == 0x0800 && f[23] == 1 && f[34] == 8 && memcmp(f + 30, MY_IP, 4) == 0 && n <= sizeof reply) {
        memcpy(reply, f, n);
        memcpy(reply, f + 6, 6);
        memcpy(reply + 6, myMac, 6);
        // en: Swap IPv4 endpoints, then recompute both IP and ICMP checksums.
        // ja: IPv4の送受信先を入れ替え、IPとICMP両方のchecksumを再計算する。
        memcpy(reply + 26, f + 30, 4);
        memcpy(reply + 30, f + 26, 4);
        reply[24] = reply[25] = 0;
        const uint16_t ipsum = checksum(reply + 14, (size_t)(reply[14] & 15) * 4);
        reply[24] = (uint8_t)(ipsum >> 8);
        reply[25] = (uint8_t)ipsum;
        const size_t ip = 14 + (size_t)(reply[14] & 15) * 4;
        const size_t total = 14 + (size_t)(reply[16] << 8 | reply[17]);
        reply[ip] = 0;
        reply[ip + 2] = reply[ip + 3] = 0;
        const uint16_t icmpsum = checksum(reply + ip, total - ip);
        reply[ip + 2] = (uint8_t)(icmpsum >> 8);
        reply[ip + 3] = (uint8_t)icmpsum;
        replyLength = total;
        pings++;
    }
}

void setup()
{
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    net.onFrame(onFrame);

    CH32RVUsbDeviceConfig config;
    config.product = "CH32RV USB network";
    if (!USBDevice.begin(config)) {
        Serial.print("USB_BEGIN_FAILED ");
        Serial.println(USBDevice.lastErrorName());
        return;
    }

    memcpy(myMac, net.macAddress(), 6);
    // en: Keep the device-side responder MAC distinct from the host-side MAC. / ja: 応答側MACがホスト側と同一にならないようにする。
    myMac[5] ^= 1;
}

void loop()
{
    // en: Deliver notifications here. / ja: 通知callbackはここで実行する。
    deliverEvents();
    if (replyLength) {
        net.sendFrame(reply, replyLength);
        replyLength = 0;
    }
    static uint32_t last;
    if (millis() - last >= 2000) {
        last = millis();
        Serial.print("link=");
        Serial.print(net.linkUp() ? 1 : 0);
        Serial.print(" frames=");
        Serial.print(frames);
        Serial.print(" arp=");
        Serial.print(arps);
        Serial.print(" ping=");
        Serial.println(pings);
    }
}
