# SPISlaveSelfTest

[English](README.md)

マスターも配線も無しで [CH32RVSPISlave](../../README.ja.md) を確かめます。SPI1 の端子でスレーブを開いたあと、スケッチ自身が SCK・MOSI・CS を GPIO の出力として動かします。SPI は端子の状態を読むので、チップが自分のマスターになります。MISO は `digitalRead()` で読み返します。

## 配線

不要です。**SPI1 の端子と CS の端子には何もつながないでください。** スケッチがこれらの端子を駆動します。

| | SCK | MISO | MOSI | CS |
|---|---|---|---|---|
| CH32V00x / M007 | PC5 | PC7 | PC6 | PC4 |
| CH32H41x | PF5 | PF3 | PD7 | PF4 |
| そのほか | PA5 | PA6 | PA7 | PA4 |

## 確かめること

SPI のモード 0〜3 と、モード 0 の LSB 先頭で、次を確かめます。

1. キューに入れた転送で、双方向にデータが届く。
2. キューが空のときの CS（埋め草のバイトを送り、`missed()` に数える）。
3. マスターがバッファより多く送る（`tx` のあとは埋め草のバイト。`length` には全部の数が入る）。
4. キューに入れた 2 つの転送が順に使われる。クロックの無い CS のパルスではキューを使わない。
5. `onTransfer()` と `deliverEvents()` で結果を受け取る。

## 出力

Serial（115200）に各ステップを出し、そのあと `SPISLAVE PASS` か `SPISLAVE FAIL` を 1 秒ごとに出します。失敗したステップは、受け取ったバイトと期待したバイトを出します。コンソールの無いプローブでは、グローバル変数 `g_result` で結果を読めます（1 は合格、2 は不合格）。`g_failed_step` は最初に失敗したステップです。

[SPISlaveSelfTest.ino](SPISlaveSelfTest.ino)
