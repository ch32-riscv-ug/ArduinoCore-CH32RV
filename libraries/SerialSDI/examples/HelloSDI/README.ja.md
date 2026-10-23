# HelloSDI

UART を使わず、デバッグ接続を通して文字列を表示します。

## 必要なものと配線

対応プローブとデバッグ接続を用意します。MCU の UART 端子の配線は不要です。

## 操作と期待する結果

`ch32rv monitor --source sdi --chip CH32V003` のように対象チップに合わせてモニタを開始し、起動メッセージと経過秒数を確認します。

## API と処理の流れ

`SerialSDI.begin()` 後に `print()` / `println()` を使います。baud 引数は UART クロックを設定するものではありません。

## 改造するときの注意

ホスト側も SDI を読み出す設定が必要です。プローブ・系列ごとの利用条件は [デバッグ出力ガイド](../../../../docs/debug-output.ja.md) を確認します。同じデバッグレジスタを使う SerialDMDATA / SerialDMSeq と同時に使いません。

## 関連資料

- [スケッチ](HelloSDI.ino)
- [ライブラリの API](../../README.ja.md)
- [サンプル案内](../../../../docs/examples.ja.md)
