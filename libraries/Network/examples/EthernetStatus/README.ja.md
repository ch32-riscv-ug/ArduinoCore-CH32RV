# EthernetStatus

[English](README.md)

Ethernet を DHCP で起動し、MAC アドレスを表示します。そのあと、リンク（速さ・全二重かどうか）とアドレス（IP、マスク、ゲートウェイ、DNS）が変わるたびに表示します。ほかのネットワークの例の前に試す、最初の一歩です。

## 操作

リセットすると MAC アドレスが出て、次に `Link up, 100 Mbit/s full duplex`（または 10）と、DHCP にかかった時間付きのアドレスが出ます。ケーブルを抜くと `Link down` が出て、挿し直すとリンクとアドレスが戻ります。DHCP サーバーが無いと、15 秒後に `No DHCP answer yet` を出して、そのまま待ち続けます。

## API と改造時の注意

`ETH.begin()` はドライバと DHCP を始めて、すぐ戻ります。false になるのは、ボードに Ethernet が無いときと、PHY が応答しないときだけです。`linkUp()` と `hasIP()` は状態を読むだけで、待ちません。固定アドレスにするときは、`begin()` の前に `ETH.config(ip, gateway, mask, dns)` を呼びます。

## 実行前の準備

Ethernet の MAC を持つボードを選び（生成された profile は CH32V307。V208、V317、V407 / V467、H415〜H417 も同じ書き方で動きます）、RJ45 を DHCP サーバーのあるネットワークにつなぎます。ログはハードウェア `Serial` の 115200 baud・8N1 です。アダプタ RX をボード TX、アダプタ TX をボード RX へ接続し、GND を共通にします。基板の USB 端子にはこの UART は出ていません。

通信の処理は `delay()` の中と各 `loop()` の後で進みます。スケッチが poll の関数を呼ぶ必要はありません。ビルドが通っても、ケーブルやネットワークの確認にはなりません。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
