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

`Wire.begin()`のようにpinを省略した場合は救済として、既定のpinで開きます。
`Wire`は、ボードで有効なI2Cのうち自分のpinを持つ最初のもの(ボードメニューで
FIXにしたI2Cはそのpin、AUTOはvariantの既定route)、`Wire1`はもう1つです。
H417以外の系列では番号順と同じです。H417はI2C1に系列全体の既定routeが無い
(PB8 / PB9はSWD、PB6 / PB7は無いパッケージがある)ので、`Wire`がI2C2、
`Wire1`がI2C1になり、`Wire1`は`begin(sda, scl)`でpinを指定します。

`SDA`と`SCL`は`Wire`の既定route(データシートどおり)を指します。
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

## SoftWire(任意の2 padのI2C)

ハードウェアのI2Cはrouteが名指すpadにしか出せません。小さい部品ではそのpadが
bondされていない、あるいは別の用途で埋まっていることがよくあります。
`SoftWire`は任意の2 padでbit-bangするcontroller(とtarget)で、`Wire`と同じ`TwoWire`なので、
`TwoWire *`を取るライブラリがそのまま受け取れます。同じ`<Wire.h>`(または`<SoftWire.h>`)で使えます。

```cpp
#include <Wire.h>

SoftWire bus(PA1, PA2);           // SDA, SCL
Adafruit_Something device(&bus);
```

- **ソフトかハードかは、クラスで決めます。** `Wire.begin(sda, scl)`はハードウェアの
  I2Cだけを選び、routeに無いpinなら`false`を返します。黙ってソフトに切り替えることはしません。
- 両padを`OUTPUT_OPENDRAIN`にします(X033 / X035はGPIOにオープンドレインが無いので、
  入力と出力Lowの切り替えで同じことをします)。**プルアップは必要です。**
- クロックストレッチを待ちます。`endTransmission()`の戻り値とtimeout APIは`Wire`と同じです。
- `setClock()`のクロックを、CPUと線が許す所まで保ちます。半周期ごとに締め切りの時刻まで待つので、端子の書き込みとSCLの立ち上がりの時間は待ちの中に吸収されます。ベンチで測った値(2026-11-15、プローブの弱いプルアップ): 20 / 50 kHzはどの台も1 %以内。上限はX035で約67 kHz(オープンドレインを真似るため)、V006で約78 kHz、V003で約102 kHz、V103で約210 kHz、V205で約373 kHz、V203で約526 kHz、H417で約862 kHz。上限より速くは頼んでも上限で動き、頼んだより速くはなりません。`getClock()`は指定した値を返します。
- **slave(target)にもなります。** `bus.begin(0x42)`(または`bus.begin(0x42, sda, scl, 0)`)で0x42に応え
  (10 bitのアドレス`0x2A5 | WIRE_10BIT`も、`setGeneralCall(true)`でアドレス0も)、
  契約は`Wire`のtargetと同じです: STOPのあと(register readではrepeated STARTで`onRequest()`より先)に`onReceive(count)`、
  読みのアドレスで`onRequest()`、書いた分より多く読まれたら0xFF、バッファより長い書き込みは頭を残します。
  START(SDAの立ち下がり、`attachInterrupt()`)で入った割り込みが、STOPまで両方の線を見てcontrollerのクロックを追います:
  SCLの立ち上がりでSDAを読み、SCLがHighの間のSDAの変化がrepeated START / STOPです。次のLowでSDAに何をするか
  (ACK、手放す、送る次のビット)はその前のHighの間に決めておき、立ち下がりを見たらすぐ書きます。callbackの所
  (読みのアドレスの`onRequest()`、repeated STARTで払う`onReceive()`)では**終わるまでSCLをLowに握ります**
  (クロックストレッチ)。callbackは時間を掛けてよく、controllerはストレッチを待つ必要があります。
  間に合う必要があるのは、STARTの割り込みが最初のビットのLowが終わるまで(100 kHzで約10 µs)と、
  各Lowで立ち下がりを見てから1回の書き込みがSCLの立ち上がりまでです。そのためビットのループはRAMで動きます
  (約0.5 KB)。100 kHzのcontrollerで測った値(2026-11-15、Lowは5.5 µs): 書き込みは立ち下がりから
  V003(24 MHz)で2.0〜3.7 µs、X035(48 MHz)で3.1〜4.0 µs。V003・X035・V203で試験が通ります。
  見積もりの上限はX035・V003とも100 kHz台で、400 kHzは無理です。**バスが動いている間(ほかのtargetの転送も)
  CPUはその割り込みの中にいます。**同じか高い優先度の割り込みがその間に走るとビットを落とすことがあります。
  SCLは割り込みを使わないので、SDAとSCLが同じEXTIの線(同じビット番号)でもかまいません。SDAの線はtargetが使います
  (自分の`attachInterrupt()`を重ねない)。targetとEXTIの処理は`begin(address)`を呼ぶスケッチだけにリンクされます。
- **役割は1つずつです。** `Wire`と違い、targetとして始めた`SoftWire`はcontrollerを兼ねません。
  `begin()`でcontrollerに戻すまで`endTransmission()`は4、`requestFrom()`は0です(自分のSTARTを
  自分の割り込みが追ってしまうため)。multi-masterのarbitrationもありません。
- 10 bitのアドレスとSMBusのPECは`Wire`と同じです(下)。10 bitのアドレスかgeneral callのtargetは、別のループ
  (flashで動く。`WIRE_10BIT`を書くか`setGeneralCall()`を呼ぶスケッチだけにリンク)で線を追います。7 bitだけのtargetは
  上のRAMのループのままです。flashの待ちのある石では遅くなるので、試験は50 kHzまでです。
- **SDAを握ったままのtarget**(byteの途中で切られた)は、STARTの前ごとにSDAがLowの間SCLを最大9回打って
  離させます(`Wire`が失敗した転送のあとに行うbus clearと同じ)。
- `setRoute()`はありません(ハードウェアだけの機能です)。`peripheral()`は0を返します。`clearBus()`は`Wire`と同じ手順でバスを解放します。
- バッファは`Wire`と同じ`CH32RV_WIRE_BUFFER_SIZE`(既定32)です。
- 開いていないバスへの`endTransmission()`は、`Wire`と同じく4を返します(自動では`begin()`しません)。

### ボードメニューでWireをソフトにする

ボードメニューの**I2C1**(I2C2)で**Software**を選ぶと、そのI2Cに当たる名前
(`Wire`、`Wire1`)が`SoftWire`になります。`begin()`はそのI2Cの既定routeのpinを使い、
`begin(sda, scl)`は任意のpadを受けます。ハードウェアI2Cのコードはlinkされません。
グローバルの`Wire`を直接使うライブラリを、ハードウェアI2Cの使えない基板で動かすためのものです。
このとき`Wire.setRoute()`などハードウェアだけの関数はコンパイルエラーになります。

## 知っておくとよいこと

- **プルアップは利用者側の責任です。** coreはpadをオープンドレインにしますが、
  内蔵プルアップはI2Cには弱すぎます。モジュール側に無ければ各線に4.7kを
  3V3へ入れてください。無いと全転送がタイムアウトします。
- **I2Cが使えないことのあるX035があります。** データシートは、ロット番号の後ろから
  5桁目が0のCH32X035 / X033ではI2Cが使えない(ADCのch3・7・11・15も)としています。
  そのロットのC8T6では、PA10 / PA11でI2Cを有効にするとSDAがhighのままになり、
  どの相手もACKできず、`endTransmission()`は2(アドレスNACK)を返しました。
  同じロットのF8U6では、PC16 / PC17(route 2)のI2Cが通りました。使う前に刻印を確かめてください。
- **すべての待ちに上限があります。** `CH32RV_WIRE_TIMEOUT_US`(既定25ms)で
  各ステップを打ち切るので、バスが固まってもsketchは止まりません。
  `endTransmission()`の戻り値は 5=タイムアウト、2=アドレスNACK、
  3=データNACK、1=書きすぎ、4=開いていないバス・ほかのcontrollerに毎回バスを取られた(下)、0=成功です。
- **バッファは32バイト**(AVRと同じ)。超えると切り捨てて1を返します。
  `-DCH32RV_WIRE_BUFFER_SIZE=128`で増やせますが、
  送受信の両方が、インスタンスごとに増えます。
- **slaveモードは割り込み駆動です**(`Wire`も`SoftWire`も)。`begin(address)`でslaveを選び、
  `onReceive()`と`onRequest()`のcallbackは割り込みcontextで呼ばれます。
- **`Wire`はcontrollerとtargetを兼ねます。** `Wire.begin(0x42)`のあとも`beginTransmission()` /
  `endTransmission()` / `requestFrom()`が使えます。転送はバスが空くのを待ち(その間にこのtargetへ来た転送には
  応えます)、バスを取り、終わるとtargetの待ち受けに戻ります。待ちの上限はバスのタイムアウトです。
  `endTransmission()`のために組み立て中のメッセージは`onRequest()`で壊れません(targetの返事は別のバッファ)。
  受信のバッファは共有なので、`requestFrom()`で受けたバイトは、ほかのcontrollerがこのtargetに書く前に読んでください
  (`onReceive()`のメッセージで置き換わります)。
- **multi-master。** arbitrationに負けた転送(転送の途中でほかのcontrollerのSTART・STOPを見た、自分のSTARTが
  出る前にtargetとして呼ばれた、も同じ)はバスを手放し、空くのを待って最初からやり直します。やり直しは
  `CH32RV_WIRE_ARBITRATION_RETRIES`(3)回までで、それでも負ければ`endTransmission()`は4(AVRの「その他のエラー」)、
  `requestFrom()`は0です。AVR・arduino-esp32・STM32duinoは最初の負けを4で返し、やり直しはスケッチに任せます。
  BUSYが残っただけに見えるバスは、両方の線が100 µs続けてHighのときだけresetします(ほかのcontrollerの転送を
  取り違えないため)。
- **10 bitのアドレス**: `Wire.beginTransmission(0x2A5 | WIRE_10BIT)`、`Wire.requestFrom(0x2A5 | WIRE_10BIT, n)`、
  targetは`Wire.begin(0x2A5 | WIRE_10BIT)`。線の上はヘッダー11110 + ビット9:8 + W、下位バイト、読みはrepeated STARTと
  ヘッダー + R。targetへの10 bitの読み(アドレスだけの書き込みを折り返したもの)では`onReceive(0)`を呼びません。
  `WIRE_10BIT`はcontrollerの10 bitのコードと同じ所で定義した定数で、名前を書いたスケッチだけにそのコードが
  リンクされます。コンパイル時の定数ではありません(`constexpr`や`case`のラベルには使えません)。
- **general call**: `Wire.setGeneralCall(true)`でtargetがアドレス0にも応え、そのバイトは普通の書き込みと同じく
  `onReceive()`に来ます。controllerからは`beginTransmission(0)`で送れます。
- **SMBusのPEC**: `Wire.setPEC(true)`で、STOPで終わる書き込みの後ろに取引全体(アドレスのバイトを含む)の
  CRC-8(x^8 + x^2 + x + 1)を付け、`requestFrom(address, n)`はnバイトの後ろのデバイスのPECも読みます。合わなければ
  0を返し、何も読めません。`endTransmission(false)`で終えた書き込みは、そのCRCを続く読みに持ち越します
  (SMBusのread byte / word)。ソフトウェアで計算し、`Wire`も`SoftWire`も同じです。`setPEC()`を呼ぶスケッチだけに
  リンクされます。targetが返すPECには`TwoWire::pec(crc, data, len)`を使えます。
- **DMAはまだありません。** 転送はバッファの32バイトまでで、controllerはポーリング、targetは割り込みです。
- **slaveはバスのオブジェクトで始めます**(`Wire.begin(0x42)`)。`TwoWire *`越しには呼べません。
  `begin(address)`は`Wire`と`SoftWire`のクラスのもので、`TwoWire`にはありません。呼ばないスケッチには
  slaveの割り込みの処理がリンクされません(V003でcontrollerだけの`Wire`のスケッチは3364 B、644 B小さい)。
  `TwoWire`がArduinoCore-APIの`HardwareI2C`から派生しないのも同じ理由です。
- **名前は`Wire`、`Wire1`の順**で、ボードで有効な(メニューでDisableにしていない)
  I2Cの数だけあります。無い名前を使うとコンパイルエラーになります。
- タイムアウトは25msで、`endTransmission()`は5を返します。**既定で有効**です
  (AVRは既定で無効で、バスが固まるとスケッチが永久に止まります)。
  `setWireTimeout(us)`で変更、`setWireTimeout(0)`で無効。
  `getWireTimeoutFlag()`はstickyで、`clearWireTimeoutFlag()`まで残ります。
  タイムアウト後はどちらにせよペリフェラルをリセットするので、
  `reset_with_timeout`は受け取るだけで挙動を変えません。
- **targetがSDAをLowに握ったまま**(resetやclockの途切れでbyteの途中で切られた)
  だと、以後の転送はすべて失敗し、このペリフェラルをresetしても直りません
  (握っているのは相手です)。`Wire.clearBus()`はslaveが離すまでSCLを最大9回打ち、
  STOPを出して、両方の線がHighなら`true`を返します。controllerは、転送が
  NACK以外で失敗し、ペリフェラルをresetしたあとSDAがLow・SCLがHighのとき、これを
  自分で行います(そのバスはもう使えず、I2Cの仕様のbus clearが戻し方です。パルスは
  `setClock()`より速くせず、最も速くて100 kHz)。起動時に握られていたバスには自分で
  呼んでください。`begin()`の前でも後でも使え、開いていたバスは元の状態で
  開き直します。線は内蔵プルアップで解放し、SCLをHighに駆動することはないので、
  clock stretchingするslaveは待ちます(`CH32RV_WIRE_CLEAR_STRETCH_US`、1パルス1 ms)。
- クロックは`setClock(100000)`で標準モード、それより速い値でfast mode(2:1)。一番遅いのは12ビットのCCRに入る値(I2Cのクロックが100 MHzで約12 kHz、24 MHzで約2.9 kHz)です。`setClock()`はarduino-esp32と同じく`bool`を返し、0とそれより遅い値には`false`を返して今の速さを保ちます(設定すると頼んだより速くなるため)。周辺回路の出せる一番速い速さより速い値は、その一番速い速さで動きます。SoftWireが断るのは0だけです。どちらも、設定した速さより速くはなりません。マスターは新しいクロックを次の取引から使います。マスターはSCLがHighになったのを見てからHighの時間を数えるので、立ち上がりが遅い(プルアップが弱い)と周期が延びます(ベンチのプローブのプルアップだけで、400 kHzが330〜394 kHz)。RMのI2Cは標準モード(100 kHz)と高速モード(400 kHz)です。それより速い値も設定はできるので頼まれたとおりにしますが、石と線で動くかは約束しません: ベンチ(2026-11-15)では1 MHzを頼むとどの台も0.55〜0.96 MHzで動き、3.4 MHzを頼むとV006・V103・V203・V205・X035・H417では転送がすべて時間切れ(5)になりました(V003は2 MHzで動いた)。
- **H417のハードのI2Cは、プルアップの弱い線で規格外のクロックを出すことがあります。** ベンチ(2026-11-14、プローブの45 kΩのプルアップだけ、20〜100 kHz)では、取引の約1割で、SCLのHighが20〜80 nsしかないクロックが混ざりました(前後のLowは普通の長さ。1を送るビットに多い。I2CのFREQ・TRISEでは変わらない)。受け手によっては余分なクロックと数え、アドレスやデータを取り違えます(DUTは2・3・5を返す)。H41xの端子にはオープンドレインのまま内蔵のプルアップを付ける設定が無いので、2.2〜4.7 kΩの外付けのプルアップを付けて使ってください。外付けで潰れが消えるかは、まだ確かめていません。同じ線でもSoftWireは通ります。
  driverはペリフェラルクロックを`F_CPU`と仮定するため、system clockとAPBの設定を
  変更する場合はこの前提も更新する必要があります。

## examples

- **I2CScanner** — 応答するデバイスを列挙します。
  プルアップが無くてタイムアウトした場合は、そうと分かる表示をします。
- **SoftI2CScanner** — 同じことを`SoftWire`で、選んだ2 padに対して行います。
- **I2COledSH1106** — SH1106（128×64 の OLED）に、Wire だけで文字とカウンタを出します。
- **I2COledSH1106U8g2** — 同じ表示を U8g2 のライブラリで（フォント・図形。U8g2 は同梱しません）。

## サンプルの詳しい使い方

- [I2CScanner](examples/I2CScanner/README.ja.md)：配線・操作・API の使い方と改造時の注意。
- [SoftI2CScanner](examples/SoftI2CScanner/README.ja.md)：`SoftWire`で任意の2 padを走査します。
- [I2COledSH1106](examples/I2COledSH1106/README.ja.md)：表示のライブラリ無しで SH1106 の OLED を動かします（WeAct X035 ではデバッグの端子で）。
- [I2COledSH1106U8g2](examples/I2COledSH1106U8g2/README.ja.md)：同じことを U8g2 で（版は sketch.yaml に固定）。
- [I2CRegisterRead](examples/I2CRegisterRead/README.ja.md): レジスタを選んで読み、書き込みエラーと受信長を確認します。
- [I2CSlaveRegister](examples/I2CSlaveRegister/README.ja.md): 短い割り込みcallbackで小さなレジスタを公開します。
- [I2CTimeoutRecovery](examples/I2CTimeoutRecovery/README.ja.md): NACKとtimeoutを区別し、明示的な操作でバスを復旧します。
