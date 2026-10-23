# UsbNetworkPing

English: [README.md](README.md)

CDC-NCM の Ethernet フレームから ARP と IPv4 ping に応答します。IP スタックを持たず、Web サーバーやインターネット接続は提供しません。

## 必要なものと配線

CDC-NCM を利用できる PC、データ用 USB ケーブル、UART モニタ。ビルド対象と容量条件は付属の `sketch.yaml` を参照してください。

## 操作と期待する結果

1. 新しく現れた USB ネットワークインターフェースへ、PC側の静的 IPv4 `192.168.7.2/24` を設定します。gateway/DNS は不要です。
2. `ping 192.168.7.1` を実行します。
3. UART に約2秒ごとに出る `link=... frames=... arp=... ping=...` が増えることを確認します。
4. OS が別の経路へ送る場合は、このインターフェースを指定して ping します。

## 設定と制約

DHCP はありません。OS が NIC を作らない場合は先に NCM driver の対応・バインドを確認します。変更する IP は `MY_IP` と PC の設定を揃えます。

標準的な短い、fragment なし・IPv4 option なしの ping で試します。この例の parser は一般用途の入力検証や IP 再構成を実装していません。callback では返信を準備し、`loop()` から送信します。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `net.onFrame(callback)` | Ethernet frame を `deliverEvents()` で受け取ります。USB の NCM ヘッダをスケッチで外す必要はありません。 |
| `net.sendFrame(data, length)` | Ethernet header を含む frame を送ります。`bool` で成否を確認します。 |
| `net.macAddress()` | USB ネットワークの MAC 情報を得ます。この例では PC 側と応答側が別 MAC になるようにします。 |
| `net.linkUp()` | USB ネットワークのリンク状態です。IP 設定完了や ping 到達性の保証ではありません。 |

callback が渡す frame のアドレスをあとまで持たず、返信用の配列へ必要な内容をコピーします。
この例の返信領域は一つなので、未送信の返信がある間に次の要求が来る場合や、`sendFrame()` が失敗する場合を
扱う用途では、キューや再試行を追加します。

PC の静的 IP と `MY_IP` は同じサブネット内の別アドレスにします。既存 LAN と重複するなら両方を変更します。
より一般的なネットワーク機能へ広げる場合は、長さ・header・checksum 等を検証する IP スタックの利用を検討します。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[UsbNetworkPing.ino](UsbNetworkPing.ino)
