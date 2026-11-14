# DualCoreV3F

[English](README.md)

スケッチを V5F で動かしたまま、CH32H417 の 2 つ目のコア V3F でコードを動かします。

## 機材と接続

**CPU Core = V5F**（既定）の CH32H417 のボード。`Serial` に UART のモニターをつなぎます（CH32H417QEU6 では PB14 が TX、PB15 が RX。`Serial.setPins()` で変えられます）。

## 手順と期待する結果

V5F が 1 秒ごとに、V3F が見つけた素数の数と最後の素数を表示します。どちらの数も増え続けます。

## API と処理の流れ

- `setup1()` / `loop1()`: スケッチにどちらかがあれば、コアが `setup()` の前に V3F を起こし、そこで動かします（arduino-pico の 2 つ目のコアと同じ書き方）。V3F のスタック（4 KB、`CH32RV_V3F_STACK_SIZE`）はヒープから取ります。
- `CH32RV.startV3F(fn, arg, stack, size)`: `setup1()` / `loop1()` を使わないときは、関数を 1 つずつ V3F に渡せます。戻ったかは `CH32RV.isV3FRunning()` で分かります。`CH32RV.onV3F()` でどちらのコアかが分かります。
- データの受け渡しは、書く側を片方に決めた `volatile` の変数か、A 拡張の不可分命令で行います。共有 SRAM には、どちらのコアにもキャッシュがありません。

## 改造するとき

V3F では `Serial`・割り込み・V5F が使うペリフェラルを使いません。`millis()`・`micros()`・`delay()`・`delayMicroseconds()` は使えます（V3F の `delay()` は USB などのシステムのサービスを回しません）。命令セットは両方のコアで同じ（rv32imafc と B）なので、スケッチのどの関数もどちらのコアで動かせます。戻らない関数を渡すと、リセットまで V3F はそのままです（外から止める仕組みはまだありません）。

## 関連文書

- [スケッチ](DualCoreV3F.ino)
- [基本サンプルの使い方と一覧](../README.ja.md)
- [CH32H417 のコアの組み方](../../../../docs/h417-cores-design.ja.md)
