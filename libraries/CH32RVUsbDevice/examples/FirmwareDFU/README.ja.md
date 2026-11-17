# FirmwareDFU

English: [README.md](README.md)

スケッチを動かしたまま、USB の DFU（ダウンロード）で新しいファームウェアを受け取り、確かめてから入れ替えます。

## 必要なものと配線

USB デバイスのある系列の板と PC（`dfu-util` か `ch32rv`）。進み具合は `Serial`（UART）に出ます。
受け取れるイメージは「flash − このスケッチ」までです。V203 の ANY（32 KB）では足りないので、品番を選びます。

## 操作と期待する結果

1. このスケッチを書き込み、PC に `1209:0001` が現れることを確かめます。
2. 入れ替えるスケッチ（このスケッチでもよい）をビルドし、`.bin` を送ります:
   `dfu-util -d 1209:0001 -a Firmware -D <sketch>.ino.bin`（または `ch32rv boot dfu flash <sketch>.ino.bin --format bin`）。
3. `Serial` に `received <バイト数>` が増えていき、終わると USB から一度消えて、新しいスケッチで現れます。
4. スケッチでないファイル（テキストなど）を送ると、dfu-util は errWRITE で失敗し、このスケッチがそのまま動き続けます。

## 設定と制約

- 既定の DFU ランタイム（Arduino の書き込み口）が隣にあるので、dfu-util には `-a Firmware` で選ばせます。
- 送るイメージにも `CH32RVUsbDeviceDfu` を入れておかないと、次の更新は工場 ISP かプローブになります。
- 入れ替え（先頭へのコピー）の間に電源が切れると、スケッチは書きかけになります。工場ブートローダーかプローブで直せます。
- 仕組みと確かめていること: [USB ガイドのファームウェア更新](../../../../docs/usb.ja.md#ファームウェア更新dfu-のダウンロードuf2-のドライブ)。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `CH32RVUsbDeviceDfu dfu` | DFU mode のインターフェースを足します。エンドポイントを使わないので、ほかのクラスと並べられます。 |
| `dfu.onProgress(callback)` | 受け取ったバイト数。USB の処理の中から呼ばれるので、値を残すだけにします。 |
| `dfu.onError(callback)` | 失敗の DFU の状態（`CH32RVUsbDfuStatus`）。 |
| `dfu.onComplete(callback)` | 使っていませんが、確かめ終わったイメージを `false` で断れます。 |
| `dfu.restartWhenComplete(false)` | 再起動をスケッチに任せます。`CH32RV.restart()` か `Update.restart()` で入れ替わります。 |

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)
- [Update ライブラリ](../../../Update/README.ja.md)

[FirmwareDFU.ino](FirmwareDFU.ino)
