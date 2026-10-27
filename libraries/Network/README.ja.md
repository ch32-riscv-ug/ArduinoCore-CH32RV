# Network

lwIP の上の IPv4 のネットワークです。API は ESP32 の Arduino コア 3.x の Network ライブラリに合わせています(`Network`、`NetworkInterface`、`NetworkClient`、`NetworkServer`、`NetworkUDP`)。方針は [docs/network-stack.ja.md](../../docs/network-stack.ja.md) にあります。

## 対象と今の状態

- 対象は、Ethernet の MAC を持つ系列です: CH32V208、V203RB、V307 / V317、V407 / V467、H415〜H417。ほかの系列でビルドすると、エラーで止まります。
- Ethernet(`ETH`)は、CH32V307(内蔵の 10 Mbit/s の PHY)、CH32V407(200 MHz で 45 Mbit/s、TLS 1.3)、CH32H417 の実機で動きます。ベンチの `eth` の試験で、DHCP、ICMP、UDP と TCP のエコー、TCP の両方向、HTTP、HTTPS を確かめています。V208 / V203RB / V317 / V467 / H415 / H416 はビルドのみです。
- USB の NCM(`NetworkUsbNcm`、`CH32RVUsbDeviceNet` の `beginNetwork()` など)は、これらの系列でビルドできますが、まだ実機では動かしていません。
- 例: [EthernetStatus](examples/EthernetStatus/README.ja.md)、[TcpEchoServer](examples/TcpEchoServer/README.ja.md)、[UdpEcho](examples/UdpEcho/README.ja.md)、[HttpGet](examples/HttpGet/README.ja.md)、[MdnsWebServer](examples/MdnsWebServer/README.ja.md)、[NetworkTime](examples/NetworkTime/README.ja.md)。どれも V307 で動かしています。
- 動作は、PC でビルドし、Linux の TCP/IP を相手に試しています(`tests/unit/network`)。固定アドレス、DHCP のクライアントとサーバー、SNTP、mDNS、ICMP、UDP、TCP(両方向、並列、こちらからの接続)、DNS を、パケットの落ちと入れ替わりのある回線でも確かめています。

## 使い方

```cpp
#include <Network.h>
// ETH などのドライバが NetworkInterface を用意する
NetworkServer server(80);

void setup() {
  ETH.begin();                 // DHCP。固定アドレスは ETH.config(ip, gw, mask, dns) を先に
  while (!ETH.hasIP()) delay(100);
  server.begin();
}

void loop() {
  NetworkClient client = server.accept();
  if (client) {
    client.print("HTTP/1.0 200 OK\r\n\r\nhello\r\n");
    client.stop();
  }
}
```

## USB の NCM(EspUsbDevice と同じ書き方)

```cpp
#include <CH32RVUsbDevice.h>
#include <Network.h>

CH32RVUsbDeviceNet usbnet;

void setup() {
  usbnet.dhcpServer(true);   // PC に 192.168.7.2 から配る(本機は 192.168.7.1)
  USBDevice.begin();
  usbnet.beginNetwork();
}
```

- 本機の側の MAC は、`CH32RVUsbDeviceNet::macAddress()`(PC の側が使う)の最後のビットを反転したものです。
- `defaultRoute(true)` を呼ばない限り、USB の NCM は本機の既定の経路になりません(Ethernet の邪魔をしない)。
- PC から受けたフレームは、4 KB の待ち行列を通して lwIP に渡します。空きが足りないときは USB で受け取りを止め、PC を待たせるので、取りこぼしません。

## 時刻合わせ(SNTP)と mDNS

```cpp
configTime(9 * 3600, 0, "pool.ntp.org");   // 日本時間。configTzTime("JST-9", ...) でも同じ
struct tm t;
if (getLocalTime(&t)) { /* ... */ }

MDNS.begin("sensor");                       // sensor.local
MDNS.addService("http", "tcp", 80);         // _http._tcp(ブラウザや Bonjour / Avahi から見える)
MDNS.addServiceTxt("http", "tcp", "path", "/");
```

- SNTP は、コアの時計(`settimeofday()`)を合わせます。1 時間ごとに合わせ直します。`time()`、`localtime_r()`、`strftime()` がその時計を読みます。
- mDNS は、Network ライブラリが始めたすべてのインターフェースで答えます。後から始めたものも含みます。サービスは 4 つまで、TXT は 1 つにつき 96 バイトまでです。問い合わせ(`queryHost()` など)はまだありません。
- 大きさ(V208): mDNS と SNTP を両方使うと、Flash が約 26 KB 増えます(lwIP の mDNS が 12 KB、newlib の時刻とタイムゾーンの処理など)。使わないスケッチには入りません。

## よく使われるライブラリ

次のライブラリが、手を入れずに動くことを PC 上で確かめています(2026-10-21)。

- MQTT: PubSubClient、ArduinoMqttClient。`NetworkClientSecure` を渡せば TLS でも動きます。
- HTTP クライアント: ArduinoHttpClient。HTTP と HTTPS。
- HTTP サーバー: aWOT。`NetworkServer::accept()` で受けた接続を渡します。

TLS のときは、ホスト名で接続してください(証明書の名前と照らし合わせるため)。

## 動きの決まり

- lwIP は OS なし(NO_SYS)で、`loop()` の後と `delay()` の中で回ります(コアの `ch32rv_net_task()`)。`loop()` の中で長く止まると、通信も止まります。
- 待つ呼び出し(`connect()`、`write()`、`Network.hostByName()`)は、待つ間もスタックを回します。`available()`、`read()`、`peek()`、`connected()`、`parsePacket()`、`accept()` も、データが無いときはスタックを 1 回回します。`Stream` の待ち付きの読み出し(`readStringUntil()`、`readBytes()` など)も、これで受信が進みます。
- 割り込みの中からは呼べません。
- `NetworkClient` は、受けたデータを自分のバッファ(TCP の窓と同じ大きさ)に写します。スケッチが読むにつれて窓が開くので、読むのが遅いと相手の送信が遅くなり、データは失われません。
- `write()` は、送信のバッファに空きができるまで待ちます。10 秒進まなければ諦めて、書けた分を返します。
- DHCP サーバー(`dhcpServer(true, leases)`)は、固定アドレスの次から `leases` 個を、1 時間ずつ貸します。同じ MAC には同じアドレスを貸します。ゲートウェイ(`dhcpAdvertiseGateway()`)と DNS(`dhcpDns()`)は、指定したときだけ配ります。転送しない機器がゲートウェイを配ると、相手の PC の既定の経路を奪うからです。
- `NetworkUDP` は、受けたデータグラムを 4 つまで溜めます。それ以上は捨てます。送れるのは 1 つ 1472 バイトまでです。
- IPv4 だけです。

## 大きさ(CH32V208、2026-10-21)

DHCP、TCP のサーバーとクライアント、UDP、DNS を使うスケッチで、ネットワークの分は Flash 約 44 KB です(lwIP 約 37 KB、このライブラリ約 6 KB)。RAM は、静的に約 22 KB(lwIP の送信用のヒープ 10 KB、受信用のプール 9 KB など)と、接続 1 本ごとに 2.9 KB です。V407 / H41x では、窓とプールを大きくしています(静的に約 60 KB)。構成は `src/lwipopts.h` です。

## ドライバを書くとき

`NetworkInterface` を継承して、次を用意します。どれも割り込みの外(タスクの文脈)で呼びます。

- `transmitFrame(frame, length)`: Ethernet のフレームを 1 つ送る。
- 受けたフレームは `frameReceived(frame, length)` に渡す。`poll()`(タスクから呼ばれる)の中で、割り込みが溜めたものを渡す形にします。
- リンクの状態は `setLinkUp(up)` で知らせる。
- 始めるときに `netifStart(mac)`、止めるときに `netifStop()`。

## ライセンス

lwIP は BSD-3-Clause です(`src/lwip-src/COPYING`)。取り込んだ版とファイルのハッシュは `vendor/lwip.lock.toml` にあります。
