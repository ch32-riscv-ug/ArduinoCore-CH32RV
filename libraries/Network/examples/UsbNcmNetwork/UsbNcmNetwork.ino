// en: The board as a USB network adapter (CDC-NCM): the PC gets an address from the board's DHCP server and opens
// en: http://192.168.7.1/ - a page with the board's uptime and the PC's address. Link up and down are logged.
// ja: ボードを USB のネットワークの口（CDC-NCM）にする。PC はボードの DHCP サーバーからアドレスをもらい、
// ja: http://192.168.7.1/ を開く（ボードの起動からの時間と PC のアドレスのページ）。リンクの上がり下がりを表示する。
// en: Wiring and operation: README.md.
// ja: 配線と操作: README.ja.md。

/* requires: USB_DEVICE, flash=60K, ram=20K */

#include <CH32RVUsbDevice.h>
#include <Network.h>
#include <NetworkUsbNcm.h>     // en: ncm.networkInterface()'s type / ja: ncm.networkInterface() の型

CH32RVUsbDeviceNet ncm;
static NetworkServer server(80);
static bool wasUp;

void setup()
{
    // en: Log on the hardware UART, apart from the USB link.
    // ja: 表示はハードウェアの UART。USB の線とは別。
    Serial.begin(115200, SERIAL_8N1, RX, TX);
    delay(100);
    // en: The board is 192.168.7.1; the PC is handed 192.168.7.2 and up. No gateway or DNS is offered, so the PC
    // en: keeps its own default route.
    // ja: ボードは 192.168.7.1、PC には 192.168.7.2 から配る。ゲートウェイと DNS は配らないので、PC の既定の経路は変わらない。
    ncm.dhcpServer(true);
    if (!USBDevice.begin() || !ncm.beginNetwork()) {
        Serial.println("USB network failed to start");
        return;
    }
    server.begin();
    Serial.println("USB network up: open http://192.168.7.1/");
}

void loop()
{
    const bool up = ncm.networkInterface() && ncm.networkInterface()->linkUp();
    if (up != wasUp) {
        wasUp = up;
        Serial.println(up ? "link up (the PC configured the adapter)" : "link down");
    }
    NetworkClient client = server.accept();
    if (!client) {
        return;
    }
    // en: Skip the request's lines up to the blank one, for at most a second.
    // ja: 要求の行を空行まで読み飛ばす（1 秒まで）。
    const uint32_t start = millis();
    while (client.connected() && millis() - start < 1000) {
        const String line = client.readStringUntil('\n');
        if (line.length() <= 1) {
            break;
        }
    }
    client.print("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\n");
    client.print("Hello from the board over USB.\r\nUptime: ");
    client.print(millis() / 1000);
    client.print(" s\r\nYour address: ");
    client.print(client.remoteIP());
    client.print("\r\n");
    client.stop();
    Serial.print("served ");
    Serial.println(client.remoteIP());
}
