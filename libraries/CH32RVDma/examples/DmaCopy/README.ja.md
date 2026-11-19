# DmaCopy

[English](README.md)

DMA で 256 バイトを写す間、CPU は数え続けます。写し終わりはコールバックで知り、中身を確かめます。次に塗りつぶしを確かめます。

## 必要なものと配線

どの石でも。配線は要りません。

## 操作と期待する結果

`Serial` に次のように出ます（DMA とチャネルの番号、数えた値は石で違います）。

```
DMA1 channel 7
copy: same, the CPU counted to 41
fill: all 0xA5
```

## API と処理の流れ

`begin()` が空いているチャネルを取り、`onDone()` で終わりのコールバックを決め、`copyAsync()` が写しを始めてすぐ戻ります。
コールバックがフラグを立てるまで数え、`memcmp()` で確かめます。`fill()` は終わるまで待つ形です。
