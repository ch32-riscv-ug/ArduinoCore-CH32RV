# BlockPool

English: [README.md](README.md)

コピーしたくない大きさの記録を、プールのブロックで貸し借りします。ISR が借りて書き、ポインタを `Queue` で送り、`loop()` が使って返します。

## 必要なものと配線

`BUTTON_PIN`（既定 `PA1`）と GND の間に押しボタン。115200 bps・8N1 の UART モニタを `RX` / `TX` に。

## 操作と期待する結果

1. 押すと `RECORD ms=... us=... free=n` が出ます。
2. 1 秒の間に 5 回以上押すとブロック（4 個）が尽き、`NO_BLOCK n` が出ます。

## 設定と制約

`release()` は、このプールのブロックでないものや返却済みのもの（二重返却）を断ります。Queue の大きさをブロック数と同じにすると、送るときに満杯になりません。

## API の使い方と改造のポイント

`pool.tryAllocate()` が `nullptr` なら空きがありません。取得したブロックへ書き込んだあと、そのポインタを Queue へ渡し、受け手が処理を終えてから `pool.release()` します。返却後はポインタを使いません。

Queue や生成側を変更するときは、`trySend()` に失敗した場合もブロックを返す経路を用意します。補助の失敗カウンタを正確に取得・リセットする場合は、その二つの操作も短い禁止区間にまとめます。

## 関連資料

- [サンプル共通の準備](../README.ja.md)
- [CH32RVSyncKit の API](../../README.ja.md)

[BlockPool.ino](BlockPool.ino)
