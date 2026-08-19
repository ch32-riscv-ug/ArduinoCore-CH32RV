# W-4 prototype: device-data → boards.txt/ld generator

状態: proof of concept(2026-08-19)。リリース対象ではありません。
関連: [環境整備計画](../../docs/infrastructure.ja.md) W-4、[R-03](../../docs/research/board-variants-and-menus.ja.md)、[実験0004](../../docs/experiments/0004-boards-generator-poc.ja.md)

## 目的

`ch32-device-data/tables`(正規化CSV)から、prototype platformの`boards.txt`とSKU別linker scriptを機械生成する。R-03の設計(family単位board+pnumメニューに全型番、生成物はcommitしてCIで再生成一致を検証)の最小実装。

## 使い方

```sh
# 生成(生成物はcommit対象)
python3 generate.py --tables /path/to/ch32-device-data/tables \
                    --platform ../platform/ch32v

# CI用: commit済み生成物と再生成結果の一致検証(drift検出でexit 1)
python3 generate.py --tables ... --platform ... --check
```

## 生成規則

- boardは家族単位(`FAMILY_CONFIG`)。pnumメニューに該当familyの**全型番**を列挙
- 並び順は`series_order`(V002→V004→…→M007)+型番昇順で決定的。FQBNの既定(先頭項目)が再生成で変わらない
- pnum項目が`build.board`/`build.series`(vendorデバイス選択マクロ。V007/M007→`CH32V007_M007`)/`build.ldscript`/`upload.maximum_*`を注入
- ldはユニークな(FLASH, SRAM)組合せごとに1本(`ch32v00x_62k_8k.ld`等)。MEMORYのみ持ち、共通`sections.ld`をINCLUDE
- 生成物ヘッダに`DO NOT EDIT`と**source tablesのgit commit**を記録(タイムスタンプは入れず再生成をidempotentに保つ)

## 現在の範囲と今後

- 対象はCH32V006 family(26 SKU)のみ。他familyは`FAMILY_CONFIG`への追加+等価性検証済みのstartup defines(実験0002)で拡張する
- march/mabi・CSR初期値は当面generator内の設定表。将来はcores.csv/series.csvやdevice-data側への移管を検討(判断ポイント)
- variant(pins_arduino.h)の生成は未実装。pins.csv/pin_functions.csvからのpin map生成はArduinoピン設計の合意後
- package index生成(W-5)は未実装
