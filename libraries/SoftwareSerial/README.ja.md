# SoftwareSerial

[English](README.md)

どの 2 本の端子でも作れる UART です。API は Arduino の SoftwareSerial（AVR のライブラリの `begin`、`listen`、`isListening`、
`stopListening`、`overflow`、`peek`、`read`、`write`、`available`、`flush`）と同じです。ハードウェアの USART の route が
その端子に届かないとき、USART が足りないとき（CH32V003 は 1 つ）に使います。

```cpp
#include <SoftwareSerial.h>

SoftwareSerial gps(PC1, PC2);      // RX, TX: どの端子でもよい

void setup() {
  gps.begin(9600);                 // 8N1
  // gps.begin(9600, SERIAL_8E1);  // HardwareSerial と同じ形式の指定
}

void loop() {
  while (gps.available()) Serial.write(gps.read());
}
```

## しくみ

- **受信。** スタートビットの縁で RX の端子の割り込み（`attachInterrupt()`）が起き、その中でフレームの残りを読みます。
  各ビットの真ん中を SysTick で計ります。割り込みが届くまでの時間は `listen()` が測り（同じ EXTI の線にソフトウェアで縁を
  起こす）、その分を差し引くので、石やクロックに依りません。受信したバイトは全部のオブジェクトで共有する 64 バイトの
  バッファ（`_SS_MAX_RX_BUFF`）に入ります。受信するのは listen しているオブジェクトだけです（AVR と同じ）。バッファが
  満杯ならそのバイトを捨て、`overflow()` が知らせます。フレーミングやパリティの誤りのあるフレームは捨てます。
- **送信。** `write()` は割り込みを止めて 1 ビットずつ送り（ストップビットは割り込みを戻して待つ）、送り終えてから
  戻ります。`flush()` は待つものがありません。
- **半二重**（AVR と同じ）。送信中に届いたバイトは失われ、ほかの割り込み（Serial、USB、`millis()`）は最大 1 フレーム
  待たされます。受信の割り込みも 1 フレームと、次のスタートビットを見張る 1 ビットの間 CPU を使います。続けて来るフレームは同じ割り込みの中で、見つけた縁から計って読みます（遅い石の 115200 では、新しい割り込みでは間に合わないため）。
- **形式。** `begin(speed, config)` は `SERIAL_5N1`〜`SERIAL_8O2`（データ 5〜8 ビット、パリティなし・偶数・奇数・
  マーク・スペース、ストップ 1・1.5・2 ビット）を受け付けます。`begin(speed)` は 8N1 です。コンストラクタの
  3 番目の引数 `inverse_logic` は待機を Low にします。
- **端子。** どの端子でも使えます。listen している間、RX の端子はそのビット番号の EXTI の線を使うので、同じビット
  番号の別の端子は `attachInterrupt()` を使えません。端子でない値（255）を渡すと、その側は無し（送るだけ・受けるだけ）
  になります。

## 速さ

受信は、縁から 1.5 ビットのうちに割り込みが届く必要があります。CH32X035（48 MHz）は約 5 µs かかるので、遅い石では
115200（1 ビット 8.7 µs）が実用の上限です。ベンチの試験（`software-serial`）は 9600・57600・115200 の 8N1 と、
9600 の 8E1・8O1・8N2・7E1 を、プローブの UART と両方向で確かめます。受信は 9600 で必ず通ることを求め、それより速い速さは
結果を記録します（石の割り込みの遅れしだいで、約束しない）。ベンチ（2026-11-17）では V003・X035・V203・V205・H417 は 115200
まで受け、V006（24 MHz）は 115200 で 16 バイト中 7 バイトでした。送信はどの台もどの速さでも正しく出ました。

## AVR のものとの違い

- 形式を選ぶ `begin(speed, config)` を足しています。
- ビットの時間は、クロック数を数えた待ちの表ではなく SysTick で計るので、どの F_CPU でも動きます。

## 例

- [SoftwareSerialExample](examples/SoftwareSerialExample/README.ja.md): `Serial` と SoftwareSerial を中継します。
