# USB Device サンプルの使い方

[English](README.md)

ボードを PC 等の USB ホストへ接続し、CDC、HID、MIDI、MSC、Vendor、CCID、音声、映像、NCM を試す例です。最初は [SerialUSBEcho](SerialUSBEcho/README.ja.md) で USB 配線と列挙を確認し、その後に目的のクラスを選びます。各フォルダの README はそのスケッチの設定・出力に合わせています。

## ボードと接続

実機確認の基準は WeAct CH32X035F8U6 の Device 接続です。今ドライバがあるのは X035 / X033 だけで、下の V103/V20x/V30x の行はドライバを足したときの接続です。詳細と型番例外は [USB の対応表](../../../docs/usb.ja.md#対応状況) を参照してください。H417 の SS は現在のサンプルの動作範囲に含みません。

| 系列・controller | D− | D+ | 選択 |
|---|---|---|---|
| X035 / X033 USBFS | PC16 | PC17 | `Auto` |
| V103 / V303 USBFS | PA11 | PA12 | `Auto` |
| V203 / V208 USBFS | PB6 | PB7 | `FullSpeed` / 通常の `Auto` |
| V203 / V208 USBD | PA11 | PA12 | `Usbd`。USBFS のない型番では `Auto` も USBD |
| V305 / V307 / V317 USBFS | PA11 | PA12 | `FullSpeed` / 通常の `Auto` |
| V305 / V307 / V317 USBHS | PB6 | PB7 | `HighSpeed`。HSE が必要 |

使う型番・パッケージに端子と controller があることを確認し、ボード回路図の USB データコネクタに接続します。USB-UART ブリッジだけにつながるコネクタへ挿しても Device は列挙されません。

多くの例は `Serial` のハードウェア UART へ115200 bps・8N1でログを出します。これは USB CDC の `SerialUSB` / `UsbSerial` とは別です。

| ボード | UART アダプタ |
|---|---|
| `TX` | RX |
| `RX` | TX（コマンド入力が必要な例） |
| GND | GND |

端子名 `RX` / `TX` / `SCK` 等の実 pin は選択した board/variant に依存します。UART は基板の I/O 電圧に合わせます。SerialUSBEcho と DfuRuntime は UART を使いません。

## ビルドと書き込み

Arduino IDE ではボードと正確な Part Number を選び、「ファイル → スケッチ例 → CH32RVUsbDevice」から例を開きます。初回は動作する書き込み経路を使い、USB Device が起動してからアプリの USB 入口を試します。

arduino-cli でリポジトリのルートからビルドする例です。対象 core が導入済みであることを前提にします。

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32rv:CH32X035:pnum=CH32X035F8U6 \
  libraries/CH32RVUsbDevice/examples/SerialUSBEcho
```

手元の開発中 core を検証する場合は、Arduino CLI が参照する platform がこの作業ツリーであることを確認します。`sketch.yaml` の profile は配布版を固定するビルド条件であり、実機検証済みボードの一覧ではありません。

- Audio の例には `flash=48K`、RAM disk 等には `ram=16K` の profile 下限があります。実際の容量は使用する Part Number とリンク結果で確認します。
- `pnum=ANY` のメモリ容量は系列内の最小値です。実ボードに大きな RAM があるのにリンクできないときは正確な型番を選びます。
- 生成済み `sketch.yaml` は直接編集せず、[examples のビルド規則](../../../docs/examples-build-rules.ja.md)に従います。

## 動作と設定の共通事項

`CH32RVUsbDevice device;` に各 function を宣言し、`device.begin(config)` を1回呼びます。既定の `USBDevice` に付ける形式もあります。HID の機能は一つの HID interface に合成されます。既定で endpoint を消費しない DFU runtime も付きます。

`begin()` 成功は USB stack の開始で、PC の列挙完了とは別です。`mounted()` で構成済み、CDC の `connected()` / `operator bool` で端末の DTR を見ます。CDC を開いていない状態で周期ログが届かないのは想定した動作です。

| 変更したいもの | 変更箇所 |
|---|---|
| 名前、VID/PID、serial number | `.ino` の `CH32RVUsbDeviceConfig`。`1209:0001` はテスト用 |
| 複数台の識別 | 固定 serial number を台ごとに変えるか、`nullptr` でチップ固有値を使う |
| controller | `config.controller`。HS 選択は USBHS の物理端子と HSE を確認 |
| クラス個数・バッファ | 個数はクラスごとの上限（CDC・MIDI は4、ベンダーとビデオは2、ほかは1）。CDC のバッファは `setBuffers()` で差し替え |
| キーボードの文字配列 | `setLayout()` と PC の配列。raw usage 送信とは区別 |
| 音声/映像の format | 各例の rate/channel/frame 設定。USB endpoint の帯域にも合わせる |

`loop()` の後と `delay()` 中に USB task が進みます。長い計算ループでは `device.task()` を定期的に呼びます。Device callback では長い待機を避け、必要な仕事を `loop()` へ渡します。callback 中の `delay()` で USB task が入れ子に進むことを期待しないでください。

## 認識しない場合

| 症状 | 最初に確認する箇所 |
|---|---|
| UART に何も出ない | UART の RX/TX、115200 bps、GND、リセット後のモニタ開始。USB CDC と UART の取り違え |
| `USB_BEGIN_FAILED Unsupported` | 現在のライブラリでその系列の Device が実装済みか |
| `PortUnavailable` | 型番の controller/端子、HS 用 HSE の有無と `CH32RV_HSE_HZ` |
| `TooManyInstances` | 同じクラスの数がクラスごとの上限を超えていないか |
| `OutOfEndpoints` / `DescriptorTooLarge` | 組み合わせた function 数と descriptor の上限 |
| `NotReady` / HID送信失敗 | PC に列挙されているか、suspend 中でないか |
| CDC に何も届かない | DTR を有効にしてポートを開いたか、ローカルエコー/改行設定 |
| エラーなしで USB が現れない | 充電専用ケーブル、USB-UART 側コネクタ、D+/D− 配線、選択 controller |

PC の device/interface/endpoint 情報を見るときは Linux の `lsusb -v` や Windows の USBView 等を使います。HID・MSC・音声等は COM ポートとして現れないクラスなので、該当クラスを扱うアプリから開きます。

## 再書き込み

X035/X033 は CDC の1200 bps touch または DFU_DETACH を入口に WCH ISP へ移行できます。通常の端末試験は1200 bpsを避けます。DFU runtime は firmware 本体を DFU_DNLOAD で受け取る実装ではありません。ほかの系列の入口は現在通常 reset で、同じ ISP 移行を保証しません。[USB 書き込み手順](../../../docs/usb.ja.md#書き込み)と[書き込み経路](../../../docs/upload-routes.ja.md)を参照してください。

## サンプル一覧

| 例 | 内容 |
|---|---|
| [AudioHeadset](AudioHeadset/README.ja.md) | UAC1 Playback と Capture を同時に公開し、PC から再生された PCM を録音側へ返すループバックです。 |
| [AudioMicrophone](AudioMicrophone/README.ja.md) | 実マイクの代わりに440 Hzのテスト音を生成する UAC1 Capture です。 |
| [AudioSpeaker](AudioSpeaker/README.ja.md) | UAC1 Playback で受信した PCM を数えて UART に表示します。スピーカーや DAC への出力は実装していません。 |
| [CompositeHidCdcMsc](CompositeHidCdcMsc/README.ja.md) | キーボード、CDC、10 KiB FAT RAM ディスクを一つの USB デバイスにまとめます。CDC からキーボード入力を指示できます。 |
| [CustomDevice](CustomDevice/README.ja.md) | CDC、DFU runtime、Vendor HID を組み合わせ、製品名や VID/PID を設定する例です。Vendor HID の Output report は Input report として返します。 |
| [CustomHID](CustomHID/README.ja.md) | 独自の report descriptor を渡して、8-byte Input report だけを持つ HID を作ります。 |
| [DfuRuntime](DfuRuntime/README.ja.md) | CDC を持たない DFU runtime インターフェースで、USB から ISP への移行を試します。PA1 が Low の間は `onDetach()` が移行を拒否します。 |
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
| [Serial](Serial/README.ja.md) | 明示的に宣言した CDC ポートの送受信と、ホストによる line coding・DTR/RTS の変更を観察します。USB 通信とハードウェア UART ログを使い分ける例です。 |
| [SerialMulti](SerialMulti/README.ja.md) | 一つの USB デバイスに独立した二つの CDC ポートを作ります。Console と Data Link を分け、ログと機械向け通信を別の端末で確認できます。 |
| [SerialUSBEcho](SerialUSBEcho/README.ja.md) | 定義済みの `SerialUSB` だけで始める CDC ACM の最小例です。受信したバイトをそのまま返し、端末を開いている間は毎秒カウンタを送ります。 |
| [SmartCardReader](SmartCardReader/README.ja.md) | カード自体をスケッチで模擬する1スロットの CCID リーダーです。実カードや接触型カード回路は接続しません。 |
| [USBVendor](USBVendor/README.ja.md) | HID ではない vendor-specific interface に bulk IN/OUT と EP0 vendor request を用意します。WebUSB と WinUSB 用の descriptor も有効にします。 |
| [UsbNetworkPing](UsbNetworkPing/README.ja.md) | CDC-NCM の Ethernet フレームから ARP と IPv4 ping に応答します。IP スタックを持たず、Web サーバーやインターネット接続は提供しません。 |
| [VendorHID](VendorHID/README.ja.md) | vendor-defined HID の Input、Output、Feature report を観察します。bulk を使う USBVendor とは別の例です。 |
| [VideoCamera](VideoCamera/README.ja.md) | カメラセンサなしで YUY2 カラーバーと動く白いブロックを生成する UVC デバイスです。 |
