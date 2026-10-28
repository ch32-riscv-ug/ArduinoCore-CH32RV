# FlashSettings

[English](README.md)

いくつかの設定を内蔵のフラッシュに保存し、リセットのあとに読み戻します。1 ページを上書きする、いちばん小さい形です。追加部品は不要です。

**上級者向けの例です。** 長く残したいデータは、SD カードや I2C の EEPROM に置くのが推奨です。内蔵のフラッシュの制約は[フラッシュへの保存](../../../../docs/flash-store.ja.md)にまとめています。

## 操作とAPI

端末から数を送ると、`interval` をその値にして保存します。ボードをリセットすると、`loaded interval …` と読み戻した値を表示します。保存がないとき、または保存の形が違うときは `defaults` と表示し、既定値を使います。

- `CH32RV_FLASH_STORE(store, CH32RV_FLASH_ERASE_SIZE)` で、フラッシュの末尾に消去の単位 1 つ分を確保します。スケッチで使わなければ確保されません。
- 読むときは、`store` を普通のメモリとして読みます。
- 書くときは `ch32rv_flash_rewrite_page()` で、1 単位を消して 1 ページ（`CH32RV_FLASH_WRITE_SIZE`）を書きます。
- ページの頭に版（`kVersion`）と大きさを書き、読むときに合うかを確かめます。`Settings` を変えたら `kVersion` を変えます。消したあとの値は系列で違う（0xFF か 0xE339）ので、空かどうかでは見分けません。
- 中身が変わっていなければ書きません。

保存の途中で電源が切れると、設定は失われます（次は既定値で始まります）。前の値を残したいときは [FlashSettingsTwoBanks](../FlashSettingsTwoBanks/README.ja.md) を使います。

## 費用

`setup()` から読み書きする部分だけで、V003 で約 +440 B、X035 で約 +500 B です。RAM は増えません（保存の間だけスタックを 1 ページ分使います）。系列ごとの値は[フラッシュへの保存](../../../../docs/flash-store.ja.md)にあります。

## 実行前の準備

ボードと正確な Part Number を選びます。ログはハードウェア `Serial` の 115200 baud・8N1 です。アダプタ RX をボード TX、アダプタ TX をボード RX へ接続し、GND を共通にします。

書き込み直すと、保存した中身が消えることがあります（経路とツールの版による）。生成された `sketch.yaml` にビルド profile があります。[ビルド規則](../../../../docs/examples-build-rules.ja.md)と[書き込み経路](../../../../docs/upload-routes.ja.md)も参照してください。
