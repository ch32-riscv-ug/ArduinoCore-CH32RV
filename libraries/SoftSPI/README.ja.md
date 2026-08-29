# SoftSPI

English: [README.md](README.md)

任意の3 padで動くbit-bang SPI。APIは[SPI](../SPI)と同じです。

```cpp
#include <SoftSPI.h>

SoftSPI bus(PA1, PA2, PA4);      // SCK, MOSI, MISO
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
SoftSPI bus(PA1, PA2, PA4);
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

CH32V003(flash 16 KB)で、同じスケッチをハードの`SPI`で組んだ場合と比較した実測:

| | flash |
|---|---|
| ハードの`SPI` | +1056 B |
| `SoftSPI` | +1376 B |

**`SoftSPI`のほうが大きい**です。`digitalWrite()`がエッジごとの関数呼び出しに
なるのに対しペリフェラルはレジスタ1回の書き込みで済むこと、そして
`HardwareSPI`を継承するとvtableが全overrideを保持することが理由です。
このうち128 Bは`setHalfPeriodUs()`が`delayMicroseconds()`を引き込む分です。

どちらも16 KBの部品に余裕で入り、**ヘッダを`#include`しないスケッチには
どちらのコストもかかりません**。
