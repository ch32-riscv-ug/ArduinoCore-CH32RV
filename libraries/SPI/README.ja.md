# SPI

CH32のSPIペリフェラルを使うcontroller専用の実装(polling)です。
pinはvariantから来ます。チップセレクトは他のArduinoコアと同じく**ただのGPIO**です。

```cpp
#include <SPI.h>

void setup() {
  pinMode(SS, OUTPUT);
  digitalWrite(SS, HIGH);
  SPI.begin();

  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  digitalWrite(SS, LOW);
  uint8_t reply = SPI.transfer(0x9F);
  digitalWrite(SS, HIGH);
  SPI.endTransaction();
}
```

## どのpinを使うか

`SCK` / `MISO` / `MOSI`が1本目のバスの既定routeです。
`SS`はdevice-dataがNSS padを持っているseriesでのみ定義されます。
ドライバはNSSを使いませんが(CSはGPIOなので)、配線図に出てくるのはこのpadです。

移動させるとき:

```cpp
SPI.setRoute(1);
SPI.setPins(PB3, PB4, PB5);       // SCK, MISO, MOSI。すべて同じrouteから
```

存在しないrouteなら**何も変えずに`false`**。
`setPins()`は3本が同じrouteに属さない場合も拒否します。

## 知っておくとよいこと

- **クロックは切り上げず、必ず切り下げます。** 48MHzの部品で
  `SPISettings(1000000, ...)`と書くと750kHzになります。
  分周は2のべき乗しか作れず、**上振れはデバイスを壊す側の誤差**だからです。
- **`begin()`がMISOをプルアップします。** 未接続でもノイズではなく`0xFF`が
  読めるので、「デバイスが居ない」が「たまに化ける」に見えません。
- **`transfer16()`は8bit×2で送ります**(ビット順は尊重します)。
  16bitフレームへ切り替えないので、`transfer()`と混ぜても設定が半端になりません。
- **`usingInterrupt()`は何もしません。** この実装は割込みからバスを触らないので
  守るべき共有状態がありません。sketchが自分のISRからSPIを使う場合は、
  sketch側で面倒を見る必要があります。
- **peripheral(slave)モードは未実装**で、`SPI_HAS_PERIPHERAL_MODE`は
  意図的に未定義にしてあります(ライブラリが判定できるように)。
- **2本目は`SPI1`、3本目は`SPI2`**です。ペリフェラル番号ではなくバスの順番。
- 旧APIの`setBitOrder()` / `setDataMode()` / `setClockDivider()`も用意しています。
  呼ぶライブラリがまだ多いためです。

## examples

- **SPILoopback** — MOSIとMISOをジャンパ1本で繋ぐだけで、
  クロック・データ線・フレーミングをまとめて確認できます。
