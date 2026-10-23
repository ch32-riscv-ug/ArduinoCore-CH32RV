# HelloRTT

[English](README.md)

RAM のリングバッファを通して、デバッグプローブからログを読みます。

## 必要なものと配線

RTT を読み出せるプローブとデバッグ接続を用意します。UART の配線は不要です。

## 操作と期待する結果

`ch32rv monitor --source rtt --chip CH32V203` のように対象へ合わせて開始し、起動メッセージと経過秒数を確認します。

## API と処理の流れ

`SerialRTT.begin()` 後に `print()` / `println()` を使います。baud は UART の速度設定ではなく、ホスト側は RAM 内の RTT 情報を読み出します。

## 改造するときの注意

バッファが満杯のときの扱いを把握し、ホスト不在時もログが無限に保存されるとは考えません。RAM の読み出し方や対象への影響はプローブ側にも依存するため、[デバッグ出力ガイド](../../../../docs/debug-output.ja.md) を参照します。

## 関連資料

- [スケッチ](HelloRTT.ino)
- [ライブラリの API](../../README.ja.md)
- [サンプル案内](../../../../docs/examples.ja.md)
