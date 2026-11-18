# Arduino_CAN

[English](README.md)

CAN 2.0B を、ArduinoCore-API の `HardwareCAN` の形で使います。Arduino の公式コア（UNO R4 など）と同じく、
`#include <Arduino_CAN.h>` で `CAN`（CAN が 2 つある系列では `CAN1` も）が使えます。

```cpp
#include <Arduino_CAN.h>

void setup() {
  CAN.begin(CanBitRate::BR_500k);           // 既定の端子（variant の CH32RV_CAN1_RX / _TX）
}

void loop() {
  uint8_t data[] = {1, 2, 3};
  CAN.write(CanMsg(CanStandardId(0x123), sizeof data, data));
  while (CAN.available()) {
    CanMsg m = CAN.read();                   // m.id、m.data_length、m.data
  }
  delay(100);
}
```

## 対象

V20x・V205・V30x・V4x7・L103 / M103・H41x の bxCAN（端子の経路は device-data から生成）。CAN の無い系列
（V00x・X035 など）では、ヘッダーは何も定義しません。

## 使い方の注意

- バスには、RX / TX にトランシーバ（TJA1050、SN65HVD230 など）が要ります。
- すべてのフレームを受け取ります（全部を通すフィルタ 1 つ）。標準 ID と拡張 ID の両方を扱います（`CanStandardId()`、
  `CanExtendedId()`）。
- `write()` は、送信のメールボックス 3 つが埋まっていると 0、`begin()` の前は -1 を返します。
- 受信したフレームは、ハードの FIFO（3 通）と RAM（8 通）で待ちます。割り込みは使わないので、`available()` か `read()`
  を時々呼んで FIFO から移してください（3 通を超えて溜まると失われます）。
- `begin(bitrate)` は、CAN のクロック（APB1）が 8〜25 の時間量子にちょうど割れるビットレートなら、どれでも受けます
  （サンプル点は 87.5 % 付近）。割れないものは `false` です。
- `setLoopback(true, true)` を `begin()` の前に呼ぶと、送ったフレームがそのまま受信に戻り、TX は出しません（サイレント）。
  トランシーバもバスも無しで、コントローラだけを確かめられます。
- `setPins(rx, tx)` で、variant の `CH32RV_CANn_ROUTES` の経路に移せます（`begin()` の前）。
- `txErrors()` / `rxErrors()` / `busOff()` でエラーの数とバスオフを読めます。バスオフからは自動で戻ります（ABOM）。

## 確かめたこと

V203・V205・H417 で、サイレントのループバックの試験（selfapi::test_can_loopback）が通ります（2026-11-18）。
125 k / 500 k / 1 Mbit/s で、標準 ID と拡張 ID を交互に、0〜8 バイトの 20 通が正しく戻り、エラーの数は 0 です。
実際のバス（トランシーバと 2 台目）での送受信は、まだ確かめていません。

H417 では、受信の FIFO の出口を進めたあと、それが終わるまで待たないと同じフレームを 2 回読みます（ライブラリが待ちます）。
また、初期化のモードに入っても FIFO が空にならないので、`begin()` が残りを捨てます。
