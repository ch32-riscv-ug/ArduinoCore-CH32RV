// en: The board as the USB host of a USB network device (CDC-NCM / ECM: a USB Ethernet adapter, an ESP32 or a
// en: CH32RV board running an NCM device): the IP stack runs over it, an address by DHCP, then an HTTP GET of
// en: the gateway's page every 10 s.
// ja: USB のネットワークの機器（CDC-NCM / ECM: USB の LAN アダプター、NCM のデバイスにした ESP32 や CH32RV のボード）の
// ja: USB ホストになる。その上で IP を動かし、DHCP でアドレスをもらって、ゲートウェイのページを 10 秒ごとに HTTP で取る。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: USBFS, flash=60K, ram=20K */

#include <CH32RVUsbHost.h>
#include <Network.h>
#include <NetworkUsbHost.h>   // en: usb.networkInterface()'s type / ja: usb.networkInterface() の型

CH32RVUsbHost usb;
static bool attached, hadIP;
static uint32_t lastGet;

void setup()
{
    Serial.begin(115200);
    if (!usb.begin()) {
        Serial.print("usb.begin() failed: ");
        Serial.println(usb.lastErrorName());
    }
}

static void httpGet(IPAddress server)
{
    NetworkClient client;
    if (!client.connect(server, 80)) {
        Serial.println("no HTTP server at the gateway");
        return;
    }
    client.print("GET / HTTP/1.0\r\nHost: ");
    client.print(server);
    client.print("\r\n\r\n");
    // en: The status line only; the rest of the page is read and dropped.
    // ja: 表示はステータス行だけ。残りは読み捨てる。
    String status = client.readStringUntil('\n');
    Serial.print("GET http://");
    Serial.print(server);
    Serial.print("/ -> ");
    Serial.println(status);
    while (client.connected() || client.available()) {
        if (client.available()) {
            client.read();
        }
    }
    client.stop();
}

void loop()
{
    deliverEvents();
    if (!attached) {
        // en: Opens the first network device once one is enumerated; DHCP starts by itself.
        // ja: ネットワークの機器が列挙されたら最初の 1 台を開く。DHCP は自動で始まる。
        if (usb.networkAttachNetif()) {
            attached = true;
            Serial.println("USB network device attached, asking DHCP");
        }
        delay(200);
        return;
    }
    NetworkUsbHost *ifc = usb.networkInterface();
    if (ifc->hasIP() && !hadIP) {
        hadIP = true;
        Serial.println(*ifc);   // en: MAC, address, gateway / ja: MAC、アドレス、ゲートウェイ
    }
    if (!usb.networkReady()) {
        // en: The device went away: let go of the interface and wait for the next one.
        // ja: 機器が抜けたら、インターフェースを放して次を待つ。
        usb.networkDetachNetif();
        attached = hadIP = false;
        Serial.println("USB network device gone");
        return;
    }
    if (hadIP && millis() - lastGet >= 10000) {
        lastGet = millis();
        httpGet(ifc->gatewayIP() ? ifc->gatewayIP() : IPAddress(192, 168, 7, 1));
    }
}
