# USB Host サンプルの使い方

[English](README.md)

ボード自身を USB Host にして、キーボード、マウス、USB シリアル、MIDI、MSC、Vendor、CCID を接続します。最初は [DeviceInfo](DeviceInfo/README.ja.md) で列挙・配線を確認します。PC は UART ログを見る役で、Host のデータ線へ接続する機器は USB Device です。

## 型番と配線

Host の有無は系列名だけでなく正確な型番で確認します。Device として動くボードが、Host としても動くとは限りません。
使用できる controller・端子・クロックと実機確認の範囲は [ライブラリの対応表](../README.ja.md#対応) を参照してください。

必要な接続は USB Device の D+/D−、GND と、Device に供給する5 V VBUSです。VBUS は基板の電源回路・スイッチから供給します。`usb.begin()` は USB-C の電源ロールや5 V供給回路を構成しません。Device 用 USB-C コネクタの CC/Rd があるだけでは Host 用の接続条件を満たしません。Type-C Host 接続を使う場合は、ボード側の Source/CC 回路も回路図で確認します。

ログは全例とも `Serial` の115200 bps・8N1です。UART アダプタの RX←ボード TX、TX→ボード RX、GND を接続し、ボードの I/O 電圧を使います。USBSerial/VendorBulk はその UART から送信も行うので RX側の配線も必要です。

## ビルドと動作

Arduino IDE の「スケッチ例 → CH32RVUsbHost」から選び、正確な Part Number を設定します。CLI でリポジトリのルートからビルドする例です。

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:CH32X035:pnum=CH32X035C8T6 \
  libraries/CH32RVUsbHost/examples/DeviceInfo
```

1. まず Device を外した状態で書き込み、UART モニタを開きます。
2. Device 側へ供給する VBUS と USB データ配線を確認してから接続します。
3. DeviceInfo で VID/PID と interface を確認し、目的のクラス例へ進みます。
4. サンプルと同じ機能がある機器で、送信・受信・切断・再接続を確認します。

`sketch.yaml` はコンパイル条件の宣言です。Host の無い型番でも系列向け profile が生成される場合があり、ビルド成功と Host の実機対応は別です。生成物の扱いは[ビルド規則](../../../docs/examples-build-rules.ja.md)を参照してください。

## task と機器の選択

Host の処理は `loop()` の後と `delay()` 中に進みます。長い計算ループでは `usb.task()` を呼びます。callback は、各例が `loop()` の先頭で呼ぶ `deliverEvents()` の中でだけ動きます。待つ API も使えますが、callback を短くしてフラグを立て、`loop()` から open/転送する例もあります。考え方は [スケッチの組み立て方](../../../docs/sketch-guide.ja.md) にあります。

| API・状態 | 確認すること |
|---|---|
| `usb.begin()` / `usb.ready()` | Host controller の起動。目的の USB Device が使用可能という意味ではない |
| `onDeviceConnected()` | 機器の列挙通知。callback の `address` で今回の機器を識別する |
| `serialReady()` / `midiReady()` / `mscReady()` 等 | 目的のクラスが使える状態か。媒体やカードの準備はさらに別に確認する |
| open・転送 API の戻り値 | 開始や転送の成否。`false` なら直後の `lastErrorName()` を確認する |

既定の address は最初の該当機器を選びます。複数機器では callback の `address` を保持し、API の address 引数または
CDC wrapper の `setAddress()` / `setPort()` で指定します。抜き挿し後の address を固定値で仮定しません。
受信 callback の参照・ポインタを保存するなら、自分の領域へ必要な値をコピーします。

待つ API はスケッチの続きの処理を遅らせます。割り込みから呼ばず、callback ではフラグだけを立てて
`loop()` で処理する形にすると、接続通知と転送処理を分けて考えられます。切断後の未送信データや状態を
残すか捨てるかも用途に応じて決めます。

複合機器・hub・クラスごとの対応範囲や設定の上限は [ライブラリ README](../README.ja.md) を参照してください。
Device 側のサンプルを対向機器にすると、通信の方向と期待する応答を照合できます。

## よくある問題

| 症状 | 確認すること |
|---|---|
| `Unsupported` | 正確な型番と Host の対応表 |
| `Busy` | 同じ controller で Device が起動していないか。ロール切替前に `end()` |
| 接続通知がない | VBUS、GND、D+/D−、Device として動く端子か、UART ログ |
| VID/PID は出るが操作できない | interface class/protocol と対応クラス、`*Ready()`、open の成否 |
| `Timeout` / `Stalled` | timeout は無応答や処理待ち、STALL は相手が request を拒否。`lastErrorName()` と descriptor を確認 |
| 大きな複合機器が列挙できない | ライブラリの構成・記述子の容量条件と使用 RAM |
| カードリーダーで ready にならない | 媒体の有無とライブラリの既知の制約 |
| シリアルの取りこぼし | UART/USB双方の読み出し頻度、戻り値、build時 FIFO とリング容量 |

詳細な API と設定値は[ライブラリ README](../README.ja.md)、実機の保証範囲は[peer テスト](../../../tests/peer/usb/README.ja.md)を参照してください。

## サンプル一覧

| 例 | 内容 |
|---|---|
| [CcidReader](CcidReader/README.ja.md) | USB CCID リーダーでカードを起動し、ATR と SELECT MF の status word を表示します。 |
| [DeviceInfo](DeviceInfo/README.ja.md) | 接続機器の identity、文字列、interface、endpoint を UART に表示します。Host 配線の最初の確認に使います。 |
| [Keyboard](Keyboard/README.ja.md) | 接続した USB キーボードの ASCII 入力を UART に表示し、Caps/Num Lock LED を更新します。 |
| [MIDI](MIDI/README.ja.md) | USB MIDI 機器の受信メッセージを UART に表示し、Note 60 を周期的に送ります。 |
| [MSCBlockDump](MSCBlockDump/README.ja.md) | USB メモリの INQUIRY と容量を表示し、先頭ブロックを hex dump します。ファイルシステムは使いません。 |
| [Mouse](Mouse/README.ja.md) | USB マウスの相対移動、ホイール、ボタン bitmap を UART で観察します。 |
| [USBSerial](USBSerial/README.ja.md) | CDC-ACM または対応 USB-UART アダプタを接続し、ボードの UART モニタと双方向に中継します。 |
| [VendorBulk](VendorBulk/README.ja.md) | vendor-specific bulk interface を開き、UART 入力を OUT へ送り、IN の受信を hex 表示します。 |
