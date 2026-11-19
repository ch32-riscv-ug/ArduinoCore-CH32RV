# UsbNcmNetwork

[English](README.md)

ボードを USB のネットワークの口（CDC-NCM）にします。PC はボードの DHCP サーバーからアドレスをもらい、ブラウザで
`http://192.168.7.1/` を開くと、ボードの起動からの時間と PC のアドレスが出ます。リンクの上がり下がりを UART に表示します。

## 必要なものと配線

- Ethernet の MAC と USB デバイスのある石（V307、V407、H417 など。Network ライブラリの対象の系列）と、PC への USB の線。
- 115200 bps・8N1 の UART モニタを `RX` / `TX` に。
- PC の側: Linux と macOS は NCM の口として認識します。Windows 10 以降も NCM の標準のドライバーで動きます。

## 操作と期待する結果

書き込んで USB をつなぐと、PC に有線のネットワークの口が 1 つ増え、192.168.7.2 などが割り当てられます。UART には次のように出ます。

```
USB network up: open http://192.168.7.1/
link up (the PC configured the adapter)
served 192.168.7.2
```

ブラウザ（または `curl http://192.168.7.1/`）には次が返ります。

```
Hello from the board over USB.
Uptime: 42 s
Your address: 192.168.7.2
```

## API と処理の流れ

- `CH32RVUsbDeviceNet ncm` が USB の NCM の機能です。`dhcpServer(true)` のあと `USBDevice.begin()`、`ncm.beginNetwork()` の順で、
  IP の口が上がります。
- ゲートウェイと DNS は配らないので（`dhcpAdvertiseGateway()` を呼ばない限り）、PC の既定の経路はそのままです。
- `ncm.networkInterface()` が Network ライブラリの口で、`linkUp()` は PC が口を設定すると true になります。
- Web サーバーは Ethernet と同じ `NetworkServer` / `NetworkClient` です。
