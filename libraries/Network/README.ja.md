# Network

lwIP の上の IPv4 のネットワークです。API は ESP32 の Arduino コア 3.x の Network ライブラリに合わせています(`Network`、`NetworkInterface`、`NetworkClient`、`NetworkServer`、`NetworkUDP`)。方針は [docs/network-stack.ja.md](../../docs/network-stack.ja.md) にあります。

## 対象と今の状態

- 対象は、Ethernet の MAC を持つ系列です: CH32V208、V307 / V317、V407 / V467、H415〜H417。ほかの系列でビルドすると、エラーで止まります。
- **Ethernet の MAC のドライバはまだありません。** USB の NCM(`NetworkUsbNcm`、`CH32RVUsbDeviceNet` の `beginNetwork()` など)はありますが、これらの系列の USB デバイスのドライバがまだ無いので、実機では動きません。
- 動作は、PC でビルドし、Linux の TCP/IP を相手に試しています(`tests/unit/network`)。固定アドレス、DHCP のクライアントとサーバー、ICMP、UDP、TCP(両方向、並列、こちらからの接続)、DNS を、パケットの落ちと入れ替わりのある回線でも確かめています。

## 使い方(ドライバができたとき)

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

## 動きの決まり

- lwIP は OS なし(NO_SYS)で、`loop()` の後と `delay()` の中で回ります(コアの `ch32rv_net_task()`)。`loop()` の中で長く止まると、通信も止まります。
- 待つ呼び出し(`connect()`、`write()`、`Network.hostByName()`)は、待つ間もスタックを回します。`available()`、`connected()`、`parsePacket()`、`accept()` も、データが無いときはスタックを 1 回回します。
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
