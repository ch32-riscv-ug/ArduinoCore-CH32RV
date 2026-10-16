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

S3 側の制約に合わせて試験の形を変えたところがあります。詳しくは下の「ESP32-S3 側の挙動」と、各試験の docstring を
見てください。

ホスト側（usb-peer-host*）は、ホスト機能を持つ DUT が必要です。CH32X035 では R8T6 / C8T6 / G8U6 / G8R6 だけが
ホストを持ち、ベンチの WeAct 板の F8U6 は持ちません（`begin()` が `Unsupported`）。V20x / V30x の USBFS でも動きます。
CH32X035C8T6 と ESP32-S3 で usb-peer-host / usb-peer-host-classes の両方が通っています（デバイス側の usb-peer も
同じ組み合わせで通ります。工場 ISP の VID は C8T6 が 4348、F8U6 が 1a86 で、試験はどちらも受け付けます）。

## 配線と設定

- DUT の USB（D+ / D- / GND）を S3 の USB OTG ポートへ。VBUS は使いません（DUT と S3 はそれぞれ給電）。
- S3 の制御は UART0（ボードの USB シリアル）。ベンチ設定の target に `usb_peer_port`（安定した by-id のパス）を書きます。
- S3 のコアとライブラリは sketch.yaml で版を固定します（esp32 3.3.12、EspUsbHost 2.10.0、EspUsbDevice 2.6.0）。
  run_hardware.py は Arduino の data / downloads を `~/.cache/ch32rv-core-bench/arduino` に置き、実行のたびに
  ESP32 のツールチェーンを取り直さないようにしています（~/.arduino15 には触れません）。

## ESP32-S3 側の挙動

対向試験で見つけた、S3 側（ESP32-S3 本体、EspUsbHost 2.10.0、EspUsbDevice 2.6.0）の変わった動きです。DUT の不具合と
見分けるために残しています。版を上げたときは、直っているかを確かめて、回避を外せるか見直します。

### ESP32-S3 本体

- **書き込みや起動の直後、OTG ポートに内蔵の USB シリアル / JTAG が出る。** S3 は同じ USB PHY を内蔵の
  USB Serial/JTAG（303a:1001、製品名 "USB JTAG/serial debug unit"）と OTG で共有していて、起動時は JTAG 側が
  つながり、スケッチの USB が始まると OTG に切り替わります。DUT がホストのときは、まず 303a:1001 がつながり、
  すぐ切れてから S3 のデバイスがつながるように見えます。試験は目的の VID / PID が出るまで待ちます。
- **UART0（ボードの USB シリアル）を開くと S3 がリセットすることがある。** 自動書き込み用の DTR / RTS が EN と
  BOOT につながっているためで、ポートを開くときの線の状態によっては再起動し、DUT には切断と再接続に見えます。
  手で S3 にコマンドを送るときは、DTR と RTS を落としてから開きます。

### EspUsbHost 2.10.0（S3 がホスト: usb-peer / -classes / -media / -net）

- **ホストのチャネルが 8 本しかない。** usb_contracts の CDC・HID・MIDI を開くと、NCM の分が残りません。
  NCM は usb_net として別のケースにしています。
- **バルクの UVC を読みに来ない。** `videoStart()` は true を返しますが、IN を一度も取りに来ません（payloads=0）。
  アイソクロナスなら届くので、usb-peer-media のカメラはアイソクロナスでビルドします（build_opt.h）。バルクは
  PC ホストの試験で確かめます。
- **アイソクロナスを 2 本同時に流すとパケットを落とす**（マイク入力 + スピーカー出力、マイク + カメラ）。しかも
  `audioInputStop()` が無く、一度始めたマイクを止められません。usb-peer-media はスピーカー → カメラ → マイクの順に
  1 本ずつ流します。
- **マイク入力を毎フレーム取りに来ない。** 32 kHz 16 bit モノラル（1 ms あたり 64 バイト）が約 46 KB/s になります。
  DUT 側は書き込みを拒まれていない（ホストが取りに来た分だけ送っている）ので、試験は連番が途切れないことだけを
  判定し、レートは記録に残します。
- **`audioSend()`（押し込み型）は送れない分を捨てる。** 待たずに受け取り、間に合わない分が欠けます。
  `onAudioOutputRequest()`（引き出し型、足りない分は無音で埋める）なら欠けないので、試験はこちらを使い、合計値の
  一致と送った量以上が届いたことを見ます。
- **`sendHIDVendorOutput()` はデータの先頭バイトをレポート ID として扱う。** CH32RVUsbHost は ID を別の引数で
  付けるので、データの並びが 1 バイトずれて見えます。

### EspUsbDevice 2.6.0（S3 がデバイス: usb-peer-host / -classes）

- **IN エンドポイントが 4 本まで。** HID・CDC・MSC・MIDI・ベンダー・CCID を一度に出せないので、S3 のデバイス
  構成を host_hid と host_classes の 2 つに分けています。
- **`sendHidReport()`（マウスの `move()` など）は timeoutMs を使わない。** 前のレポートがまだホストに取られて
  いないと、待たずに即 false を返します。「押す → 離す」を続けて送ると 2 つ目が必ず失敗するので、S3 のスケッチでは
  キーボード・マウス・コンシューマのどのレポートも、取られるまで再試行します（最大 500 ms）。キーボードの `write()` は
  キーの間に 5 ms 待つだけなので、ホストが取りに来るのが遅いと途中で失敗します（実機で一度起きた）。
- **RAM ディスクの `attach()` はメディアを「あり」にしない。** `EspUsbDeviceMscRamDisk` /
  `EspUsbDeviceMscFatRamDisk` の `attach()` は読み書きのコールバックを入れるだけで、`mediaPresent(true)` を呼びません
  （`EspUsbDeviceMscFirmwareDisk` は呼びます）。このままだと TEST UNIT READY が失敗し続け、TinyUSB の MSC ホストが
  上限なく再試行するため、同じデバイスの HID や CDC まで含めてマウントされません。S3 のスケッチで
  `msc.mediaPresent(true)` と `msc.isWritable(true)` を呼んでいます。
- **`keyboard.ledState()` は構造体を返す。** LED のビットは `.leds` です。`%u` でそのまま出すと、Num Lock などの
  フラグも混ざった値（3 のとき 65795）になります。

