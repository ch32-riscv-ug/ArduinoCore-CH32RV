# SPI

CH32のSPIペリフェラルを使うcontroller専用の実装(polling)です。
pinは`SPI.begin(sck, miso, mosi, ss)`で指定します。
チップセレクトは他のArduinoコアと同じく**ただのGPIO**です。

```cpp
#include <SPI.h>

void setup() {
  pinMode(SS, OUTPUT);
  digitalWrite(SS, HIGH);
  SPI.begin(SCK, MISO, MOSI, SS);   // 実際に配線したpadを書く

  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  digitalWrite(SS, LOW);
  uint8_t reply = SPI.transfer(0x9F);
  digitalWrite(SS, HIGH);
  SPI.endTransaction();
}
```

## どのpinを使うか

指定したpinをrouteに持つ、空いているSPIが割り当てられます。名前とSPIの番号は
結び付いていません。`peripheral()`で、実際に使っているSPIの番号が分かります。

`SPI.begin()`のようにpinを省略した場合は救済として、ボードで有効なSPIの
何番目かに当たるもの(`SPI`は1つ目、`SPI1`は2つ目、`SPI2`は3つ目)を既定のpinで
開きます。ボードメニューでFIXにしたSPIはそのpin、AUTOはvariantの既定routeです。

`SCK` / `MISO` / `MOSI`が1本目のSPIの既定routeです。
`SS`はdevice-dataがNSS padを持っているseriesでのみ定義されます。
ドライバはNSSを使いませんが(CSはGPIOなので)、配線図に出てくるのはこのpadです。

移動させるとき:

```cpp
SPI.setRoute(1);
SPI.setPins(PB3, PB4, PB5);       // SCK, MISO, MOSI。すべて同じrouteから
```

存在しないrouteなら**何も変えずに`false`**。
`setPins()`は3本が同じrouteに属さない場合も拒否します。

## SoftSPI(任意の3 padのSPI)

ハードウェアのSPIはrouteが名指すpadにしか出せません。`SoftSPI`は任意の3 padで
bit-bangするcontrollerで、`SPI`と同じ`SPIClass`なので、`SPIClass *`を取るライブラリが
そのまま受け取れます。同じ`<SPI.h>`(または`<SoftSPI.h>`)で使えます。

```cpp
#include <SPI.h>

SoftSPI bus(PA1, PA4, PA2);       // SCK, MISO, MOSI (SPI.begin()の順)
Adafruit_Something device(&bus);
```

- **ソフトかハードかは、クラスで決めます。** `SPI.begin(sck, miso, mosi)`はハードウェアの
  SPIだけを選び、routeに無いpinなら`false`を返します。
- MISOは`NOT_A_PIN`(`begin()`なら-1)にでき、書き込み専用のバスになります(`transfer()`は0を返す)。
- **クロック周波数を持ちません。** `SPISettings`の周波数は受け取って無視します。速度を変えるのは
  `setHalfPeriodUs()`(半周期の**下限**)だけです。
- 4つのモードと`MSBFIRST` / `LSBFIRST`に対応します。`transfer16()` / `transfer32()`は
  `SPI`と同じく、`LSBFIRST`では下位バイトから送ります。
- CSもslaveモードもありません(CSは`SPI`と同じく普通のGPIOです)。`setRoute()`はありません。`peripheral()`は0を返します。

### ボードメニューでSPIをソフトにする

ボードメニューの**SPI1**(SPI2、SPI3)で**Software**を選ぶと、そのSPIに当たる名前
(`SPI`、`SPI1`、`SPI2`)が`SoftSPI`になります。`begin()`はそのSPIの既定routeのpinを使い、
`begin(sck, miso, mosi)`は任意のpadを受けます。ハードウェアSPIのコードはlinkされません。
グローバルの`SPI`を直接使うライブラリを、ハードウェアSPIの使えない基板で動かすためのものです。

## `SPIClass`

`SPIClass`はこのライブラリのクラス(`<SPIClass.h>`)で、ハードウェアのバス(`CH32RVSPIClass`)と
`SoftSPI`の共通の型です。ArduinoCore-APIの`typedef HardwareSPI SPIClass`ではないので、
`class SPIClass;`の前方宣言が通ります。`SPI`の型は`CH32RVSPIClass`なので、
`extern SPIClass SPI;`や、ESP32の`SPIClass spi(bus)`の書き方は通りません。

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
- **名前は`SPI`、`SPI1`、`SPI2`の順**で、ボードで有効なSPIの数だけあります。
  無い名前を使うとコンパイルエラーになります。
- 旧APIの`setBitOrder()` / `setDataMode()` / `setClockDivider()`も用意しています。
  呼ぶライブラリがまだ多いためです。

## examples

- **SPILoopback** — MOSIとMISOをジャンパ1本で繋ぐだけで、
  クロック・データ線・フレーミングをまとめて確認できます。
- **SoftSPILoopback** — 同じことを`SoftSPI`で、選んだ3 padに対して行います。

## サンプルの詳しい使い方

- [SPILoopback](examples/SPILoopback/README.ja.md)：配線・操作・API の使い方と改造時の注意。
- [SoftSPILoopback](examples/SoftSPILoopback/README.ja.md)：`SoftSPI`で任意の3 padの往復を確かめます。
- [SpiFlashId](examples/SpiFlashId/README.ja.md): 外部SPI Flashを消去・書き換えせずJEDEC IDを読みます。
