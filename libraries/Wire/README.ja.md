# Wire (I2C)

CH32のI2Cペリフェラルを使うmaster/slave実装です。pinはESP32と同じ形で
指定します: `Wire.begin(sda, scl)`、`Wire.begin(sda, scl, 400000)`。
ライブラリには`TwoWire *`(`&Wire`)で渡し、`SoftWire`もそこへ渡せます。

```cpp
#include <Wire.h>

void setup() {
  Wire.begin(SDA, SCL);           // 実際に配線したpadを書く
  Wire.beginTransmission(0x3C);
  Wire.write(0x00);
  if (Wire.endTransmission() == 0) {
    // デバイスが応答した
  }
}
```

## どのpinを使うか

指定したpinをrouteに持つ、空いているI2Cが割り当てられます。名前とI2Cの番号は
結び付いていません(`Wire`にI2C2のpin、`Wire1`にI2C1のpinを渡してもかまいません)。
`peripheral()`で、実際に使っているI2Cの番号が分かります(開いていなければ0)。

`Wire.begin()`のようにpinを省略した場合は救済として、ボードで有効なI2Cの
何番目かに当たるもの(`Wire`は1つ目、`Wire1`は2つ目)を既定のpinで開きます。
ボードメニューでFIXにしたI2Cはそのpin、AUTOはvariantの既定routeです。

`SDA`と`SCL`が1本目のI2Cの既定route(データシートどおり)を指します。
seriesごとに違うので、分からなければ表示させてください。

```cpp
Serial.print(SCL); Serial.print(' '); Serial.println(SDA);
```

**既定routeが全packageでbondされているとは限りません。** CH32X033/X035では
PA10/PA11ですが、このpairを外へ出していないpackageもあります。
board側に無い場合は移動させます。

```cpp
Wire.setRoute(2);                 // データシートのroute番号
Wire.setPins(PC17, PC16);         // padで指定してもよい(SDA, SCL)
```

どちらも、存在しないrouteなら**何も変えずに`false`**を返します。
`setPins()`はESP32と同じくSDAが先です。**別routeのSDAとSCLを混ぜた場合も拒否**します。
ハードウェアはペリフェラルごと移動するものであり、
X035のいくつかのrouteは**同じpadでSCLとSDAが入れ替わる**ので、
順序は意味を持ちますし、実際に検査しています。

`begin()`のあとに呼ぶと、古いpadを入力に戻してから新しいpinで開き直します。

## 知っておくとよいこと

- **プルアップは利用者側の責任です。** coreはpadをオープンドレインにしますが、
  内蔵プルアップはI2Cには弱すぎます。モジュール側に無ければ各線に4.7kを
  3V3へ入れてください。無いと全転送がタイムアウトします。
- **すべての待ちに上限があります。** `CH32RV_WIRE_TIMEOUT_US`(既定25ms)で
  各ステップを打ち切るので、バスが固まってもsketchは止まりません。
  `endTransmission()`の戻り値は 5=タイムアウト、2=アドレスNACK、
  3=データNACK、1=書きすぎ、0=成功です。
- **バッファは32バイト**(AVRと同じ)。超えると切り捨てて1を返します。
  `-DCH32RV_WIRE_BUFFER_SIZE=128`で増やせますが、
  送受信の両方が、インスタンスごとに増えます。
- **slaveモードは割り込み駆動です。** `begin(address)`でslaveを選び、
  `onReceive()`と`onRequest()`のcallbackは割り込みcontextで呼ばれます。
  1つのinstanceをmasterとslaveに同時利用すること、multi-master arbitration、
  general callへの応答には対応しません。
- **名前は`Wire`、`Wire1`の順**で、ボードで有効な(メニューでDisableにしていない)
  I2Cの数だけあります。無い名前を使うとコンパイルエラーになります。
- タイムアウトは25msで、`endTransmission()`は5を返します。**既定で有効**です
  (AVRは既定で無効で、バスが固まるとスケッチが永久に止まります)。
  `setWireTimeout(us)`で変更、`setWireTimeout(0)`で無効。
  `getWireTimeoutFlag()`はstickyで、`clearWireTimeoutFlag()`まで残ります。
  タイムアウト後はどちらにせよペリフェラルをリセットするので、
  `reset_with_timeout`は受け取るだけで挙動を変えません。
- **slaveがSDAをLowに握ったまま**(resetやclockの途切れでbyteの途中で切られた)
  だと、以後の転送はすべて失敗し、このペリフェラルをresetしても直りません
  (握っているのは相手です)。`Wire.clearBus()`はslaveが離すまでSCLを最大9回打ち、
  STOPを出して、両方の線がHighなら`true`を返します。バス上の全デバイスにパルスが
  届くので**自動では行いません**。転送がタイムアウトし、打ってよいと分かっている
  ときに呼んでください。`begin()`の前でも後でも使え、開いていたバスは元の状態で
  開き直します。線は内蔵プルアップで解放し、SCLをHighに駆動することはないので、
  clock stretchingするslaveは待ちます(`CH32RV_WIRE_CLEAR_STRETCH_US`、1パルス1 ms)。
- クロックは`setClock(100000)`で標準モード、それより速い値でfast mode(2:1)。
  driverはペリフェラルクロックを`F_CPU`と仮定するため、system clockとAPBの設定を
  変更する場合はこの前提も更新する必要があります。

## examples

- **I2CScanner** — 応答するデバイスを列挙します。
  プルアップが無くてタイムアウトした場合は、そうと分かる表示をします。

## サンプルの詳しい使い方

- [I2CScanner](examples/I2CScanner/README.ja.md)：配線・操作・API の使い方と改造時の注意。
- [I2CRegisterRead](examples/I2CRegisterRead/README.ja.md): レジスタを選んで読み、書き込みエラーと受信長を確認します。
- [I2CSlaveRegister](examples/I2CSlaveRegister/README.ja.md): 短い割り込みcallbackで小さなレジスタを公開します。
- [I2CTimeoutRecovery](examples/I2CTimeoutRecovery/README.ja.md): NACKとtimeoutを区別し、明示的な操作でバスを復旧します。
