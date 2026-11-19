# CH32RVPioc

[English](README.md)

PIOC（WCH の RISC8B、X035 / X033・V205・H41x の I/O プロセッサ）を使います。PIOC は 4 KiB の自分のプログラム SRAM と
2 本の端子（IO0、IO1）を持つ 8 bit のコアで、端子をサイクル単位で動かします。プログラムはスケッチ（かライブラリ）の
`.S` にコアのマクロ（`<ch32rv_pioc.inc>`）で書き、スケッチと同じ clang で組みます。ほかのアセンブラは要りません。

X035 / X033 と V205 では、PIOC のプログラム用 SRAM が普通の RAM の 4 KiB です（X035 は上端で、スタックがその下に移る。
V205 は下端で、`.data` などがその上に移る）。PIOC を使うスケッチでだけリンカが空け、使わないスケッチは RAM を全部使えます。
X035 の端子は PCFR1 の remap で選びます（IO0 / IO1 が PC18 / PC19、または IO0 が PC7）。PC18 / PC19 は SWD と兼用です。
H417・X035・V205 のベンチ（PA0・PC7・PB10）で試験が通っています（2026-11-19）。

## 使い道と限界

PIOC は「端子 2 本つきの、クロック単位で正確に動く小さな 8 bit のコア」です。メインのコアの代わりに、決まった 1 つの
仕事（多くは端子の波形）を受け持ちます。何でも動かせるわけではありません。

**向いているもの**
- ハードウェアの周辺に無い、1〜2 本の線のプロトコルを、メインのコアの割込みを止めずに動かす: WS2812 の多数の LED、
  1-Wire（DS18B20）、赤外線リモコン（NEC 方式）の送受信、独自の 1 線式、マンチェスター符号。
- パルスの幅や間隔を、メインのコアの割込み（USB、millis()）に揺らされずに測る・作る。
- 端子の変化を見張り、条件がそろったときだけメインのコアに知らせる（`onDone()` の割込み）。

**向いていないもの**
- ハードウェアの周辺（USART、I2C、SPI、タイマーの PWM・捕捉）でできること。そちらのほうが速く、端子も自由です。
- 3 本以上の線（SPI の 4 線、パラレルバス）。PIOC の端子は IO0 と IO1 の 2 本で、選べる端子も少数です
  （X035 は IO0 が PC18 か PC7、IO1 が PC19。PC18 / PC19 は SWD と兼用）。
- 大量のデータの流し込み。DMA も FIFO も無く、メインのコアとは 32 バイトの共有レジスタとコマンドの 1 バイトで
  やりとりします。長いデータは PIOC の 4 KiB の SRAM（プログラムと共用）に置きます。
- 計算の多い仕事。8 bit で、乗算も無く、スタックは 6 段です。
- 同時にいくつも。動くプログラムは 1 つで、別の仕事に替えるには読み込み直します。

**時間の目安**: PIOC は HCLK で動きます（測定: H417 約 100 MHz、X035 48 MHz、V205 160 MHz）。命令は 1〜3 サイクル
（分岐の成立・CALL・RET は 2、RDCODE は 3）なので、X035 では 1 命令 約 21 ns、端子を High / Low にするループは数サイクルが下限です。
待ち時間を命令の数で作るので、クロックが違えば速さも変わります（系列ごとに数え直す）。

**Raspberry Pi Pico の PIO と比べると**: PIO は 1 命令 1 クロックの小さな状態機械が 4 つずつあり、どの GPIO でも
複数本をまとめて扱え、FIFO と DMA で高速に流せます。代わりに 1 ブロック 32 命令で、計算や判断はほとんどできません。
PIOC は逆に、1 つだけで端子も 2 本、FIFO も DMA もありませんが、比較・加減算・条件分岐・サブルーチン（6 段）といった
汎用の命令があり、判断の入る処理が書けます。プログラムは 4 KiB の SRAM（最大 2048 語。長いデータを置けばその分減る）です。PIO のプログラムはそのままでは移せず、PIO の感覚で「速く・広く・流し込む」用途に使うと合いません。
「判断の入る遅めのプロトコルを、1〜2 本の線で、メインのコアの代わりに正確にこなす」のが PIOC の居場所です。

**ESP32 の ULP と比べると**: どちらも「メインのコアとは別に動き、終わったら知らせる小さなコア」という点は似ています。
ただし ULP の主な目的はメインのコアが深く眠っている間の見張り（低消費電力）です。PIOC は HCLK で動くので、
クロックが止まる眠りでは止まります（2026-11-19 に X035・V205・H417 で測定）。

| メインのコアの状態 | PIOC |
|---|---|
| 素の WFI（Sleep、HCLK は動いている） | 動き続ける。PIOC の割込み（`onDone()`）でメインのコアが起きる |
| `CH32RV.lightSleep()` の Stop の部分 | 止まる。起きると続きから動く（途中のパルスは延びる）。PIOC の割込みでは起きない |
| `CH32RV.deepSleep()`（Standby、再起動） | 止まり、プログラムも失われる（起きた後に `begin()` からやり直す） |

`lightSleep()` は、RTC の系列（V205、H417 など）では 1 秒未満の眠りを素の WFI で過ごすので、その間は PIOC が動きます。
X035 は AWU の Stop で眠るので、短くても止まります。PIOC の割込みを `lightSleep()` の起床の理由にする API はまだありません。
つまり省電力の見張り役ではなく、「メインのコアが動いているか WFI で待っている間に、時間に厳しい端子の仕事を肩代わりする」
使い方が中心です。

## プログラムを書く

```c
// pulses.S
#include <ch32rv_pioc.inc>

PIOC_BEGIN pulses
        DW     0x0000                   // 0 番地は予約（実行されない）
START:  BC     SFR_PORT_IO, SB_PORT_OUT0
        MOVA1F 0x01                     // IO0 を出力に
WAIT:   WAITB  WB_DATA_MW_SR_1          // ホストが cmd.in に書くのを待つ
        MOV    SFR_CTRL_WR, A
        ...
PIOC_END pulses
```

- 命令・別名・`f,d` の書き方（d は A / 0 で A へ、F / 1 / 省略でレジスタへ）・SFR とビットの名前は WCH のもの
  （RISC8B の命令セットのマニュアル、EVT の `PIOC_INC.ASM`）です。EVT の ASM は行ごとに移せます。
- 違うのは次の点です。コメントは `//` か `/* */`（C のプリプロセッサを通るため。`;` は文の区切り、`#` はディレクティブ）。
  定数は `.equ 名前, 値`（か `#define`）。プログラムは `PIOC_BEGIN 名前` と `PIOC_END 名前` の間に書きます。
  `ORG`・`DW`・`DB`（RETL）もマクロで、`ORG` は前にしか進めず、間を NOP で埋めます。
- 組むときに検査します。即値・レジスタ・ビットの幅、JMP / CALL の飛び先が 2048 語の中か、条件ジャンプ
  （JNZ / JZ / JNC / JC）の飛び先の bit 10 が次の語と同じか（コアは下位 10 bit だけを置き換える）、CMPZ の飛び先の
  bit 10:8 が同じか（下位 8 bit だけ）、イメージが 4096 バイト以下か。前方参照も含めて `PIOC_END` でまとめて調べ、
  外れていれば `.error` で止めます。
- このファイルには PIOC のコードだけを書きます。マクロ（NOP、AND、RET、CALL など）が、同じ名前の RISC-V の命令の
  代わりになるためです。
- 出力が正しいことは、X035・V205・H417 の EVT の 15 本を `.S` に移し、ベンダーの `.BIN` とバイト単位で一致させて確かめました
  （2026-11-19）。突き合わせはリポジトリの外で行い、ベンダーのバイナリはここに置いていません。

## スケッチから使う

```cpp
#include <CH32RVPioc.h>

PIOC_IMAGE(pulses);                       // .S の PIOC_BEGIN pulses
CH32RVPioc pioc;

void setup() {
    pioc.begin(PIOC_IMAGE_OF(pulses), PA0);  // IO0 を PA0 に、IO1 は使わない
}

void loop() {
    pioc.command(3);                      // cmd.in（SFR_CTRL_WR）
    int status = pioc.waitDone(100);      // cmd.out（SFR_CTRL_RD）、時間切れは -1
}
```

| API | 動き |
|---|---|
| `begin(image, io0, io1)` | PIOC を止め、イメージを SRAM に写し、端子を PIOC に渡して 1 番地から走らせる。端子は -1 で使わない。PIOC の無い石、空か 4 KiB を超えるイメージ、PIOC の届かない端子では false |
| `end()` | 止めて（リセットのまま、クロックを切る）端子を戻す |
| `command(v)` | cmd.in に書く。プログラムは `WAITB WB_DATA_MW_SR_1` で待ち、`MOV SFR_CTRL_WR, A` で読む |
| `done()` | プログラムが `SB_INT_REQ` を立てた |
| `waitDone(ms)` | `done()` まで待って cmd.out を読み、要求を下ろす。時間切れは -1 |
| `result()` | 今の cmd.out。要求も下ろす（`SB_INT_REQ` はホストが cmd.out に書くと消え、読むだけでは消えない。2026-11-19 に X035 で確認） |
| `onDone(cb)` | 要求を割込みで受ける。割込みが cmd.out を読んで要求を下ろし、`cb(cmd.out)` を呼ぶ（割込みの中なので短く）。`nullptr` で `waitDone()` に戻る |
| `writeData(i, p, n)` / `readData(i, p, n)` / `write(i, v)` / `read(i)` | 共有のレジスタ d0〜d31（`SFR_DATA_REG0` + i） |
| `loadSram(offset, p, n)` | プログラム SRAM のイメージより後ろ（`offset` バイト目から）にデータを写す。プログラムは `RDCODE` で語（`offset / 2`）ごとに読む。PIOC が走っている間 SRAM は PIOC 専用なので、リセットして写し、最初から走らせ直す（d0〜d31 も消えるので、そのあとで書く）。イメージと重なる・4 KiB を超えるときは false |
| `clockHz()` | PIOC のクロック（HCLK。H41x ではメインのコアより遅い）。プログラムの時間はこのサイクルで数える |

PIOC の届く端子は variant の `CH32RV_PIOC_PADS` です（H417 は IO0 が PA0・PB8・PC1・PE3・PF12、IO1 が PB9・PC0・PC2・PE4・
PF13・PF14。PB8 / PB9 は SWD と兼用）。

## 例

- [PiocPulses](examples/PiocPulses/README.ja.md): コマンド n ごとに IO0 に n 個のパルスを出す。
- [PiocWs2812](examples/PiocWs2812/README.ja.md): WS2812（NeoPixel）を PIOC で光らせる。色のデータは `loadSram()` で SRAM に置き、待ちの回数は `clockHz()` から計算する。H417・X035・V205 で WireSkein の `rgb_led` が通っています（2026-11-19）。
