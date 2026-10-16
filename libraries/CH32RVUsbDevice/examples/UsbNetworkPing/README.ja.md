# UsbNetworkPing

English: [README.md](README.md)

CDC-NCM の Ethernet フレームから ARP と IPv4 ping に応答します。IP スタックを持たず、Web サーバーやインターネット接続は提供しません。

## 必要なものと配線

CDC-NCM を利用できる PC、データ用 USB ケーブル、UART モニタ。RAM 16 KiB以上の profile 条件があります。

## 操作と期待する結果

1. 新しく現れた USB ネットワークインターフェースへ、PC側の静的 IPv4 `192.168.7.2/24` を設定します。gateway/DNS は不要です。
2. `ping 192.168.7.1` を実行します。
3. UART に約2秒ごとに出る `link=... frames=... arp=... ping=...` が増えることを確認します。
4. OS が別の経路へ送る場合は、このインターフェースを指定して ping します。

## 設定と制約

DHCP はありません。OS が NIC を作らない場合は先に NCM driver の対応・バインドを確認します。変更する IP は `MY_IP` と PC の設定を揃えます。

標準的な短い、fragment なし・IPv4 option なしの ping で試します。この例の parser は一般用途の入力検証や IP 再構成を実装していません。callback では返信を準備し、`loop()` から送信します。

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [USB 全体のガイド](../../../../docs/usb.ja.md)

[UsbNetworkPing.ino](UsbNetworkPing.ino)
