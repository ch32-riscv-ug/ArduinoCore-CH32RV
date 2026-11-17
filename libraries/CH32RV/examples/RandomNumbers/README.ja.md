# RandomNumbers

[English](README.md)

同じ seed による疑似乱数の再現と、seed を変えた場合の違いを確かめます。

## 必要なものと配線

UART モニタを用意します。

## 操作と期待する結果

リセットして、`seed 1:` の列が再現されること、`ch32rv_random()` を seed にした列と乱数のバイト列が実行ごとに変わること、`d6:` に1～6の値が出ることを確認します。

## API と処理の流れ

`randomSeed(seed)` で生成列の初期条件を設定します。呼ばなければ `random()` は毎回同じ列で、ほかの Arduino の板と同じです。`random(min, max)` の上限 max は含まれないため、六面体は `random(1, 7)` です。`ch32rv_random()` と `ch32rv_fill_random(buf, len)` は arduino-esp32 の `esp_random()` と `esp_fill_random()` にあたり、V30x・V4x7・H41x ではハードウェアの乱数を返します（そこでは `CH32RV_RNG_BASE` が定義されます）。

## 改造するときの注意

ハードウェアの乱数が無い石では、`ch32rv_random()` は石の固有の ID と呼んだ時刻を混ぜます。石ごと・実行ごとに変わりますが予測できるので、暗号鍵や認証用の値へ使いません。`random()` 自体は暗号用ではありません。再現テストでは固定 seed が便利です。

## 関連資料

- [スケッチ](RandomNumbers.ino)
- [基本サンプルの準備と一覧](../README.ja.md)
- [スケッチの組み立て方](../../../../docs/sketch-guide.ja.md)
