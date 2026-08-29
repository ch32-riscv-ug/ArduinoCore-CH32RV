# Wire (I2C)

CH32のI2Cペリフェラルを使うmaster専用の実装です。pinはvariantから来るので
`Wire.begin()`に引数は要りません。

```cpp
#include <Wire.h>

void setup() {
  Wire.begin();
  Wire.beginTransmission(0x3C);
  Wire.write(0x00);
  if (Wire.endTransmission() == 0) {
    // デバイスが応答した
  }
}
```

## どのpinを使うか

`SDA`と`SCL`が1本目のバスの既定route(データシートどおり)を指します。
seriesごとに違うので、分からなければ表示させてください。

```cpp
Serial.print(SCL); Serial.print(' '); Serial.println(SDA);
```

**既定routeが全型番でbondされているとは限りません。** CH32X033/X035では
PA10/PA11ですが、これが出ているのは7型番中2つだけです。
board側に無い場合は移動させます。

```cpp
Wire.setRoute(2);                 // データシートのroute番号
Wire.setPins(PC16, PC17);         // padで指定してもよい
```

どちらも、存在しないrouteなら**何も変えずに`false`**を返します。
`setPins()`は**別routeのSCLとSDAを混ぜた場合も拒否**します。
ハードウェアはペリフェラルごと移動するものであり、
X035のいくつかのrouteは**同じpadでSCLとSDAが入れ替わる**ので、
順序は意味を持ちますし、実際に検査しています。

`begin()`のあとに呼ぶと、古いpadを入力に戻してから新しいpinで開き直します。

## 知っておくとよいこと

- **プルアップは利用者側の責任です。** coreはpadをオープンドレインにしますが、
  内蔵プルアップはI2Cには弱すぎます。モジュール側に無ければ各線に4.7kを
  3V3へ入れてください。無いと全転送がタイムアウトします。
- **すべての待ちに上限があります。** `CH32_WIRE_TIMEOUT_US`(既定25ms)で
  各ステップを打ち切るので、バスが固まってもsketchは止まりません。
  `endTransmission()`の戻り値は 5=タイムアウト、2=アドレスNACK、
  3=データNACK、1=書きすぎ、0=成功です。
- **バッファは32バイト**(AVRと同じ)。超えると切り捨てて1を返します。
  `-DCH32_WIRE_BUFFER_SIZE=128`で増やせますが、
  送受信の両方が、インスタンスごとに増えます。
- **slaveモードは未実装です。** `begin(address)`・`onReceive()`・`onRequest()`は
  受け付けますが何もしません(中途半端に動くよりよいと判断しています)。
- **2本目は`Wire1`**です。ペリフェラル番号ではなく**バスの順番**で、
  Arduinoエコシステムの慣習に合わせています。
- タイムアウトは25msで、`endTransmission()`は5を返します。**既定で有効**です
  (AVRは既定で無効で、バスが固まるとスケッチが永久に止まります)。
  `setWireTimeout(us)`で変更、`setWireTimeout(0)`で無効。
  `getWireTimeoutFlag()`はstickyで、`clearWireTimeoutFlag()`まで残ります。
  タイムアウト後はどちらにせよペリフェラルをリセットするので、
  `reset_with_timeout`は受け取るだけで挙動を変えません。
- クロックは`setClock(100000)`で標準モード、それより速い値でfast mode(2:1)。
  ペリフェラルクロックは`F_CPU`と仮定しています
  (HSI直結・APB分周1という現在の構成で成り立ちます)。

## examples

- **I2CScanner** — 応答するデバイスを列挙します。
  プルアップが無くてタイムアウトした場合は、そうと分かる表示をします。
