# USB Device サンプルの使い方

[English](README.md)

ボードを PC 等の USB ホストへ接続し、USB の各機能を試すサンプルです。
まず [SerialUSBEcho](SerialUSBEcho/README.ja.md) で接続を確認し、目的のクラスへ進みます。
各例の README には、配線・操作・期待する結果と、API の使い方・改造時の注意をまとめています。

## 準備と最初の確認

1. [USB の対応表](../../../docs/usb.ja.md#対応状況) で、正確な型番・controller・必要なクロックを確認します。
   系列向けのビルド成功だけでは、そのパッケージの端子や実機動作は確認できません。
2. ボードの回路図で MCU の USB データ端子につながるコネクタを確認し、データ通信できるケーブルで PC へ接続します。
   USB-UART ブリッジにつながるコネクタと区別します。
3. Arduino IDE の「ファイル → スケッチ例 → CH32RVUsbDevice」から例を開き、ボードと Part Number を設定します。
   初回はそのボードで使える書き込み経路で書き込みます。
4. CDC、HID、音声など、そのクラスを扱う PC アプリを開いて入出力を試します。すべてが COM ポートになるわけではありません。

CLI はリポジトリのルートから、導入済みの core を指定してビルドできます。

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:CH32X035:pnum=CH32X035F8U6 \
  libraries/CH32RVUsbDevice/examples/SerialUSBEcho
```

開発中の core を調べる場合は、CLI が参照する platform がこの作業ツリーであることも確認します。
`sketch.yaml` の扱いと容量条件は [examples のビルド規則](../../../docs/examples-build-rules.ja.md) を参照してください。

## UART ログと USB 通信を分ける

多くの例の診断ログは `Serial` のハードウェア UART へ出ます。USB CDC の `SerialUSB` / `UsbSerial` とは別です。
通信条件はスケッチの `Serial.begin(...)` に合わせます。

| ボード | UART アダプタ |
|---|---|
| TX | RX |
| RX | TX（コマンド入力がある場合） |
| GND | GND |

UART の電圧はボードの I/O 電圧に合わせます。`RX` / `TX` 等の実際の端子は board/variant と回路図で確認します。
USB が起動しない場合にも UART ならエラーを読めます。UART を使わない例は個別 README に記載しています。

## 宣言から通信まで

通常はグローバルにデバイスと function を宣言し、設定後にデバイス全体を起動します。

```cpp
#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceCdcSerial console(device, "Console");

void setup() {
  Serial.begin(115200, SERIAL_8N1, RX, TX);
  CH32RVUsbDeviceConfig config;
  config.product = "My USB console";
  if (!device.begin(config)) {
    Serial.println(device.lastErrorName());
  }
}

void loop() {
  while (console.available()) {
    const int value = console.read();
    if (value >= 0) console.write(static_cast<uint8_t>(value));
  }
}
```

function は同じ `device` に結び付けます。定義済みの `USBDevice` / `SerialUSB` で始める方法は
[SerialUSBEcho](SerialUSBEcho/README.ja.md)、複数機能の組み合わせは
[CustomDevice](CustomDevice/README.ja.md) を参照してください。
識別情報の文字列、独自 descriptor、差し替えバッファは、USB が使っている間有効に保ちます。

| 状態・API | 意味 |
|---|---|
| `device.begin(config)` が `true` | 内部の起動に成功。PC の列挙完了はまだ保証しない |
| `device.started()` | スタックが起動している |
| `device.mounted()` / `device.ready()` | ホストがデバイスを構成した。Device ではこの二つは同じ意味 |
| CDC の `connected()` / `operator bool` | ホストの DTR が有効。端末を開いたかを判断するために使う |
| 送信 API の成功 | API がそのデータを受け付けた。PC アプリが意味を解釈・処理した保証ではない |

CDC の `begin(baud)` は USB バス速度を変えません。ハードウェア UART へ中継する場合、UART 側の baud は別に設定します。
`write()` は生バイト、`print()` は文字列化した値です。API ごとに待ち方や戻り値が異なるので、
CDC、Vendor、Audio の `write()` を同じ動作と仮定しません。

## 内部処理と callback

USB の内部処理は `loop()` の後と `delay()` 中に進みます。スケッチが戻らない長い計算では
`device.task()` で進める方法もありますが、基本は処理を区切って `loop()` へ戻します。

| 種類 | 例 | 呼ばれる場所・書き方 |
|---|---|---|
| 通知 | CDC の `onRx()`、HID の Output、バス通知、映像の完了通知 | `loop()` の `deliverEvents()` で実行。最新状態にまとまる通知もある |
| 応答を作る provider・判定 | MSC の読み書き・START STOP、CCID の APDU、Vendor 制御要求、映像の payload 生成、DFU の detach 判定 | USB の処理中に実行。短くし、`delay()` や待ち続けるループを入れない |
| ポーリング | CDC の `available()`、MIDI の `readPacket()`、Audio の `pollEvent()` | スケッチから読み出す。この方式だけなら通知用の `deliverEvents()` は不要 |

通知が来るまでの保存は無制限ではありません。クラスによって相手を待たせたり、通知がまとまったり、
記録を取りこぼしたりします。`onRx()` の引数をメッセージ境界と考えず、実際に読み出せた長さを扱います。
callback の受信ポインタをあとで使う場合は自分の配列へコピーします。
[SerialEvents](SerialEvents/README.ja.md) と [スケッチの組み立て方](../../../docs/sketch-guide.ja.md) で流れを確認できます。

## 改造するときに確認すること

サンプルのVID/PID `1209:0001`は試験用の識別値です。製品には製品用の識別値を設定し、試験用IDをそのまま流用しません。

- 名前・VID/PID・serial number 等は `CH32RVUsbDeviceConfig` で設定します。サンプルの識別情報を製品へそのまま流用せず、
  複数台の識別には serial number も使います。
- descriptor に反映される設定は `begin()` より前に済ませます。構成を変えたら PC 側の再認識も確認します。
- HID は押下と解放を、MIDI は Note On と Note Off を対で扱います。送信失敗時の解放・再試行も考えます。
- bulk と CDC はストリームです。改行や長さフィールド等でアプリのメッセージ境界を定義します。
- 音声・映像・連続データでは送信の戻り値を確認し、未送信分を保持するか、破棄を数えるかを決めます。
- MSC はホストの書き込みキャッシュを考慮し、マウント中の媒体をスケッチから同時に変更しません。

## 動かない場合

| 症状 | 確認すること |
|---|---|
| UART にログがない | 端子・速度・GND・リセット。USB CDC と UART を取り違えていないか |
| `begin()` が失敗 | `lastErrorName()` と [ライブラリ README](../README.ja.md)。型番・controller・クロック・他機能との競合 |
| PC に現れない | ケーブル、USB データコネクタ、D+/D−、controller の選択 |
| CDC に届かない | 正しいポートを DTR 有効で開いたか。送信先が UART ではないか |
| callback が動かない | 通知を登録した場合、`loop()` で `deliverEvents()` を呼んでいるか |
| 列挙するが操作できない | 対応する PC アプリ、report/PCM 等の形式、interface と endpoint の選択 |
| 高負荷時だけ欠落する | 処理の戻り値、長い callback、読み出し頻度、送受信量 |

再書き込みの入口と実際の更新方式は系列によって異なります。
[USB 書き込み手順](../../../docs/usb.ja.md#書き込み) と [書き込み経路](../../../docs/upload-routes.ja.md) を参照してください。
通常の CDC 試験では、意図せず更新へ移らないよう1200 bps touchにも注意します。

## サンプル一覧


| 例 | 内容 |
|---|---|
| [AudioHeadset](AudioHeadset/README.ja.md) | UAC1 Playback と Capture を同時に公開し、PC から再生された PCM を録音側へ返すループバックです。 |
| [AudioMicrophone](AudioMicrophone/README.ja.md) | 実マイクの代わりに440 Hzのテスト音を生成する UAC1 Capture です。 |
| [AudioSpeaker](AudioSpeaker/README.ja.md) | UAC1 Playback で受信した PCM を数えて UART に表示します。スピーカーや DAC への出力は実装していません。 |
| [CompositeHidCdcMsc](CompositeHidCdcMsc/README.ja.md) | キーボード、CDC、10 KiB FAT RAM ディスクを一つの USB デバイスにまとめます。CDC からキーボード入力を指示できます。 |
| [CustomDevice](CustomDevice/README.ja.md) | CDC、DFU runtime、Vendor HID を組み合わせ、製品名や VID/PID を設定する例です。Vendor HID の Output report は Input report として返します。 |
| [CustomHID](CustomHID/README.ja.md) | 独自の report descriptor を渡して、8-byte Input report だけを持つ HID を作ります。 |
| [DescriptorDump](DescriptorDump/README.ja.md) | ホストに見せている記述子（デバイス・構成・BOS・MS OS 2.0・文字列・HID）と、制御要求・バスのイベントを UART に表示します。 |
| [DfuRuntime](DfuRuntime/README.ja.md) | CDC を持たない DFU runtime インターフェースで、USB から ISP への移行を試します。PA1 が Low の間は `onDetach()` が移行を拒否します。 |
| [ButtonKeyboard](ButtonKeyboard/README.ja.md) | チャタリングを除いたスイッチでキーを押下・解放し、未送信の状態を再試行します。 |
| [ButtonMouse](ButtonMouse/README.ja.md) | 物理スイッチで左ボタンを押下・解放します。移動は自動生成しません。 |
| [AnalogGamepad](AnalogGamepad/README.ja.md) | 中央を校正した2軸とボタンを送り、中央付近には中立帯を設けます。 |
| [Gamepad](Gamepad/README.ja.md) | 6軸、ハット、ボタン bitmap を持つ HID ゲームパッドのテスト入力を送ります。 |
| [Keyboard](Keyboard/README.ja.md) | USB キーボードとして、文字列・JIS 記号・raw HID usage を送信し、Lock LED の Output report を受信します。 |
| [KeyboardMouse](KeyboardMouse/README.ja.md) | 一つの HID インターフェースにキーボードとマウスを合成する例です。Report ID で二つの機能を区別します。 |
| [KeyboardNKRO](KeyboardNKRO/README.ja.md) | NKRO の bitmap report で10キーと Left Shift を同時送信します。boot protocol では6キー形式に切り替わることも観察できます。 |
| [MIDI](MIDI/README.ja.md) | USB MIDI 1.0 の Note / Control Change を自動送信し、受信 packet を UART に表示します。 |
| [MIDIController](MIDIController/README.ja.md) | 可変抵抗で CC74、ボタンで Note 60 を送る USB MIDI コントローラです。 |
| [MIDIInterface](MIDIInterface/README.ja.md) | USB MIDI と 31250 bps UART の MIDI メッセージを双方向に変換する例です。 |
| [MSC](MSC/README.ja.md) | RAM を16×512 bytesの raw block device として公開します。SCSI やブロック転送の確認用です。 |
| [MSCFatRamDisk](MSCFatRamDisk/README.ja.md) | 10 KiB の RAM に FAT12 を作り、PC が書いた `CONFIG.TXT` を取り出す例です。 |
| [MSCSdCard](MSCSdCard/README.ja.md) | SPI の SD カードを USB MSC の媒体として PC へ公開します。ファイルシステムは PC が扱います。 |
| [MediaKeys](MediaKeys/README.ja.md) | 音量・再生・トラック操作の Consumer Control と、System Control の宣言例です。 |
| [Mouse](Mouse/README.ja.md) | 相対移動、ホイール、左ボタンを順に送る USB マウスです。物理スイッチやセンサは使いません。 |
| [SerialEvents](SerialEvents/README.ja.md) | USB の callback が `deliverEvents()` の中でだけ動くことを確かめます。`loop()` がふさがっている間に届いたデータは、待ってから 1 回で届きます。 |
| [Serial](Serial/README.ja.md) | 明示的に宣言した CDC ポートの送受信と、ホストによる line coding・DTR/RTS の変更を観察します。USB 通信とハードウェア UART ログを使い分ける例です。 |
| [SerialMulti](SerialMulti/README.ja.md) | 一つの USB デバイスに独立した二つの CDC ポートを作ります。Console と Data Link を分け、ログと機械向け通信を別の端末で確認できます。 |
| [SerialUSBEcho](SerialUSBEcho/README.ja.md) | 定義済みの `SerialUSB` だけで始める CDC ACM の最小例です。受信したバイトをそのまま返し、端末を開いている間は毎秒カウンタを送ります。 |
| [SmartCardReader](SmartCardReader/README.ja.md) | カード自体をスケッチで模擬する1スロットの CCID リーダーです。実カードや接触型カード回路は接続しません。 |
| [USBVendor](USBVendor/README.ja.md) | HID ではない vendor-specific interface に bulk IN/OUT と EP0 vendor request を用意します。WebUSB と WinUSB 用の descriptor も有効にします。 |
| [UsbNetworkPing](UsbNetworkPing/README.ja.md) | CDC-NCM の Ethernet フレームから ARP と IPv4 ping に応答します。IP スタックを持たず、Web サーバーやインターネット接続は提供しません。 |
| [VendorHID](VendorHID/README.ja.md) | vendor-defined HID の Input、Output、Feature report を観察します。bulk を使う USBVendor とは別の例です。 |
| [VideoCamera](VideoCamera/README.ja.md) | カメラセンサなしで YUY2 カラーバーと動く白いブロックを生成する UVC デバイスです。 |
