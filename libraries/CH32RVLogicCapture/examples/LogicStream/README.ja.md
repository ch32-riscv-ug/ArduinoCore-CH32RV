# LogicStream

[English](README.md)

GPIO ポートを連続取得し、完成した半バッファを USB Vendor の bulk IN へ送ります。

## 必要なものと配線

対象ボードの USB データ接続と UART モニタを用意します。例は PE3 に観察用 PWM を出します。外部信号は各 GPIO の許容電圧を確認して接続します。

## 操作と期待する結果

ホストで vendor interface の bulk IN を読み、16 bit little-endian の bitmap として解釈します。bit 3 の周期と、UART の sent/lost を確認します。

## API と処理の流れ

`logic.startContinuous(buf, samples)` は一つの配列を二分して循環使用します。`readBuffer()` が返すのは完成した半分です。`writeDirect()` へ渡す長さは「半分のサンプル数 × sizeof(uint16_t)」です。

## 改造するときの注意

取り込み DMA と USB が同じ領域へ同時に触らないようにします。送信受付後もバッファは転送完了まで保持し、DMA がそこへ戻るまでに USB が読み終わる必要があります。処理猶予は「半分のサンプル数 ÷ 実サンプルレート」です。保証が必要なら完了管理と、遅れたときの停止・破棄を実装します。

## 関連資料

- [スケッチ](LogicStream.ino)
- [ライブラリの API](../../README.ja.md)
- [サンプル案内](../../../../docs/examples.ja.md)
