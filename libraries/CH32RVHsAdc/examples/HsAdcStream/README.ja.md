# HsAdcStream

[English](README.md)

高速 ADC の連続データを、USB Vendor の bulk IN へ直接送ります。

## 必要なものと配線

対象ボードの USB データ接続と UART モニタ、PC0 の入力を用意します。VIO18 を変更するため、[HsAdcRead](../HsAdcRead/README.ja.md) の電源条件も確認します。

## 操作と期待する結果

ホストから vendor interface の bulk IN を読み、16 bit little-endian のサンプルとして解釈します。UART の sent・lost・overruns・host busy を併せて観察します。

## API と処理の流れ

`startContinuous(bufA, bufB, samples)` は二つの配列を交互に埋めます。`readBuffer()` が返した完成バッファを `stream.writeDirect(pointer, byteLength)` へ渡します。サンプル数とバイト数を区別します。

## 改造するときの注意

`writeDirect()` は転送開始の受付であり、呼び出しから戻っても USB がバッファを参照しています。ADC が同じ領域へ戻る前に送信完了することが必要です。ホストが遅い場合は単なるカウンタの監視だけでデータの整合性を保証できません。完了通知、追加バッファ、取得停止や破棄の方針を設けます。

### ホストの受信とストリーム形式

選んだsample rateに十分な帯域のUSB経路を使います。例の既定IDは`1209:0001`です。class `0xff`のinterfaceを選び、bulk INのendpointは固定値ではなく記述子から取得します。同期readの呼び出し間でendpointを繰り返し遊ばせないよう、libusbの非同期readを複数投入します。

送信内容はアプリ用headerを付けない16 bit little-endianの生sample列です。この例のADC値は0～1023です。必要なpayload帯域は`sampleRate() × sizeof(uint16_t)` bytes/sで求めます。sent/lostはsample数ではなくbuffer数の累計で、生ストリーム内の欠落位置を表すものではありません。

## 関連資料

- [スケッチ](HsAdcStream.ino)
- [ライブラリの API](../../README.ja.md)
- [サンプル案内](../../../../docs/examples.ja.md)
