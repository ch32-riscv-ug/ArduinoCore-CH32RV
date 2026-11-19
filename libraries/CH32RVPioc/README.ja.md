# CH32RVPioc

[English](README.md)

PIOC（WCH の RISC8B、X035 / X033・V205・H41x の I/O プロセッサ）を使います。PIOC は 4 KiB の自分のプログラム SRAM と
2 本の端子（IO0、IO1）を持つ 8 bit のコアで、端子をサイクル単位で動かします。プログラムはスケッチ（かライブラリ）の
`.S` にコアのマクロ（`<ch32rv_pioc.inc>`）で書き、スケッチと同じ clang で組みます。ほかのアセンブラは要りません。

X035 / X033 と V205 では、PIOC のプログラム用 SRAM が普通の RAM の 4 KiB です（X035 は上端で、スタックがその下に移る。
V205 は下端で、`.data` などがその上に移る）。PIOC を使うスケッチでだけリンカが空け、使わないスケッチは RAM を全部使えます。
X035 の端子は PCFR1 の remap で選びます（IO0 / IO1 が PC18 / PC19、または IO0 が PC7）。PC18 / PC19 は SWD と兼用です。
H417・X035・V205 のベンチ（PA0・PC7・PB10）で試験が通っています（2026-11-19）。

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

PIOC の届く端子は variant の `CH32RV_PIOC_PADS` です（H417 は IO0 が PA0・PB8・PC1・PE3・PF12、IO1 が PB9・PC0・PC2・PE4・
PF13・PF14。PB8 / PB9 は SWD と兼用）。

## 例

- [PiocPulses](examples/PiocPulses/README.ja.md): コマンド n ごとに IO0 に n 個のパルスを出す。
