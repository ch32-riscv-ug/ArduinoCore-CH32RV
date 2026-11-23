# CH32RVQspi

[English](README.md)

クアッド SPI のコントローラー（CH32V205 の QSPI1、CH32H41x の QSPI1 / QSPI2）で、外付けのシリアルフラッシュなどに命令を送ります。命令・アドレス・代替バイト・ダミー・データの段ごとに、1 線・2 線・4 線を選べます。Arduino・arduino-esp32 に相当するものはありません。

```cpp
#include <CH32RVQspi.h>

CH32RVQspi qspi;                                   // QSPI1（QSPI2 は qspi2(2)）

void setup() {
  qspi.begin(PA12, PB0, PA2, PA3, PA0, PA1);       // SCK, CS, IO0〜IO3（IO2 / IO3 は 4 線のときだけ）
  qspi.setClock(10000000);

  CH32RVQspiCommand id;                            // 9F: JEDEC ID を 1 線で 3 バイト
  id.instruction = 0x9F;
  uint8_t jedec[3];
  qspi.read(id, jedec, 3);
}
```

| 関数 | 意味 |
|---|---|
| `begin(sck, cs, io0, io1, io2 = -1, io3 = -1)` | 端子。CS が SCSN の端子なら 1 つ目のフラッシュ（IO は SIO0〜3）、SCSXN なら 2 つ目（SIOX0〜3） |
| `setClock(hz)` | HCLK ÷ 1〜256 のうち hz 以下で一番速いもの。実際の速さを返す |
| `setMode(0 / 3)` | SPI のモード（このコントローラーは 0 と 3） |
| `command(cmd)` | データの段の無い命令（書き込み許可、アドレス付きの消去など） |
| `write(cmd, data, n)` / `read(cmd, data, n)` | 命令に続けて n バイトを `cmd.dataLines` 本で送る／受ける |
| `memoryMap(readCmd, flashBytes)` / `mapped()` | フラッシュをメモリとして読む（CH32H41x の QSPI1 の 0x90000000 だけ。ほかは番地が資料に無く false）。ほかの呼び出しで終わる |
| `end()` | 止めて端子を入力に戻す |

`CH32RVQspiCommand` の項目: `instruction`、`instructionLines`（0・1・2・4）、`address`、`addressLines`（0 で無し）、`addressBytes`（1〜4、既定 3）、`alternate`・`alternateLines`・`alternateBytes`、`dummyCycles`（0〜31）、`dataLines`（1・2・4）。

- 各端子の機能は端子ごとに選ばれます（V205・H41x）。使える端子は variant の `CH32RV_QSPIn_PADS` です。CH32H416 は QSPI2 の端子だけ、CH32H415 には QSPI の端子がありません。
- 4 線の段では IO2 / IO3 が要ります（CR の SIOXEN を立てる）。
- 待ちはポーリングで、各段 100 ms で諦めます。

## 例

- [QspiFlashInfo](examples/QspiFlashInfo/README.ja.md): フラッシュの JEDEC ID と、1・2・4 線の読み出し。

## 試験

periph::test_qspi: 1 つ目のフラッシュの SCK・CS・IO0・IO1 の 4 本の線で、消去（20）・書き込み（02）・2 線の書き込み（A2）・高速読み出し（0B、ダミー 8）・2 線の読み出し（BB、ダミー 4）を WireSkein の qspi で段ごとに判定（相手は無く、IO はプローブのプルアップで ff）。V205 で通ります（2026-11-23）。4 線・実際のフラッシュ・メモリマップ・2 つ目のフラッシュは未確認（線と部品が要る）。H41x はビルドのみ。
