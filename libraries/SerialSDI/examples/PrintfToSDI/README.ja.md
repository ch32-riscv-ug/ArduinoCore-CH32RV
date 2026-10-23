# PrintfToSDI

[English](README.md)

`printf()` の出力先を UART と SDI の間で切り替えます。

## 必要なものと配線

SDI 用のデバッグ接続を用意します。最初と切り戻し後の UART 出力も見る場合は UART モニタを接続します。

## 操作と期待する結果

起動時に UART→SDI→UART の順で表示され、その後の `tick` は切り戻した UART へ出ます。SDI モニタの開き方は [HelloSDI](../HelloSDI/README.ja.md) を参照します。

## API と処理の流れ

`ch32rv_set_stdout(&SerialSDI)` で stdio の出力先を差し替えます。`&Serial` を渡せば UART に戻ります。指定した出力オブジェクトは使用中有効に保ちます。

## 改造するときの注意

変わるのは stdio の出力先です。`Serial.print()` は引き続き UART に出ます。ライブラリが `Serial` を直接使う場合と `printf()` を使う場合を区別して、ログの経路を確認します。

## 関連資料

- [スケッチ](PrintfToSDI.ino)
- [ライブラリの API](../../README.ja.md)
- [サンプル案内](../../../../docs/examples.ja.md)
