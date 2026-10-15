# peer/usb — ESP32-S3 との USB 対向試験

スケッチは規約どおり tests/sketch_support に置き、run_hardware.py がケースの作業ディレクトリへ
`peer_s3/` として並べます（pytest-embedded-arduino-cli の peer の置き方）。

DUT（CH32）の USB ポートを ESP32-S3 の USB OTG ポートにつなぎ、役割ごとに別のケースとして試験します
（[テスト計画](../../TEST_PLAN.ja.md) の「USB peer 計画」）。S3 側は独立した実装（EspUsbHost は ESP-IDF の
USB ホスト、EspUsbDevice は TinyUSB を S3 の OTG で動かすもの）なので、通ることが相互運用の確認になります。

| ケース | DUT の役割 | DUT スケッチ（sketch_support/） | S3（sketch_support/usb_peer/） | 確かめること |
|---|---|---|---|---|
| usb-peer | デバイス | usb_contracts | device/peer_s3（EspUsbHost） | 列挙、CDC（往復・2000 バイト・ボーレート）、キーボード、ベンダー HID、LED、MIDI、再接続、DFU_DETACH → 工場 ISP → 復帰 |
| usb-peer-classes | デバイス | usb_classes | device/peer_s3 | ベンダーバルクと要求、MSC（INQUIRY・容量・ブロックの読み書き・FAT 上のファイルの読み書き）、CCID（ATR・APDU） |
| usb-peer-media | デバイス | usb_media（カメラはアイソクロナス） | device/peer_s3 | スピーカー（合計値の一致）、カメラ（パターン）、マイク（連番） |
| usb-peer-net | デバイス | usb_net | device/peer_s3 | NCM のフレームの往復 |
| usb-peer-host | ホスト | usb_peer_host | host_hid/peer_s3（EspUsbDevice: HID・CDC・MSC） | 列挙と文字列、CDC、キーボード・マウス・コンシューマ、LED、ベンダー HID、MSC |
| usb-peer-host-classes | ホスト | usb_peer_host | host_classes/peer_s3（EspUsbDevice: MIDI・ベンダー・CCID） | MIDI、ベンダーバルクと要求、CCID |

S3 の制約で試験の形を合わせたところ（詳しくは各試験の docstring）:

- S3 の USB ホストはチャネルが 8 本で、usb_contracts の CDC・HID・MIDI を開くと NCM の分が残らないため、NCM は
  usb_net として分けています。
- EspUsbHost 2.10.0 はバルクの UVC を開始しても読みに来ないため、usb-peer-media のカメラはアイソクロナスで
  ビルドします（build_opt.h）。バルクは PC ホストの試験で確かめます。
- S3 はアイソクロナスを 2 本同時に流すとパケットを落とし、マイクを止める API もないため、スピーカー → カメラ →
  マイクの順に行います。マイクは S3 が毎フレーム取りに来ない（約 46 KB/s）ので、連番の連続だけを判定し、
  レートは記録に残します。
- S3 の IN エンドポイントは 4 本までなので、ホスト側の試験は S3 のデバイス構成を 2 つ（hid / classes）に分けています。

ホスト側（usb-peer-host*）は、ホスト機能を持つ DUT が必要です。CH32X035 では R8T6 / C8T6 / G8U6 / G8R6 だけが
ホストを持ち、ベンチの WeAct 板の F8U6 は持ちません（`begin()` が `Unsupported`）。V20x / V30x の USBFS でも動きます。

## 配線と設定

- DUT の USB（D+ / D- / GND）を S3 の USB OTG ポートへ。VBUS は使いません（DUT と S3 はそれぞれ給電）。
- S3 の制御は UART0（ボードの USB シリアル）。ベンチ設定の target に `usb_peer_port`（安定した by-id のパス）を書きます。
- S3 のコアとライブラリは sketch.yaml で版を固定します（esp32 3.3.12、EspUsbHost 2.10.0、EspUsbDevice 2.6.0）。
  run_hardware.py は Arduino の data / downloads を `~/.cache/ch32rv-core-bench/arduino` に置き、実行のたびに
  ESP32 のツールチェーンを取り直さないようにしています（~/.arduino15 には触れません）。
