# LogicUhsif

[English](README.md)

UHSIF のデータ線 16 本を 200 MS/s で取り込み、そのうち DATA9（PC6）に出した PWM の周波数とデューティ比を測ります。CH32H415 / H416 / H417 用です。

## 必要なものと配線

対象のボードと UART のモニターを用意します。PC6 は UHSIF の DATA9 で、TIM3 の PWM も出せるので、例は自分の出した PWM を測ります。配線は要りません。

外の信号を測るときは、PC6 の `analogWrite()` を外し、信号を DATA0〜15 のどれかにつないで `DATA_BIT` を合わせます（端子の表はライブラリの README）。入力の電圧は、その端子の電源の電圧までです。

## 操作と期待する結果

1 秒ごとに 1 行出ます。

```
PWM 390625.0 Hz, duty 25.0 %, 512.00 samples a period (halves 12238, skipped 12235)
```

- 周波数は、最初と最後の立ち上がりの間の周期の数から出します。PWM と取り込みは同じクロックから作られるので、1 周期はちょうど 512 サンプルです。
- halves は埋まった半分の数、skipped はスケッチが受け取らなかった半分の数です。この例は 1 秒に 1 回しか受け取らないので、skipped が増えるのは正常です。
- `UHSIF capture did not start` が出たら、ボードが H41x か、バッファーの大きさと位置を確かめます。
- `no signal on DATA9 (PC6)` は、受け取った半分の中に立ち上がりが 2 つ無かったときです。

## API と処理の流れ

`logic.begin(UHSIF_DATA, 200000000)` で UHSIF のデータ線を選び、`startContinuous(buf, 32768)` で 16384 サンプルずつの前半と後半を交互に埋め続けます。`readBuffer()` は埋まった半分を返します。サンプルの bit n が DATAn です。

## 改造するときの注意

- 半分（16384 サンプル）は、200 MS/s では 82 µs で次に上書きされます。全部を処理するには、その間に終わる処理にするか、USB などへそのまま渡します。
- 速さを下げると、`begin()` は SYSCLK ÷ n（n = 2〜64）の近い値を選びます。`sampleRate()` で実際の値を確かめます。
- 半分は 20 µs 以上の長さにします。サンプル数は 16 の倍数、バッファーは 16 バイト境界に置きます。

## 関連資料

- [スケッチ](LogicUhsif.ino)
- [ライブラリの API](../../README.ja.md)
- [CH32RVUhsif](../../../CH32RVUhsif/README.ja.md)
- [サンプル案内](../../../../docs/examples.ja.md)
