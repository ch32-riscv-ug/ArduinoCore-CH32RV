// en: Bring Ethernet up with DHCP and report the link and the address as they change.
// ja: EthernetをDHCPで起動し、リンクとアドレスの変化を表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: ETH_MAC */

#include <Network.h>

static const uint32_t DHCP_TIMEOUT_MS = 15000;   // en: report, then keep waiting / ja: 表示して待ち続ける

static bool wasUp, hadIP;
static uint32_t linkUpAt;
static bool warned;

static void printAddress()
{
    Serial.print("IP ");
    Serial.print(ETH.localIP());
    Serial.print("  mask ");
    Serial.print(ETH.subnetMask());
    Serial.print("  gateway ");
    Serial.print(ETH.gatewayIP());
    Serial.print("  DNS ");
    Serial.println(ETH.dnsIP());
}

void setup()
{
    Serial.begin(115200);
    delay(100);
    // en: For a fixed address, call ETH.config(ip, gateway, mask, dns) before begin().
    // ja: 固定アドレスにするときは、begin()の前にETH.config(ip, gateway, mask, dns)を呼ぶ。
    if (!ETH.begin()) {
        Serial.println("ETH.begin() failed: no Ethernet on this board, or the PHY does not answer");
        return;
    }
    Serial.print("MAC ");
    Serial.println(ETH.macAddress());
}

void loop()
{
    // en: The stack runs inside delay() and after loop(); here we only look at its state.
    // ja: 通信の処理はdelay()とloop()の戻りで進む。ここでは状態を見るだけ。
    const bool up = ETH.linkUp();
    if (up != wasUp) {
        wasUp = up;
        if (up) {
            linkUpAt = millis();
            warned = false;
            Serial.print("Link up, ");
            Serial.print(ETH.linkSpeed());
            Serial.println(ETH.fullDuplex() ? " Mbit/s full duplex" : " Mbit/s half duplex");
        } else {
            Serial.println("Link down: check the cable and the switch");
        }
    }
    const bool ip = ETH.hasIP();
    if (ip != hadIP) {
        hadIP = ip;
        if (ip) {
            Serial.print("Address after ");
            Serial.print(millis() - linkUpAt);
            Serial.print(" ms: ");
            printAddress();
        } else {
            Serial.println("Address lost");
        }
    }
    if (up && !ip && !warned && millis() - linkUpAt > DHCP_TIMEOUT_MS) {
        warned = true;
        Serial.println("No DHCP answer yet: is there a DHCP server on this network? Still waiting");
    }
    delay(10);
}
