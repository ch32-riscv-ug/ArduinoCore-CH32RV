# CH32RVUsbDevice — チップの USB をデバイスとして使う

CDC シリアル（`SerialUSB`）、HID（キーボード・マウス・汎用）、MIDI、DFU ランタイムを、宣言するだけで組み合わせられる
USB デバイスのライブラリです。土台は同梱の TinyUSB（[CH32RVTinyUSB](../CH32RVTinyUSB/)）です。
使い方の全体は [docs/usb.ja.md](../../docs/usb.ja.md) にあります。

```cpp
#include <CH32RVUsbDevice.h>

CH32RVUsbDeviceKeyboard keyboard;        // 宣言すると USBDevice に付く
CH32RVUsbDeviceDfuRuntime dfu;           // USB だけで書き込み直す入口

void setup() {
  CH32RVUsbDeviceConfig config;
  config.product = "My gadget";
  if (!USBDevice.begin(config)) {        // 付いたクラスからディスクリプタを組み立てて接続
    // USBDevice.lastErrorName() が理由
  }
  SerialUSB.begin();                     // SerialUSB も付く（定義済み）
}
void loop() { if (SerialUSB) SerialUSB.println("hello"); delay(1000); }
```

## 考え方

- **デバイスは1つ、機能は宣言で付ける。** `USBDevice` と `SerialUSB` は `Serial` と同じく定義済みです。自分で
  `CH32RVUsbDevice usb;` を宣言してもかまいません（1つのスケッチにデバイスは1つ）。クラスのオブジェクトは、
  宣言した順にデバイスへ付きます。
- **使わないものはリンクされない。** 宣言しなかったクラスのコードと領域は入りません。`#include` しただけなら
  Flash も RAM も増えません。
- **ビルド時に決まるのは大きさだけ。** 同じクラスをいくつ持てるか、バッファの大きさは、スケッチの `build_opt.h` で
  変えられます（例: `-DCFG_TUD_CDC=2`）。
- **失敗は戻り値で分かる。** `begin()` が `false` のとき、`lastError()` / `lastErrorName()` が理由を返します。USB の
  無い系列でも、スケッチはそのままビルドできます（`Unsupported`）。

## クラス

| クラス | 内容 |
|---|---|
| `CH32RVUsbDevice` | デバイス本体。`begin(config)`、`mounted()`、`suspended()`、`remoteWakeup()`、`enterBootloader()` |
| `CH32RVUsbDeviceConfig` | VID/PID、製造者名・製品名・シリアル番号、電流、使うポート |
| `CH32RVUsbDeviceCdcSerial` | 仮想 COM ポート（`Stream`）。1200bps タッチ、`onLineState()`、`onLineCoding()` |
| `CH32RVUsbDeviceKeyboard` | ブートキーボード。`print()`、`press()` / `release()`、`leds()` |
| `CH32RVUsbDeviceMouse` | ブートマウス。`move()`、`click()` |
| `CH32RVUsbDeviceHidGeneric` | ベンダー定義の HID（1〜64 B のレポート）。`send()`、`onOutputReport()` |
| `CH32RVUsbDeviceHid` | 任意のレポートディスクリプタの HID |
| `CH32RVUsbDeviceMidi` | USB MIDI。`noteOn()`、`controlChange()`、`readPacket()` |
| `CH32RVUsbDeviceDfuRuntime` | DFU_DETACH で工場出荷の USB ブートローダーへ。Windows 向けに WinUSB の MS OS 2.0 ディスクリプタ付き |

## 例

| 例 | 内容 |
|---|---|
| SerialUSBEcho | `SerialUSB` だけの最小の形 |
| CustomDevice | 自分の名前・VID/PID、複数クラス、`begin()` の失敗の表示 |
| TwoSerialPorts | `build_opt.h` で CDC を2つにする |
| Keyboard / Mouse | ボタンを押したときだけ入力を送る |
| HidGenericEcho | ドライバ不要のデータ通信（hidapi / WebHID） |
| MidiController | MIDI と CDC の複合デバイス |
| DfuRuntime | CDC なしで、USB から書き込み直す入口だけを持つ |

## 対応

CH32X035 / X033（USBFS）。X035（WeAct CH32X035F8U6 の板）で、全部の例の列挙と動作、DFU_DETACH・1200bps タッチ
からの `ch32rv isp flash` を実機で確認しています。ほかの系列では `begin()` が `false` を返します。

## 書き込み

1200bps タッチ（CDC がある場合）か DFU_DETACH（`CH32RVUsbDeviceDfuRuntime` がある場合）で工場出荷の USB
ブートローダー（1a86:55e0）に入れ、`ch32rv isp flash` で書き込みます。アップロードが無ければ約 10 秒でスケッチに
戻ります。USB を使わないスケッチを書いた後は、Boot / DOWNLOAD キーかデバッグプローブで入れてください。
詳しくは [docs/usb.ja.md](../../docs/usb.ja.md#書き込み)。
