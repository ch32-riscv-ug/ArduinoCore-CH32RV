# CH32RVUsbDevice — チップの USB をデバイスとして使う

CDC シリアル、HID（キーボード・マウス・ゲームパッド・メディアキー・システム制御・ベンダー・カスタム）、MIDI、
ベンダー（バルク。WebUSB と MS OS 2.0 ディスクリプタ付き）、MSC（RAM ディスク付き）、スマートカードリーダー（CCID）、
DFU ランタイム（書き込みの入口）を、宣言するだけで組み合わせられる USB デバイスのライブラリです。
API は [EspUsbDevice](https://github.com/tanakamasayuki/EspUsbDevice) に合わせてあり、名前の `Esp` を `CH32RV` に
置き換えれば、たいていのスケッチはそのまま動きます。土台は同梱の TinyUSB（[CH32RVTinyUSB](../CH32RVTinyUSB/)）です。
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
- **コールバックは関数ポインタ**です（EspUsbDevice は `std::function`）。キャプチャしないラムダはそのまま渡せます。
  キャプチャするラムダは渡せません。
- **キーボードレイアウトは、使ったものだけがリンクされます。** 全部（19種）を持つと約 17 KB になるため、
  レイアウトごとに別のオブジェクトにしています。
- **CDC は `Stream` です。** `print()` などの Arduino の関数がそのまま使え、ホストが開いている間は書き込みが
  全部終わるまで待ちます（EspUsbDevice は書けた分だけ返します）。1200bps タッチでブートローダーに入れます。
- **スタックは `loop()` と `delay()` から動きます**（RTOS のタスクではありません）。`device.task()` で明示的にも
  動かせます。
- **個数とバッファはビルド時に決まります。** CDC を2つ以上使うときは、スケッチの `build_opt.h` に
  `-DCFG_TUD_CDC=2` と書きます。EspUsbDevice の実行時の設定（`setRxBufferSize()` など）はありません。
- **ディスクは MSC クラスが読みに行くオブジェクト**です（`setStorage()`。`attach()` がこれを呼びます）。
  EspUsbDevice のように `onRead()` にラムダを結び付ける形ではありません。
- **無いもの**: オーディオ、ビデオ、USB ネットワーク、ファームウェア更新の各クラス、SD カードのディスク、ベンダーの
  直接転送（`writeDirect()` は `NotSupported`）（[docs/usb.ja.md](../../docs/usb.ja.md#今後の予定)）。
- **既定の文字列と ID はすべて `build_opt.h` の define で上書きできます**（VID/PID、メーカー名、製品名、
  インターフェース名、MSC の INQUIRY、FAT のボリュームラベル）。
- **DFU ランタイムが既定で付きます**（EspUsbDevice には無い動作）。USB を使うスケッチは、何も書かなくても USB から
  書き込み直せます。`config.dfuRuntime = false` で外せます。
- **このライブラリだけのもの**: `CH32RVUsbDeviceDfuRuntime`（ISP への入口）、`SerialUSB` / `USBDevice` の定義済み
  オブジェクト、`enterBootloader()`。

## クラス

| クラス | 内容 |
|---|---|
| `CH32RVUsbDevice` | デバイス本体。`begin(config)`、`end()`、`ready()` / `mounted()`、`task()`、`sendHidReport()`、`lastErrorName()`、`enterBootloader()` |
| `CH32RVUsbDeviceCdcSerial` | 仮想 COM ポート（`Stream`）。`connected()`、`lineCoding()`、`lineState()`、`onLineCoding()`、`onLineState()`、`onRx()`、1200bps タッチ |
| `CH32RVUsbDeviceHidKeyboard` | キーボード。`write()`、`tapKey()`、`tapUsage()`、`pressUsage()`、`releaseAll()`、`setLayout()`、`ledState()`、`enableNkro()` |
| `CH32RVUsbDeviceHidMouse` | マウス。`move()`、`wheel()`、`click()`、`press()` / `release()` |
| `CH32RVUsbDeviceHidGamepad` | ゲームパッド（6 軸、ハット、32 ボタン）。`send()`、`sendReport()` |
| `CH32RVUsbDeviceHidConsumerControl` | メディアキー。`click(CH32RV_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP)` など |
| `CH32RVUsbDeviceHidSystemControl` | 電源オフ・スタンバイ・ホストの起床 |
| `CH32RVUsbDeviceHidVendor` | ベンダー定義の HID（ドライバ不要のデータ通信）。`sendInput()`、`onOutputReport()`、`onFeatureReport()` |
| `CH32RVUsbDeviceHidCustom` | 自分のレポートディスクリプタの HID |
| `CH32RVUsbDeviceMidi` | USB MIDI（最大 16 ケーブル）。`noteOn(channel, note, velocity, cable)`、`readPacket()` |
| `CH32RVUsbDeviceVendor` | ベンダー定義のバルク転送（`Print`）。`read()`、`write()`、`flush()`、`waitWritable()`、`onRx()`、`onControlRequest()`。WebUSB と Windows の WinUSB はこのクラスがあると有効 |
| `CH32RVUsbDeviceMsc` | USB メモリ。`begin(blocks, size)`、`onRead()` / `onWrite()`、`mediaPresent()`、`isWritable()`、`vendorID()` など |
| `CH32RVUsbDeviceMscRamDisk` / `CH32RVUsbDeviceMscFatRamDisk` | RAM 上のブロック / FAT12 のボリューム。`attach(msc)`、`addTextFile()`、`readFile()`、`onEject()` |
| `CH32RVUsbDeviceCcid` | スマートカードリーダー。`insertCard(atr)`、`removeCard()`、`onApdu()`、`onEscape()`、`onPower()` |
| `CH32RVUsbDeviceDfuRuntime` | DFU_DETACH を受けて工場出荷の USB ISP ブートローダーに入る入口。DFU のランタイムインターフェースだけで、DFU モードやダウンロードはありません。**既定で付きます**（`config.dfuRuntime = false` で外す）。宣言するのは `onDetach()` を使うときだけ。インターフェース名 "CH32RV" が ch32rv の discovery の目印 |

HID のクラスは、いくつ宣言しても1つの HID インターフェースにまとまります（EspUsbDevice と同じ）。1つだけなら
そのまま（キーボードとマウスは BIOS でも使えるブートデバイス）、2つ以上ならレポート ID で区別します。

## 例

| 例 | 内容 |
|---|---|
| Serial、SerialMulti | CDC。SerialMulti は `build_opt.h` で CDC を2つにする |
| Keyboard、KeyboardNKRO、Mouse、KeyboardMouse、MediaKeys、Gamepad | HID。**起動すると自動で入力を送ります**（EspUsbDevice の例と同じ） |
| VendorHID、CustomHID | ベンダー HID / 自分のレポートディスクリプタ |
| MIDI、MIDIController、MIDIInterface | MIDI。MIDIInterface は UART の MIDI と USB MIDI の変換 |
| USBVendor | ベンダー（バルク）と WebUSB、ベンダーリクエスト |
| MSC、MSCFatRamDisk | RAM ディスク / FAT の RAM ディスク（X035 の RAM に合わせて 8〜10 KB に縮小） |
| CompositeHidCdcMsc | キーボード + CDC + MSC の複合デバイス |
| SmartCardReader | CCID のカードリーダー |
| SerialUSBEcho、CustomDevice、DfuRuntime | このライブラリ独自: `SerialUSB` だけの形、設定と失敗の表示、書き込みの入口だけ |

EspUsbDevice の同名の例を移植したもの（Serial〜SmartCardReader）は、名前の置き換えとピン・VID/PID の変更だけで、
中身は元のままです。

## 対応

CH32X035 / X033（USBFS）。X035（WeAct CH32X035F8U6 の板）で実機確認しています。ほかの系列では `begin()` が
`false`（`Unsupported`）を返し、スケッチはそのままビルドできます。

## 書き込み

1200bps タッチ（CDC がある場合）か DFU_DETACH（`CH32RVUsbDeviceDfuRuntime` がある場合）で工場出荷の USB
ISP ブートローダー（1a86:55e0）に入れ、`ch32rv isp flash --entry auto` で書き込みます。DFU は入口に使うだけで、
書き込みは WCH の ISP プロトコルです（dfu-util では書けません）。詳しくは [docs/usb.ja.md](../../docs/usb.ja.md#書き込み)。
