# CH32RVUsbDevice — チップの USB をデバイスとして使う

[English](README.md)

CDC シリアル、HID（キーボード・マウス・ゲームパッド・メディアキー・システム制御・ベンダー・カスタム）、MIDI、
ベンダー（バルク。WebUSB と MS OS 2.0 ディスクリプタ付き）、MSC（RAM ディスク・SD カード）、スマートカードリーダー（CCID）、
USB ネットワーク（CDC-NCM）、オーディオ（UAC1 / UAC2）、カメラ（UVC）、DFU ランタイム（書き込みの入口）を、宣言するだけで組み合わせられる USB デバイスのライブラリです。
API は [EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) と名前・操作をそろえています。
移植時には下記の API 差分と、CH32 の pin・RAM・endpoint・帯域の制約を確認します。
土台はこのライブラリ自身のデバイススタックで、TinyUSB は使いません（[ADR-0012](../../docs/adr/0012-usb-stack.ja.md)）。
使い方の全体は [docs/usb.ja.md](../../docs/usb.ja.md) にあります。

```cpp
#include <CH32RVUsbDevice.h>

CH32RVUsbDevice device;
CH32RVUsbDeviceHidKeyboard keyboard(device);

void setup() {
  keyboard.setLayout(CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP);
  CH32RVUsbDeviceConfig config;
  config.product = "My keyboard";
  if (!device.begin(config)) {
    // device.lastErrorName() が理由
  }
}
void loop() {
  keyboard.write("Hello @\n");
  delay(3000);
}
```

`USBDevice` と `SerialUSB` は `Serial` と同じく定義済みなので、`SerialUSB.begin()` だけでも使えます。

配線・ビルド・UART と USB CDC の区別・エラー対処は[サンプル共通ガイド](examples/README.ja.md)へ、
最小の動作確認は [SerialUSBEcho](examples/SerialUSBEcho/README.ja.md) へ進んでください。

## EspUsbDevice からの置き換え

| EspUsbDevice | CH32RVUsbDevice |
|---|---|
| `#include "EspUsbDevice.h"` | `#include <CH32RVUsbDevice.h>` |
| `EspUsbDevice`、`EspUsbDeviceHidKeyboard` など | `CH32RVUsbDevice`、`CH32RVUsbDeviceHidKeyboard` など |
| `ESP_USB_HID_KEY_A`、`ESP_USB_DEVICE_MOD_LEFT_SHIFT` など | `CH32RV_USB_HID_KEY_A`、`CH32RV_USB_DEVICE_MOD_LEFT_SHIFT` など |
| `ESP_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP`（列挙値） | `CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP`（表のオブジェクト。書き方は同じ） |
| `EspUsbDeviceConfig::controller` | `CH32RVUsbDeviceConfig::controller`（`CH32RVUsbController`） |
| `lastError()` が `esp_err_t` | `lastError()` が `CH32RVUsbDeviceError`。`lastErrorName()` は同じ |

### 違い
- **コールバックは `CH32RVCallback` です**（EspUsbDevice は `std::function`）。関数ポインタと、取り込みが
  ポインタ 2 つ分まで（`[this]`、ポインタと小さな値）のラムダを渡せます。ヒープは使いません。もっと大きい取り込みは
  コンパイルエラーになります。
- **キーボードレイアウトは、使ったものだけがリンクされます。** 全部（19種）を持つと約 17 KB になるため、
  レイアウトごとに別のオブジェクトにしています。
- **CDC は `Stream` です。** `print()` などの Arduino の関数がそのまま使え、ホストが開いている間は書き込みが
  全部終わるまで待ちます（EspUsbDevice は書けた分だけ返します）。1200bps タッチでブートローダーに入れます。
- **スタックは `loop()` と `delay()` から動きます**（RTOS のタスクではありません）。`device.task()` で明示的にも
  動かせます。
- **通知の callback は `deliverEvents()` から呼ばれます**（`onRx()`、`onLineState()`、`onBusEvent()`、`onFrame()`
  など）。スケッチが `loop()` で呼び、`delay()` の中では呼ばれません。それまでデータはクラスの buffer かホスト側で
  待ちます。ホストが答えを待つ provider（MSC の `onRead()` / `onWrite()`、`onApdu()`、`onControlRequest()`、
  `onFillPayload()`）はスタックの中から呼ばれます。
- **同じクラスの数には上限があります**（CDC・MIDI は4、ベンダーとビデオは2、ほかは1）。上限の中なら
  宣言するだけで使えます。CDC と CCID のバッファは `setBuffers()` でスケッチのものに差し替えられます
  （EspUsbDevice には無い拡張です）。クラスの総数や記述子の大きさには、ビルド時の上限がありません。
- **ディスクは MSC クラスが読みに行くオブジェクト**です（`setStorage()`。`attach()` がこれを呼びます）。
  EspUsbDevice のように `onRead()` にラムダを結び付ける形ではありません。
- **USB ネットワークの IP 層**（`ipConfig()`、`dhcpServer()`、`dhcpClient()`、`dhcpAdvertiseGateway()`、`dhcpDns()`、
  `defaultRoute()`、`beginNetwork()`、`networkUp()`、`localIP()`）は、Network ライブラリが持っています。使うスケッチは
  `#include <Network.h>` も書きます。Network ライブラリは Ethernet の MAC を持つ系列（V208、V307 / V317、V407 / V467、
  H41x）向けで、X035 では使えません。`beginNetwork()` を呼ばなければ、生の Ethernet フレームを扱うだけです。
  USB controller とネットワークの実機確認範囲は [USB の対応表](../../docs/usb.ja.md#対応状況) を参照してください。
- **無いもの**: ファームウェア更新の各クラス（[docs/usb.ja.md](../../docs/usb.ja.md#今後の予定)）。
- **ベンダーの直接転送（`writeDirect()`）は全系列で使えます。** 呼び出し側のバッファから、そのまま 1 回の転送で
  送ります（`onTxComplete()` で返るまで、バッファは触らず残してください）。EspUsbDevice と違い、ビルド時の設定は
  要らず、整列の決まりもありません（USBSS は 4 バイト境界なら DMA で直接読みます）。`onRxData()` も実行時に選べ、
  `setRxDataBuffer()` で受信先を大きなバッファにできます。`setInterfaceProtocol()` で subclass / protocol を、
  HID ベンダーの `setUsage()` で usage page / usage を変えられます（どちらもこのライブラリだけのもの）。
- **既定の文字列と ID は実行時に変えます**（`CH32RVUsbDeviceConfig` の VID/PID・メーカー名・製品名、`setName()`
  のインターフェース名、MSC の `vendorID()` など、FAT の `format()` のボリュームラベル）。`build_opt.h` の define では変えません。
- **DFU ランタイムが既定で付きます。** `config.dfuRuntime = false` で外せます。
  移行先と更新方式は系列に依存します。工場 ISP、自己書き換えによる DFU mode、通常 reset の違いは
  [USB の書き込み手順](../../docs/usb.ja.md#書き込み) を参照してください。
- **記述子を取り出す API は同じ名前です**（`deviceDescriptor()`、`configurationDescriptor()`、`stringDescriptor()`、
  `bosDescriptor()`、`microsoftOs20Descriptor()`、`hidReportDescriptor()` など）。このライブラリは記述子を常駐させず、
  呼ばれたときに 1 つの共有のヒープのバッファへ組み立てます。configuration・BOS・MS OS 2.0・HID report 等のポインタは、別の種類も含め、次に記述子を組み立てる API を呼ぶまで有効です。
  `onAnyControlRequest()` も同じ動きです（ベンダー要求は SETUP で、すべての制御転送は ACK で知らせる）。
- **このライブラリだけのもの**: `CH32RVUsbDeviceDfuRuntime`（ISP への入口）、`SerialUSB` / `USBDevice` の定義済み
  オブジェクト、`enterBootloader()`、`onBusEvent()`（リセット・構成・構成の解除・サスペンド・レジューム）、
  `configurationDescriptorFor(CH32RVUsbSpeed)`。

## クラス

| クラス | 内容 |
|---|---|
| `CH32RVUsbDevice` | デバイス本体。`begin(config)`、`end()`、`started()` / `ready()`、`remoteWakeupEnabled()` / `remoteWakeup()`（`config.remoteWakeupSupported` で宣言）、`task()`、`sendHidReport()`、`lastErrorName()`、`enterBootloader()` |
| `CH32RVUsbDeviceCdcSerial` | 仮想 COM ポート（`Stream`）。`connected()`、`lineCoding()`、`lineState()`、`onLineCoding()`、`onLineState()`、`onRx()`、1200bps タッチ |
| `CH32RVUsbDeviceHidKeyboard` | キーボード。`write()`、`tapKey()`、`tapUsage()`、`pressUsage()`、`releaseAll()`、`setLayout()`、`ledState()`、`enableNkro()` |
| `CH32RVUsbDeviceHidMouse` | マウス。`move()`、`wheel()`、`click()`、`press()` / `release()` |
| `CH32RVUsbDeviceHidGamepad` | ゲームパッド（6 軸、ハット、32 ボタン）。`send()`、`sendReport()` |
| `CH32RVUsbDeviceHidConsumerControl` | メディアキー。`click(CH32RV_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP)` など |
| `CH32RVUsbDeviceHidSystemControl` | 電源オフ・スタンバイ・ホストの起床 |
| `CH32RVUsbDeviceHidVendor` | ベンダー定義の HID（ドライバ不要のデータ通信）。`sendInput()`、`onOutputReport()`、`onFeatureReport()`、`setUsage()`。レポートは FS だけの系列で 63 バイト、HS / SS の系列で 511 / 1023 バイトまで |
| `CH32RVUsbDeviceHidCustom` | 自分のレポートディスクリプタの HID |
| `CH32RVUsbDeviceMidi` | USB MIDI（最大 16 ケーブル）。`noteOn(channel, note, velocity, cable)`、`readPacket()` |
| `CH32RVUsbDeviceVendor` | ベンダー定義のバルク転送（`Print`）。`read()`、`peek()`（このコアの独自）、`write()`、`flush()`、`waitWritable()`、`onRx()`、`onControlRequest()`、`writeDirect()`、`onTxComplete()`、`onRxData()`、`setRxDataBuffer()`、`setInterfaceProtocol()`。WebUSB と Windows の WinUSB はこのクラスがあると有効 |
| `CH32RVUsbDeviceMsc` | USB メモリ。`begin(blocks, size)`、`onRead()` / `onWrite()`、`mediaPresent()`、`isWritable()`、`vendorID()` など |
| `CH32RVUsbDeviceMscRamDisk` / `CH32RVUsbDeviceMscFatRamDisk` | RAM 上のブロック / FAT12 のボリューム。`attach(msc)`、`addTextFile()`、`readFile()`、`onEject()` |
| `CH32RVUsbDeviceCcid` | スマートカードリーダー。`insertCard(atr)`、`removeCard()`、`onApdu()`、`onEscape()`、`onPower()` |
| `CH32RVUsbDeviceMscSdCard` | SPI の SD カードを MSC の中身にする。`begin(cs, SPI)`、`attach(msc)`（`#include <CH32RVUsbDeviceMscSdCard.h>`） |
| `CH32RVUsbDeviceNet` | USB ネットワーク（CDC-NCM）。`onFrame()`、`sendFrame()`、`macAddress()`、`linkUp()`、受信の流量制御の `receiveGate()` / `resumeReceive()`。IP 層は上記 |
| `CH32RVUsbAudioFunction` | オーディオ（UAC1 / UAC2）。`addPlaybackStream()` / `addCaptureStream()`、`read()` / `write()`、ミュート・音量、`pollEvent()`。録音側の `availableForWrite()` はこのライブラリの追加 |
| `CH32RVUsbDeviceVideo` | カメラ（UVC）。`setFormat()`、`setFrameSize()`、`sendFrame()`、`onCommit()`。`onFillPayload()` / `sendGeneratedFrame()`（フレームを持たずに作る）はこのライブラリの追加 |
| `CH32RVUsbDeviceDfuRuntime` | DFU_DETACH による更新への入口。移行先は系列依存。**既定で付きます**（`config.dfuRuntime = false` で外す）。明示宣言は `onDetach()` で移行を判定するとき等に使用。インターフェース名 "CH32RV" は ch32rv の discovery の目印 |

HID のクラスは、いくつ宣言しても1つの HID インターフェースにまとまります（EspUsbDevice と同じ）。1つだけなら
そのまま（キーボードとマウスは BIOS でも使えるブートデバイス）、2つ以上ならレポート ID で区別します。

## 例

| 例 | 内容 |
|---|---|
| Serial、SerialMulti | CDC。SerialMulti は CDC を2つ持つ |
| Keyboard、KeyboardNKRO、Mouse、KeyboardMouse、MediaKeys、Gamepad | HID。**起動すると自動で入力を送ります**（EspUsbDevice の例と同じ） |
| VendorHID、CustomHID | ベンダー HID / 自分のレポートディスクリプタ |
| MIDI、MIDIController、MIDIInterface | MIDI。MIDIInterface は UART の MIDI と USB MIDI の変換 |
| USBVendor | ベンダー（バルク）と WebUSB、ベンダーリクエスト |
| MSC、MSCFatRamDisk | RAM ディスク / FAT の RAM ディスク（X035 の RAM に合わせて 8〜10 KB に縮小） |
| CompositeHidCdcMsc | キーボード + CDC + MSC の複合デバイス |
| SmartCardReader | CCID のカードリーダー |
| MSCSdCard | SD カードを USB メモリにする |
| AudioSpeaker、AudioMicrophone、AudioHeadset | オーディオ（X035 の 64 B/ms に合わせて 16〜32 kHz） |
| VideoCamera | 80x60 のカラーバーを流すカメラ |
| UsbNetworkPing | USB ネットワークで ARP と ping に答える（このライブラリ独自） |
| SerialUSBEcho、CustomDevice、DfuRuntime | このライブラリ独自: `SerialUSB` だけの形、設定と失敗の表示、書き込みの入口だけ |

各例の[個別 README と一覧](examples/README.ja.md)には、必要な機器、操作、期待する出力、変更する定数と制約を記載しています。
EspUsbDevice からの移植例も、CH32 の RAM に合わせた disk、音声の sample rate、frame buffer を持たない映像等の差があります。

## クラスを組み合わせるとき

同じ `device` を各 function の constructor に渡し、設定を済ませてから `device.begin(config)` を呼びます。
追加する順序は interface/endpoint の割当てにも影響します。default DFU は endpoint を使いません。
HID の複数機能は report ID で一つの interface を共有します。

| 構成 | endpoint 番号の消費例 |
|---|---|
| キーボードのみ | HID に1 |
| CDC のみ | 通知用1、データ IN/OUT用1の計2 |
| CDC二つ | 計4 |
| Keyboard + CDC + MSC | 1 + 2 + 1の計4 |

IN/OUT の endpoint 数、packet memory、ISO 能力は controller ごとに異なります。ESP32 の「IN endpoint が四つ」等の
制約をそのまま使わず、[USB controller の対応表](../../docs/usb.ja.md#コントローラと速度)を確認します。
`OutOfEndpoints` は割当て不足、`TooManyInstances` は class 個数不足で、対処する設定も異なります。

## 各クラスの確認方法

| 機能 | ホスト側で確認するもの |
|---|---|
| CDC | 端末を DTR 有効で開く。baud は USB バス速度とは別 |
| HID | 入力先アプリまたは raw report viewer。report ID と payload を区別 |
| MIDI | MIDI モニタや音源。channel は0始まり |
| MSC | raw blockなら [MSC](examples/MSC/README.ja.md)、ファイルなら [MSCFatRamDisk](examples/MSCFatRamDisk/README.ja.md) |
| Vendor bulk | libusb/WinUSB/WebUSB のアプリ。COM端末では開かない |
| CCID | PC/SC の ATR/APDU。callback の応答に SW1/SW2 も含める |
| Audio | 再生/録音アプリ。PCM format の一致と FIFO の読み書き |
| Video | 対応する解像度・YUY2と転送方式を選べる UVC アプリ |
| NCM | NIC と静的IP。IP 処理はスケッチ側（生フレーム）か、Network ライブラリ（`beginNetwork()`） |

callback の受信 buffer は後の処理まで所有し続けるものではありません。
保持が必要ならコピーし、Device callback の長い待ちや再入に頼らないでください。
`loop()` と `delay()` が USB 処理を進め、通知の callback は `deliverEvents()` で届きます。音声や映像で `write()` 等が返した長さを無視するとデータを失う場合があります。

### サンプル固有の制約

- HID の自動入力例は接続後に文字・クリック・音量操作を送ります。テスト用の入力先を用意します。
- [MIDIController](examples/MIDIController/README.ja.md) の変換式は12-bit入力、このコアの既定 ADC は10-bitです。全 CC 範囲を使う設定方法を個別 README に記載しています。
- AudioSpeaker は受信数の表示のみ、AudioMicrophone は合成音、AudioHeadset は USB loopbackです。実 codec の駆動例ではありません。
- VideoCamera は80×60のテストパターンを payload ごとに生成します。X035でISOを選ぶ場合は frame rateも調整します。
- UsbNetworkPing は小さな通常の ARP/ICMP の実験用で、DHCP/Web/汎用 IP parser はありません。

## 対応

controller の実装・型番ごとの例外・端子・クロック・実機確認の範囲は
[USB の対応表](../../docs/usb.ja.md#対応状況) を参照してください。
ビルドできることと、その構成で実機動作を確認していることは別です。

## 書き込み

CDC の1200 bps touch、または DFU_DETACH を更新への入口に使います。
移行先と書き込み方法は系列によって異なるため、[USB の書き込み手順](../../docs/usb.ja.md#書き込み) を確認してください。
`onDetach()` は移行の可否をその場で返す処理で、通知用の `deliverEvents()` を待ちません。
