# UsbHostNetwork

[English](README.md)

ボードを USB のネットワークの機器（CDC-NCM か CDC-ECM）の USB ホストにし、その上で IP を動かします
（`CH32RVUsbHost::networkAttachNetif()`、EspUsbHost と同じ呼び方）。DHCP でアドレスをもらい、ゲートウェイの
ページを 10 秒ごとに HTTP で取って、ステータス行を表示します。

## 必要なものと配線

- Network ライブラリの対象の系列（Ethernet の MAC のある石: V307、V407、H417 など）で、USB ホストの口のあるボード。
- 相手の機器: CDC-NCM か CDC-ECM のもの。たとえば EspUsbDevice の例 `UsbNetwork` を書いた ESP32-S3 / P4
  （192.168.7.1 で DHCP サーバーと Web サーバーが動く）、`UsbNcmNetwork` を書いた CH32RV のボード、NCM / ECM の USB の LAN アダプター。
- 115200 bps の `Serial`。

## 操作と期待する結果

書き込んで相手をつなぐと、次のように出ます（EspUsbDevice の `UsbNetwork` が相手のとき）。

```
USB network device attached, asking DHCP
... 192.168.7.2 ...（MAC・アドレス・ゲートウェイ）
GET http://192.168.7.1/ -> HTTP/1.1 200 OK
```

相手が DHCP サーバーを持たないときは、`CH32RVUsbHostNetworkConfig` の `dhcpClient = false` と `ip` / `gateway` /
`subnet` で決まったアドレスにします。

## 注意

- 開けるのは 1 台です。NCM の受けのブロックは 3200 バイトまで（大きいものを出す機器には小さくしてもらう）。
- `networkAttachNetif()` の間、フレームはインターフェースのものです。`onNetworkFrame()` / `networkReadFrame()` は使いません。
- V407 と ESP32-P4（EspUsbDevice の NCM）の組で、DHCP と TCP の往復を確かめています（2026-11-22、usb-peer-host-net）。
