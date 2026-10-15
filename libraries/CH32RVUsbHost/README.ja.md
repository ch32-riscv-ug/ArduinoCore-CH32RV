# CH32RVUsbHost — チップの USB をホストとして使う

キーボード・マウスなどの HID、USB シリアル（CDC-ACM と FTDI / CP210x / CH34x / PL2303）、MIDI、マスストレージ、
ベンダーのバルクインターフェース、スマートカードリーダー（CCID）を、ハブ越しも含めて扱います。

API は同じ作者の ESP32 用ライブラリ EspUsbHost のものを、
`EspUsbHost` → `CH32RVUsbHost`、`ESP_USB_HOST_` → `CH32RV_USB_HOST_` と読み替えた形です。

```cpp
#include <CH32RVUsbHost.h>

CH32RVUsbHost usb;

void setup() {
  usb.onDeviceConnected([](const CH32RVUsbHostDeviceInfo &d) { usb.printDeviceInfo(d.address); });
  usb.onKeyboard([](const CH32RVUsbHostKeyboardEvent &e) { if (e.pressed && e.ascii) Serial.print((char)e.ascii); });
  usb.begin();
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
- タスクの設定はありません。TinyUSB は `loop()` の後と `delay()` の中で動き、待つ API はその間スタックを回します。
  コールバックは割り込みからではなくそこから呼ばれ、待つ API を呼んでもかまいません。
- アイソクロナス（オーディオ・ビデオ）と USB ネットワークはありません。帯域とバッファがこのチップには足りません。
- MSC はブロック単位だけです（`mscMount` / `EspUsbHostMscFS` に当たるものはありません）。
- ベンダーのバルクと CCID は、同時に開けるのはそれぞれ1つです。

## 中身

- TinyUSB のホストスタックを使います（[CH32RVTinyUSB](../CH32RVTinyUSB/)）。ホストコントローラのドライバ
  （`ch32rv_hcd_usbfs.c`）はこのコアで書いたもので、開いているエンドポイントごとに転送を持ち、1 本しかない
  トランザクションエンジンで順に流します。NAK は次のフレーム（インタラプトは bInterval ごと）で再試行し、STALL・
  無応答・トグル違いを区別します。制御転送は 5 秒（USB 仕様の上限）で打ち切ります。
- スケッチが使う機能のファイルだけがリンクされ、TinyUSB のクラスドライバもそれと一緒に入ります。
- デバイス（CH32RVUsbDevice）とは同じコントローラを使うので、同時にはどちらか一方です（もう一方が動いていれば `Busy`）。
