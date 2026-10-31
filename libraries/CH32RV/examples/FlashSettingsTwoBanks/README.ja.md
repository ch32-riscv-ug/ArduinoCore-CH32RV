# FlashSettingsTwoBanks

[English](README.md)

いくつかの設定を内蔵のフラッシュに保存し、リセットのあとに読み戻します。2 つのページに交互に書くので、保存の途中で電源が切れても、前に保存した値が残ります。追加部品は不要です。

**上級者向けの例です。** 長く残したいデータは、SD カードや I2C の EEPROM に置くのが推奨です。内蔵のフラッシュの制約は[フラッシュへの保存](../../../../docs/flash-store.ja.md)にまとめています。

## 操作とAPI

操作は [FlashSettings](../FlashSettings/README.ja.md) と同じです。数を送ると保存し、リセットすると読み戻します。

- `CH32RV_FLASH_STORE(store, 2 * CH32RV_FLASH_ERASE_SIZE)` で、消去の単位 2 つ分を確保します。
- 各ページの頭に CRC-16 と世代を置きます。読むときは、CRC が合うページのうち、世代が新しいほうを使います。
- 保存は、古いほうのページに `ch32rv_flash_rewrite_page()` で書きます。書いている途中で切れても、新しいほうのページはそのまま残ります。
- 版（`kVersion`）と大きさの確かめ方、変わっていなければ書かないことは、FlashSettings と同じです。
- 2 つのページを交互に消すので、1 ページあたりの消去の回数は半分になります。

## 費用

`setup()` から読み書きする部分だけで、V003 で約 +670 B、X035 で約 +730 B です。RAM は増えません。確保するフラッシュは FlashSettings の 2 倍です（4 KB 単位の系列では 8 KB、H415/H417 では 16 KB）。系列ごとの値は[フラッシュへの保存](../../../../docs/flash-store.ja.md)にあります。

## 実行前の準備

ボードと正確な Part Number を選びます。ログはハードウェア `Serial` の 115200 baud・8N1 です。アダプタ RX をボード TX、アダプタ TX をボード RX へ接続し、GND を共通にします。

書き込み直しても、保存した中身は残ります（ツール → Erase Flash が既定の Keep Store のとき）。All Flash を選ぶと消えます。生成された `sketch.yaml` にビルド profile があります。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
