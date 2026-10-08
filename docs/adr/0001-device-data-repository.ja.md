# ADR-0001: Device data の正本を独立 repository に置く

- Status: Accepted

## 決定

デバイス、型番、package、memory、pin、route、割り込み等の正本は
[`ch32-device-data`](https://github.com/ch32-riscv-ug/ch32-device-data) に置きます。
ArduinoCore-CH32RV は固定 revision の consumer とし、生成物と lock file だけを保持します。

## 理由

同じデータを uploader、probe、viewer 等でも使うため、Arduino 固有形式を正本にすると consumer ごとの
重複と不一致が生じます。schema、provenance、validator を独立して管理し、各 consumer が同じ revision を
固定する形なら、用途と release cycle が異なっても事実を共有できます。

## 保守上の契約

- 通常ビルド時に device-data を取得しない
- 採用 commit と manifest hash を `vendor/ch32-device-data.lock.toml` に記録する
- 手書き例外を増やさず、再利用可能な修正は上流へ入れて再生成する

詳細は [device-data.ja.md](../device-data.ja.md) を参照してください。
