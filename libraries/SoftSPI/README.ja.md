# SoftSPI

English: [README.md](README.md)

任意の3 padで動くbit-bang SPI。APIは[SPI](../SPI)と同じです。

```cpp
#include <SoftSPI.h>

SoftSPI bus(PA1, PA4, PA2);      // SCK, MISO, MOSI (SPI.begin()'s order)
```

## なぜ要るか

ハードウェアのSPIは、routeが名指すpadにしか出せません。CH32の小さい部品では
そのpadが使えないことがよくあります — そのpackageにbondされていない、あるいは
既に別の用途で埋まっている。CH32V003のSOP8はGPIOが全部で6本です。

これがその逃げ先です。`pinMode()`と`digitalWrite()`しか使わないので、
pinがあるところならどこでも動きます。

## そのまま差し替わります

`SoftSPI`は`SPI`と同じ`HardwareSPI`を継承しているので、`SPIClass&`を取る
ライブラリがそのまま受け取れます。

```cpp
SoftSPI bus(PA1, PA4, PA2);
Adafruit_Something device(&bus);
```

## やらないこと

- **クロック周波数を持ちません。** `SPISettings`の周波数は受け取って無視します。
  bit-bangでは数値を狙えないからです。壊れはしません — SPIはコントローラが
  クロックを出すので、遅いのは転送が遅いだけです。速度を変える唯一のつまみは
  `setHalfPeriodUs()`で、これは半周期の**下限**を決めます。長い配線や、
  ループより遅いクロックを要求するデバイス向けです。
- **CSは持ちません。** `SPI`と同じで、ArduinoはCSを普通のGPIOとして駆動します。
  1本のバスに複数デバイスをぶら下げられるのはそのためです。
- **slaveモードはありません。** slaveは相手のクロックに追従する必要があり、
  ビジーループでは保証できません。`SPI_HAS_PERIPHERAL_MODE`は定義しません。

## モード

4つとも対応します。`SPI_MODE0`〜`SPI_MODE3`と`MSBFIRST`/`LSBFIRST`を、
`beginTransaction(SPISettings(...))`か、従来の`setDataMode()`/`setBitOrder()`で。

## コスト

edgeごとに`digitalWrite()`を呼び、`HardwareSPI`のoverrideも保持するため、hardware
`SPI`より小さくなるとは限りません。正確な使用量はbuild size reportで確認してください。
ヘッダを`#include`しないsketchにはlinkされません。
