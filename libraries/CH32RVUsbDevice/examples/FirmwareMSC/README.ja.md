# FirmwareMSC

English: [README.md](README.md)

`.uf2`（または `.bin`）をコピーすると、そのファームウェアに入れ替わる USB ドライブです。

## 必要なものと配線

USB デバイスのある系列で RAM が 16 KB 以上の板と PC。進み具合は `Serial`（UART）に出ます。
受け取れるイメージは「flash − このスケッチ」までです。

## 操作と期待する結果

1. このスケッチを書き込むと、`FIRMWARE` というドライブが現れ、`README.TXT` があります。
2. 入れ替えるスケッチの `.bin` から UF2 を作ります。family ID は `Serial` に出る値（系列ごと、USB ガイドの表）です:
   `python3 uf2conv.py -c -b 0x08000000 -f <family> <sketch>.ino.bin -o sketch.uf2`
3. `sketch.uf2` をドライブにコピーすると、`received` が増え、全ブロックがそろうと約 0.5 秒後にドライブが消えて、
   新しいスケッチで起動します。
4. ほかの系列の family ID の `.uf2` は、最初のブロックで断られ（`update error 30`）、このスケッチのままです。
5. `.bin` をそのままコピーしてもかまいません。ホストが順に書く限り受け、ファイルの長さに届くか取り出したときに入れ替えます。

## 設定と制約

- `storage`（8 KB）にはブートセクタ、FAT、ルートディレクトリと、ホストが置くファイルだけが入ります。イメージは flash へ書きます。
- 送るイメージにも更新の手段を入れておかないと、次の更新は工場 ISP かプローブになります。
- 仕組み、family ID の表、確かめていること: [USB ガイドのファームウェア更新](../../../../docs/usb.ja.md#ファームウェア更新dfu-のダウンロードuf2-のドライブ)。

## API の使い方と改造のポイント

| API | この例での使い方 |
|---|---|
| `CH32RVUsbDeviceMscFirmwareDisk disk(storage, size)` | ドライブの中身。`begin()` が staging の flash に合わせてボリュームを作ります（staging が無ければ false）。 |
| `disk.addTextFile()` | 最初から見えるファイル。`begin()` と `attach()` の間に呼びます。 |
| `disk.attach(msc)` | `CH32RVUsbDeviceMsc` の中身にします（媒体あり、書き込み可）。 |
| `disk.onProgress()` / `disk.onError()` | 書いたバイト数と失敗（`CH32RV_UPDATE_*`）。USB の処理の中から呼ばれます。 |
| `CH32RVUsbDeviceMscFirmwareDisk::uf2FamilyId()` | `.uf2` に要る family ID。 |

## 関連資料

- [サンプル共通の準備とトラブルシューティング](../README.ja.md)
- [CH32RVUsbDevice の API と対応範囲](../../README.ja.md)

[FirmwareMSC.ino](FirmwareMSC.ino)
