# CH32RVUsbHost — チップの USB をホストとして使う

[English](README.md)

キーボード・マウスなどの HID、USB シリアル（CDC-ACM と FTDI / CP210x / CH34x / PL2303）、MIDI、マスストレージ、
ベンダーのバルクインターフェース、スマートカードリーダー（CCID）を、ハブ越しも含めて扱います。

API は同じ作者の ESP32 用ライブラリ EspUsbHost のものを、
`EspUsbHost` → `CH32RVUsbHost`、`ESP_USB_HOST_` → `CH32RV_USB_HOST_` と読み替えた形です。

```cpp
#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;

void setup() {
  Serial.begin(115200);
  usb.onDeviceConnected([](const CH32RVUsbHostDeviceInfo &d) { usb.printDeviceInfo(d.address); });
  usb.onKeyboard([](const CH32RVUsbHostKeyboardEvent &e) { if (e.pressed && e.ascii) Serial.print((char)e.ascii); });
  if (!usb.begin()) {
    Serial.println(usb.lastErrorName());
  }
}
void loop() { }          // スタックは loop() の後と delay() の中で動く
```

## 対応

| 系列 | ポート | 確認 |
|---|---|---|
| CH32X035 R8T6 / C8T6 / G8U6 / G8R6 | USBFS（PC16 / PC17） | C8T6 で ESP32-S3（EspUsbDevice）相手に HID・CDC・MSC・MIDI・ベンダー・CCID を実機確認 |
| CH32X035 F8U6 / F7P6 / D8U6、CH32X033 | — （シリコンにホストが無い。`begin()` が `Unsupported`） | F8U6 で `Unsupported` を実機確認 |
| CH32V203 / V208 | USBFS（PB6 / PB7） | ビルドのみ |
| CH32V303 / V305 / V307 / V317 | USBFS（PA11 / PA12） | ビルドのみ |
| ほかの系列 | — （`begin()` が `Unsupported`） | ビルドのみ |

X035 のどの型番がホストを持つかはデータシートの型番表のとおりで、`begin()` は BASE_CTRL のホストモードのビットが
立つかどうかで実際に確かめます（F8U6 では立ちません）。対向試験は [tests/peer/usb/host](../../tests/peer/usb/host/)
にあり、CH32X035C8T6 と ESP32-S3 で両方（usb-peer-host / usb-peer-host-classes）が通っています。

**VBUS は基板が供給します。** チップには VBUS を入り切りする機能がありません。

必要な型番、D+/D−、VBUS、UART の接続とビルド手順は
[サンプル共通ガイド](examples/README.ja.md)を参照してください。
最初の接続確認には [DeviceInfo](examples/DeviceInfo/README.ja.md) を使います。

## できること

- **機器**: 接続 / 切断のコールバック、ディスクリプタ、文字列、インターフェース、エンドポイント、制御転送
  （`vendorControlTransfer` / `vendorControlIn` / `vendorControlOut`）、ハブ（1 段、ポートの状態と電源）。
- **HID**: キーボード（配列 16 種、Caps / Num / Scroll Lock の LED）、マウス（レポートディスクリプタからの配置）、
  コンシューマ / システムコントロール、ゲームパッド、ベンダーのレポート、生の入力、レポートディスクリプタ、
  出力 / フィーチャーレポート、ブート / レポートプロトコル。
- **CDC シリアル**: `CH32RVUsbHostCdcSerial`（Stream）または `sendSerial` / `onSerialData`。ボーレート、データ形式、DTR / RTS。
- **MIDI**: 受信のコールバック、ノート・CC・プログラム・プレッシャー・ピッチベンド・SysEx の送信。
- **マスストレージ**: INQUIRY、容量、REQUEST SENSE、TEST UNIT READY、ブロックの読み書き、LUN の選択。
  ファイルシステムは持ちません（ブロック単位）。
- **ベンダー（バルク）**: `vendorOpen` で開き、`vendorWrite` / `vendorRead` / `onVendorData`。
- **CCID**: 電源、ATR、APDU（応答の SW は `statusWord` に分けて返す。EspUsbHost と同じ）、エスケープ、スロットの状態、
  カードの抜き差し。

## EspUsbHost との違い

- コールバックは関数ポインタです（`std::function` もリスナーの複数登録もありません）。RAM が 20 KB のため。
- タスクの設定はありません。スタックは `loop()` の後と `delay()` の中で動き、待つ API はその間スタックを回します。
  コールバックは割り込みからではなくそこから呼ばれ、待つ API を呼んでもかまいません。
- アイソクロナス（オーディオ・ビデオ）と USB ネットワークはありません。帯域とバッファがこのチップには足りません。
- MSC はブロック単位だけです（`mscMount` / `EspUsbHostMscFS` に当たるものはありません）。
- ベンダーのバルクと CCID は、同時に開けるのはそれぞれ1つです。
- USB シリアルの runtime `setRxBufferSize()` はありません。容量は `build_opt.h` の define で変えます。
- エラーは `esp_err_t` ではなく `CH32RVUsbHostError` です。ESP32 の task / PHY / channel 設定を持ち込みません。

API の語彙は共有しますが、現行 EspUsbHost のすべての機能との互換を意味しません。
このリポジトリの [公開ヘッダ](src/CH32RVUsbHost.h) が利用できる API の正本です。

## ライフサイクルと callback

| API | 意味 |
|---|---|
| `begin()` / `end()` | controller と Host stack の開始/停止。戻り値と `lastErrorName()` を確認 |
| `ready()` | Host が開始している。周辺機器の列挙完了を意味しない |
| `task()` | Host の処理を明示的に進める。長い計算ループで使用 |
| `onDeviceConnected()` / `onDeviceDisconnected()` | 機器の接続/切断を通知 |
| `deviceCount()` / `getDevices()` / `getDevice()` | 接続機器と address を取得 |

callback はそれぞれ1枠の関数ポインタです。再登録すると置き換わります。キャプチャしないラムダや通常関数を使い、
必要な状態は静的変数等へ保持します。イベント中の `data` / `rawData` や文字列へのポインタを後で使う場合は、
自前の buffer へコピーしてください。

`CH32RV_USB_HOST_ANY_ADDRESS` と `CH32RV_USB_HOST_ANY_PORT` は最初の該当機器/ポートを選ぶ指定です。
USB address は抜き挿しで変わり得ます。複数機器を扱うときは接続 callback の address で選び、切断時に状態を破棄します。
CDC の port は同一デバイス内の0始まりの番号です。

## ディスクリプタと制御転送

| API | 用途 |
|---|---|
| `printDeviceInfo(address)` / `printAllDeviceInfo()` | identity、構成、interface、endpoint を `Print` へ出す（既定 `Serial`） |
| `getInterfaces()` / `getEndpoints()` | descriptor から interface / endpoint を列挙 |
| `getStringDescriptor(address, index, out, capacity)` | 文字列を取得。非 ASCII は `?` に変換 |
| `vendorControlTransfer(requestType, request, value, index, ...)` | request type と recipient を含めて EP0 転送 |
| `vendorControlIn()` / `vendorControlOut()` | vendor device request の簡易形 |
| `getHubInfo()` / `getHubPortStatus()` / `setHubPortPower()` | hub の情報、port status/change、port power |

独自の interface request では descriptor の interface 番号を `wIndex` に入れ、正しい recipient と方向を指定します。
USB address、interface 番号、endpoint address、CDC port番号は別の値です。`0x81` のような endpoint address は
向きを含みます。PC側アプリや別実装の例の固定値をそのまま流用せず、実際の descriptor を確認します。

hub の power API は hub の port power request であり、ボード自身の VBUS スイッチを制御する API ではありません。
hub に個別電源制御の実装がない場合もあります。

## クラス別 API

### HID

| API | 用途 |
|---|---|
| `onKeyboard()` / `setKeyboardLayout()` / `setKeyboardLeds()` | キー入力、文字配列、Lock LED |
| `onMouse()` / `onGamepad()` | descriptor を解析した入力 |
| `onConsumerControl()` / `onSystemControl()` | メディア・システム usage |
| `onHIDInput()` / `onHIDVendorInput()` / `onHIDReportDescriptor()` | 生入力、vendor入力、report descriptor |
| `sendHIDReport()` / `getHIDReport()` | interface・type・report ID を指定する report 転送 |
| `sendHIDVendorOutput()` / `sendHIDVendorFeature()` | vendor report の送信 |
| `sendSetProtocol()` | boot/report protocol の切替 |

キーボードの `ascii=0` は、そのキーが ASCII 文字にならない場合にも使います。raw usage と pressed/released を
使えば文字以外のキーも処理できます。ゲームパッドの独自配置は、生の report と descriptor で確認します。

### USB シリアル

`CH32RVUsbHostCdcSerial port(usb)` は Arduino `Stream` です。`begin(baud)`、`available()`、`read()`、`peek()`、
`write()`、`flush()` を使え、`setBaudRate()` / `setConfig()` / `setDtr()` / `setRts()` で設定します。
接続前の `begin()` は設定を保持し、機器が現れたときに適用します。複数ポートは `setAddress()` / `setPort()` で選びます。

callback 形式なら `onSerialData()` / `sendSerial()`、状態は `serialReady()` / `serialPortCount()` /
`getSerialPortInfo()` で確認します。USB の baud 設定は対向 UART の line coding で、FS バスの速度とは別です。
大量転送では `write()` の戻り値を確認して再送位置を進めます。

受信リングが埋まると、この実装は次のパケットを受け取りません（機器は NAK で待ちます）。Esp 側の「古いデータを捨てる」説明を
そのまま適用しないでください。最終的な損失の有無は対向機器・UART・読み出し頻度にも依存します。

### MIDI

`onMidiMessage()` で cable、status、data1/data2 を受けます。`midiSendNoteOn/Off()`、`midiSendControlChange()`、
program/pressure/pitch bend/SysEx の helper を使えます。channel は0始まりです。詳細な引数は
[公開ヘッダ](src/CH32RVUsbHost.h)と [MIDI サンプル](examples/MIDI/README.ja.md)を参照してください。

### MSC

`mscWaitReady()` → `mscInquiry()` / `mscCapacity()` → `mscReadBlocks()` / `mscWriteBlocks()` の順で扱います。
必要な byte 数は block count × block size です。媒体が512-byte sectorとは限らないので容量情報を確認します。
`mscRequestSense()` で SCSI の状態、`mscSelectLun()` で対象 LUN を選べます。
filesystem、ファイル名、mount は扱いません。書き込みを追加する場合は、partition/FAT等を壊さないよう媒体の所有者を明確にします。

### Vendor bulk と CCID

`vendorOpen()` で class `0xff` の interface を開き、`vendorWrite()`、`vendorRead()` / `onVendorData()` で通信します。
`getVendorInterface()` で実際の endpoint を確認できます。bulk の byte列にアプリケーションのメッセージ境界はありません。

CCID は `ccidOpen()`、`ccidPowerOn()`、`ccidApdu()`、`ccidEscape()`、カード状態通知を用意します。
`ccidApdu()` の response は SW1/SW2 を除いた payload、status word は別の出力です。ただし受信 buffer の capacity は
SW の2 bytesも収まるよう確保します。通信成功と `SW=9000` の命令成功を分けて扱います。

## 容量の設定と失敗の確認

持てる数は決まっています: デバイス 5（ハブとその先）、ハブ 2、HID インタフェース 4、USB シリアル 2 ポート、
MSC 1、MIDI 1。構成記述子は 256 バイトまで読みます（その先のインタフェースは割り当てません）。

受信バッファの大きさだけは、スケッチと同じディレクトリの `build_opt.h` で変えられます。

| define | 既定 | 対象 |
|---|---|---|
| `CH32RV_USB_HOST_CDC_RX_BUFFER_SIZE` | 128 | USB シリアル（Stream）の受信リング |
| `CH32RV_USB_HOST_VENDOR_RX_BUFFER_SIZE` | 256 | ベンダーの受信リング |
| `CH32RV_USB_HOST_CCID_BUFFER_SIZE` | 272 | CCID のメッセージ |
| `CH32RV_USB_HOST_HID_REPORTS` | 8 | HID インタフェースごとに覚えるレポート ID |
| `CH32RV_USB_HOST_STRING_MAX` | 32 | 文字列記述子1つ（終端込み） |

容量を増やすと使用 RAM も増えます。

| エラー | 意味 |
|---|---|
| `Unsupported` | Host 未実装、またはシリコンに Host がない |
| `Busy` | controller を Device が使用中 |
| `NotStarted` / `NotConnected` | 開始前 / 対象機器なし |
| `NotSupported` | 必要な機能がない、または interface が未 open |
| `Timeout` / `Stalled` / `TransferFailed` | 時間切れ / 相手の拒否 / 転送失敗 |
| `InvalidArgument` / `InvalidResponse` | 引数 / 応答が不正 |
| `NoMemory` | buffer / 管理表の容量不足 |

## サンプル

配線・実行・出力・制約は[サンプル一覧](examples/README.ja.md)から個別の README を開いてください。

| 例 | 確認すること |
|---|---|
| [DeviceInfo](examples/DeviceInfo/README.ja.md) | 列挙、interface/endpoint、抜き挿し |
| [Keyboard](examples/Keyboard/README.ja.md) / [Mouse](examples/Mouse/README.ja.md) | HID 入力と LED |
| [USBSerial](examples/USBSerial/README.ja.md) | UART と USB シリアルの中継 |
| [MIDI](examples/MIDI/README.ja.md) | MIDI 入力と周期 Note |
| [MSCBlockDump](examples/MSCBlockDump/README.ja.md) | 媒体の容量と LBA 0の読み出し |
| [VendorBulk](examples/VendorBulk/README.ja.md) | 既知の bulk protocol の送受信 |
| [CcidReader](examples/CcidReader/README.ja.md) | ATR と APDU status word |

## 中身

- ホストスタックはこのライブラリ自身のものです（`CH32RVUsbHost.cpp`。TinyUSB は使いません）。割り込みは転送の
  完了とポートの変化を記録するだけで、制御転送の段階・列挙（ルートポートとハブ）・クラスの処理は通常の文脈で
  動きます。ホストコントローラのドライバ（`CH32RVUsbHcdUsbfs.cpp`、`CH32RVUsbHcd.h` の表）は、開いているエンドポイントごとに転送を持ち、1 本しかない
  トランザクションエンジンで順に流します。NAK は次のフレーム（インタラプトは bInterval ごと）で再試行し、STALL・
  無応答・トグル違いを区別します。制御転送は 5 秒（USB 仕様の上限）で打ち切ります。
- スケッチが使う機能のファイルだけがリンクされます（ハブはいつも入ります）。
- デバイス（CH32RVUsbDevice）とは同じコントローラを使うので、同時にはどちらか一方です（もう一方が動いていれば `Busy`）。
