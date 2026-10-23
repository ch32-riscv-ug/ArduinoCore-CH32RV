# HelloDMSeq

[English](README.md)

デバッグ接続の双方向端末で、入力文字を大文字にして返します。

## 必要なものと配線

対応プローブとデバッグ接続を用意します。UART 端子は使いません。

## 操作と期待する結果

`ch32rv monitor --source dmseq` を開き、入力の返信と経過秒数を確認します。対象指定が必要な環境ではモニタへその指定も渡します。

## API と処理の流れ

`SerialDMSeq.begin()` 後に `available()` / `read()` / `write()` を使います。`available()` は入力を受け取るためのホストとのやり取りも進めるので、出力がない間も呼びます。

## 改造するときの注意

ホスト側は DMSeq 対応の端末を使います。SDI / DMDATA と同じレジスタを別方式で使うため同時に使いません。送信が待つ条件や相手不在時の扱いは [ライブラリ README](../../README.ja.md) を参照します。

## 関連資料

- [スケッチ](HelloDMSeq.ino)
- [ライブラリの API](../../README.ja.md)
- [サンプル案内](../../../../docs/examples.ja.md)
