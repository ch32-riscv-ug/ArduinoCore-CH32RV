/* Wiring, operation and expected output: README.md / README.ja.md. */
/* UsbNetworkPing - a USB network adapter (CDC-NCM) that answers ping.
 *
 * The board appears as a network adapter on a host with a CDC-NCM driver.
 * This core has no IP stack, so the sketch
 * answers the two things `ping` needs straight from the Ethernet frames: ARP
 * ("who has 192.168.7.1?") and ICMP echo. Give the PC's new interface an
 * address on 192.168.7.0/24 (e.g. 192.168.7.2/24) and run `ping 192.168.7.1`.
 *
 * EspUsbDevice's UsbNetwork example serves a web page through lwIP; that
 * needs an IP stack this core does not ship.
 *
 * See README.md for controller support and the limited IPv4 parser scope.
 * requires: ram=16K */
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

/* Builds the answer in `reply`; loop() sends it (sending from inside the
 * frame callback would re-enter the USB stack). */
static void onFrame(const uint8_t *f, size_t n)
{
    frames++;
    if (replyLength || n < 42) {
        return;
    }
    const uint16_t type = (uint16_t)(f[12] << 8 | f[13]);
    if (type == 0x0806 && f[21] == 1 && memcmp(f + 38, MY_IP, 4) == 0) {   /* ARP request for us */
        memcpy(reply, f + 6, 6);
        memcpy(reply + 6, myMac, 6);
        memcpy(reply + 12, f + 12, 8);           /* type, htype, ptype, sizes */
        reply[21] = 2;                           /* reply */
        memcpy(reply + 22, myMac, 6);
        memcpy(reply + 28, MY_IP, 4);
        memcpy(reply + 32, f + 22, 10);          /* the asker's MAC and IP */
        replyLength = 42;
        arps++;
    } else if (type == 0x0800 && f[23] == 1 && f[34] == 8 && memcmp(f + 30, MY_IP, 4) == 0 && n <= sizeof reply) {
        /* IPv4 ICMP echo request for us: swap addresses, make it a reply. */
        memcpy(reply, f, n);
        memcpy(reply, f + 6, 6);
        memcpy(reply + 6, myMac, 6);
        memcpy(reply + 26, f + 30, 4);
        memcpy(reply + 30, f + 26, 4);
        reply[24] = reply[25] = 0;
        const uint16_t ipsum = checksum(reply + 14, (size_t)(reply[14] & 15) * 4);
        reply[24] = (uint8_t)(ipsum >> 8);
        reply[25] = (uint8_t)ipsum;
        const size_t ip = 14 + (size_t)(reply[14] & 15) * 4;
        const size_t total = 14 + (size_t)(reply[16] << 8 | reply[17]);
        reply[ip] = 0;                           /* echo reply */
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
    Serial.begin(115200, SERIAL_8N1, RX, TX);   /* the log: RX, TX are the pads you wired */
    net.onFrame(onFrame);

    CH32RVUsbDeviceConfig config;
    config.product = "CH32RV USB network";
    if (!USBDevice.begin(config)) {
        Serial.print("USB_BEGIN_FAILED ");
        Serial.println(USBDevice.lastErrorName());
        return;
    }
    /* The PC's interface takes macAddress(); this side answers with the same
     * address and the last bit flipped, so the two differ. */
    memcpy(myMac, net.macAddress(), 6);
    myMac[5] ^= 1;
}

void loop()
{
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
