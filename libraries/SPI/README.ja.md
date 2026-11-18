# SPI

CH32のSPIペリフェラルを使う実装(polling)です。controllerのほか、そう頼んだtransactionではperipheral(slave)になります(下)。
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
- CSはありません(`SPI`と同じく普通のGPIOです)。`SPI`と同じく、そう頼んだtransactionではperipheralになります(下)。
  SCKをpollingで追うので、controllerのクロックの半周期より長い割り込みが入るとビットを落とします。
  `setRoute()`はありません。`peripheral()`は0を返します。

### SoftSPISlave(チップセレクトで区切るslave、任意のpad)

`SoftSPISlave`は[CH32RVSPISlave](../CH32RVSPISlave/README.ja.md)と同じAPIを任意の4 padで動かします:
`begin(sck, miso, mosi, cs, mode, order)`、`queue()`、`poll()` / `onTransfer()`、`transfer()`、`setFill()`、
`missed()`、`overruns()`。

```cpp
#include <SPI.h>                  // または <SoftSPISlave.h>

SoftSPISlave slave;
static uint8_t tx[4] = {0xA5, 1, 2, 3}, rx[4];

void setup() {
  slave.begin(PC5, PC7, PC6, PC4);      // SCK, MISO, MOSI, CS
  slave.queue(tx, rx, sizeof rx);       // 次のチップセレクトに備える
}

void loop() {
  SoftSPISlave::Result r;
  if (slave.poll(r)) {                  // r.length バイト(整数バイト)がクロックされた
    slave.queue(tx, rx, sizeof rx);
  }
}
```

- **1ビットが1回の割り込みです。** CSは両方の縁、SCKはそのモードがサンプルする縁で端子変化の割り込みを取り、
  MOSIを読んでMISOに次のビットを出します。どれも縁からクロックの半周期のうちに走る必要があり、controllerは
  CSが下がってから最初のクロックまでと、最後のクロックからCSが上がるまでに同じだけ空ける必要があります。
  縁を落とすと、クロックが整数バイトにならないチップセレクトとして`overruns()`に数えます。
  実機ではまだ測っていません(2026-11-15)。ベンチの試験は50 kHzで動かします。
- **SCKとCSは別のEXTIの線に**(端子のビット番号。PA1とPB1は同じ線1。`begin()`は`false`)。
  その線はslaveが使います。MISOは-1(受信だけ)、MOSIは-1(送信だけ)にできます。
- MISOはCSがLowの間だけ駆動するので、ほかの機器とバスを共有できます。

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

## バッファの転送と非同期の転送(DMA)

`transfer(buf, n)`、`transferBytes(out, in, n)`、`writeBytes(out, n)`(ESP32の名前)は、
ハードウェアのバスでは、データレジスタを直接回す1本のループで送ります。送るだけ
(`writeBytes`、`transferBytes`の`in`がnull)のときは、次のバイトをすぐ入れるので、バイトの間が空きません。
`SPIClass *`で受け取るライブラリ(表示器のドライバなど)にも効きます。

`transferAsync()`(arduino-picoの名前)は、DMAで送ってすぐ戻ります。転送のあいだもスケッチは動きます:

```cpp
SPI.beginTransaction(SPISettings(36000000, MSBFIRST, SPI_MODE0));
digitalWrite(CS, LOW);
SPI.transferAsync(pixels, nullptr, sizeof pixels);   // false: 始まらなかった
// ... 次の行を描く ...
while (!SPI.finishedAsync()) {}                       // 終わるとDMAのチャネルを返す
digitalWrite(CS, HIGH);
SPI.endTransaction();
```

- `send`がnullなら`0xFF`を送り、`recv`がnullなら受けたものを捨てます。`finishedAsync()`が`true`を返すまで、
  2つのバッファを変えず、そのバスでほかの転送をしないでください。
- `false`になるのは、バスが閉じている・peripheralモード・転送中、または、このSPIのDMAチャネルを
  ほかの利用者(`HardwareSerial`のDMAバッファなど)が持っているときです。V20xのSPI1はUSART3と
  同じチャネルです。チャネルは`transferAsync()`から終わりまでだけ借ります。
- 1回で65535バイトを超えると、何回かに分けて送ります。次の回は、前の回の終わりを見た
  `finishedAsync()`が始めます。
- `abortAsync()`は、線の上のバイトを送り終えたところで止めます。
- `SoftSPI`の`transferAsync()`は、戻る前に送り終えます(DMAが無い)。メニューでSPIをSoftwareにしても、
  同じスケッチがそのまま動きます。
- DMAのコードは`transferAsync()`を呼ぶスケッチだけにリンクされます。

V203(144 MHz)で4096バイトを送った時間(2026-11-18):

| SCK | 1バイトずつ`transfer()` | `writeBytes` | `transferBytes`(送受信) | `transferAsync` |
|---|---|---|---|---|
| 18 MHz | 3707 µs | 1823 µs | 3018 µs | 1823 µs |
| 36 MHz | 2802 µs | 915 µs | 2116 µs | 913 µs(送受信も同じ) |

LVGLのflushは、`transferAsync()`で送り、`lv_display_set_flush_wait_cb()`で`finishedAsync()`を待つ形にできます。

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
- **ArduinoCore-APIの決めたperipheral(slave)モード**(`SPIBusMode`。ArduinoCore-zephyrのSPIが手本)を
  `SPI`と`SoftSPI`の両方で持ち、`SPI_HAS_PERIPHERAL_MODE`を定義しています:

  ```cpp
  SPI.beginTransaction(SPISettings(0, MSBFIRST, SPI_MODE0, SPI_PERIPHERAL));
  uint8_t in = SPI.transfer(out);       // controllerのクロックを待つ
  SPI.endTransaction();                 // controllerに戻る
  ```

  クロックはcontrollerのもので(設定の周波数は使いません)、`transfer()`は手本と同じく時間切れなしで待ちます。
  このAPIにはチップセレクトがありません: `beginTransaction()`から`endTransaction()`まで選ばれたままで
  MISOを駆動し続け、バイトの区切りは`transfer()`の呼び出しだけで決まり、controllerはバイトの間に
  `transfer()`から次の`transfer()`までの時間を空ける必要があります(ハードウェアのSPIは`transfer()`が
  渡したときにそのバイトを入れます)。CSで区切り、先に用意して割り込みで動かす転送は
  [CH32RVSPISlave](../CH32RVSPISlave/README.ja.md)(ハードウェアのSPI)か`SoftSPISlave`(任意のpad)を使います。
  peripheralのコードは`SPI_PERIPHERAL`の`SPISettings`を作るスケッチだけにリンクされます(ArduinoCore-APIの
  `SPISettings`への変更。`tools/vendor/check_api_sync.py`)。バスに残るのはフラグの判定だけです。
  実機ではまだ測っていません(2026-11-15)。
- **名前は`SPI`、`SPI1`、`SPI2`の順**で、ボードで有効なSPIの数だけあります。
  無い名前を使うとコンパイルエラーになります。
- 旧APIの`setBitOrder()` / `setDataMode()` / `setClockDivider()`も用意しています。
  呼ぶライブラリがまだ多いためです。

## examples

- **SPILoopback** — MOSIとMISOをジャンパ1本で繋ぐだけで、
  クロック・データ線・フレーミングをまとめて確認できます。
- **SoftSPILoopback** — 同じことを`SoftSPI`で、選んだ3 padに対して行います。
- **SpiAsyncDisplay** — ST7789の表示器に、DMA(`transferAsync()`)で送る間に次の行を描きます。

## サンプルの詳しい使い方

- [SPILoopback](examples/SPILoopback/README.ja.md)：配線・操作・API の使い方と改造時の注意。
- [SoftSPILoopback](examples/SoftSPILoopback/README.ja.md)：`SoftSPI`で任意の3 padの往復を確かめます。
- [SpiFlashId](examples/SpiFlashId/README.ja.md): 外部SPI Flashを消去・書き換えせずJEDEC IDを読みます。
- [SpiAsyncDisplay](examples/SpiAsyncDisplay/README.ja.md): `transferAsync()`で送る間に描く、ST7789の表示器。LVGLのflushでの使い方も。
