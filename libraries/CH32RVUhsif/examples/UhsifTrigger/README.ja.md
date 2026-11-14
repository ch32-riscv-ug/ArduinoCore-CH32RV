# UhsifTrigger

[English](README.md)

データの比較器（`setDataMatch()`）で DATA2 の立ち上がりを待ち、そこから DATA0〜7 を 1 サンプル 1 バイト（`setBusWidth(8)`）で 4096 サンプル、1 回だけ取り込みます。取り込みはちょうど立ち上がりの瞬間から始まります。CH32H415 / H416 / H417 用です。

## 必要なものと配線

対象のボードと UART のモニターを用意します。配線は要りません。スケッチが PD12（DATA2）にタイマーの PWM（100 kHz、duty 1/4）を出し、それを取り込みます。

## 操作と期待する結果

1 秒ごとに 1 行出ます。SYSCLK が 400 MHz なら 200 MS/s です。

```
first sample 44, high 500 samples, period 2000 samples (100.0 kHz at 200000000 Hz), unwritten 0
```

- `first sample` の bit 2 は 1（立ち上がりの直後から取っている）。ほかのビットは、つないでいない端子の値です。
- `high` は duty 1/4 の長さ、`period` は周期のサンプル数です。
- `unwritten` は書かれなかったサンプルの数で、0 になります。DMA は 64 バイトの burst で書くので、積むのをやめると最後の端数がブロックに残ります。そのため、64 バイト余分に積んでいます。

## API と処理の流れ

1. `begin(1)` で SYSCLK ÷ 2、`setBusWidth(8)` で DATA0〜7 を 1 バイトずつにします。
2. `setDataMatch(1 << 2, 1 << 2)` で、DATA2 が 1 のとき比較器（`DataMatch`）が真になります。
3. 状態機械は、DATA2 が 0 になるのを待ち（状態 0 / 1）、1 になるのを待ち（2 / 3）、毎クロック積んで数え（4 / 5）、上限で止まります（6 / 7）。どの段も 2 つの状態にします（同じ状態へ戻る遷移は判断し直さない）。
4. `setRing(0, capture, 4096, 1, 4096)` で 1 つのバッファーに書き、少し待ってから `stop()` します。

## 改造するときの注意

- 「0 を待ってから 1 を待つ」のでエッジのトリガーになります。1 を待つだけだと、始めたときに High なら途中から取ります。
- データ線の外の端子（CTRL0〜15 = PB14〜PB10、PE15〜PE7、PF13、PF12）でトリガーするときは、`setControlMatch()` と `ControlMatch` を使います。
- 輪にして取り続けるときは、1 つのバッファーを書き終えるたびに止まらないよう 2 スレッドにします（CH32RVLogicCapture の形）。トリガーの位置は、トリガーの遷移で `Interrupt` を出し、`bytesWritten()` を読むと分かります。

## 関連資料

- [スケッチ](UhsifTrigger.ino)
- [ライブラリの API](../../README.ja.md)
- [UHSIF の解析](../../../../docs/h417-uhsif.ja.md)
