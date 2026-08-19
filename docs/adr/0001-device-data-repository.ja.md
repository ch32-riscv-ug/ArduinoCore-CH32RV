# ADR-0001: Device databaseを独立repositoryに置く

- Status: Proposed
- Date: 2026-08-17
- Related questions: Q-011, Q-018

## Context

device schemaとpin/package dataはArduino coreだけでなく、検索viewer、文書生成、他のCH32向けtoolからも利用できます。Arduino core、family別資料mirror、既存`ch32_riscv_tools`のいずれかに正本を置くと、release lifecycleの結合、family間の分散、手製legacy表との混在が起きます。

## Decision drivers

- Arduino固有形式にdataを閉じ込めない
- schema、provenance、validator、data releaseを一体で管理する
- 公式資料mirrorと作成データのlicense・役割を分離する
- 複数consumerが同じversionを再現可能に利用できる
- 旧コア、EVT、手製pin表を無断コピーしない

## Options considered

### ArduinoCore-CH32に置く

初期実装は単純ですが、data releaseがArduino core releaseへ結合し、他consumerが利用しにくくなります。

### Family別資料mirrorに置く

一次資料との近さはありますが、cross-family schemaとvalidatorが分散します。

### ch32_riscv_toolsに置く

既存viewerへつなぎやすい一方、出典・coverageを持たない手製表と正本を混同しやすくなります。

### 独立repositoryに置く

運用対象は1 repository増えますが、data固有のversioningと複数consumerの固定利用が可能です。

## Decision

[`ch32-riscv-ug/ch32-device-data`](https://github.com/ch32-riscv-ug/ch32-device-data)をdevice databaseの正本とします。

`ArduinoCore-CH32`は固定versionのconsumerとし、通常build時にnetwork取得しません。family別repositoryは公式datasheet/RM/EVTのmirror、`ch32_riscv_tools`は将来のviewer・生成物consumerとして扱います。

この決定は保存場所とrepository境界だけを確定します。schema、正規化方式、対象family、Arduino対応SKU、release/lock形式は別途決定します。

## Consequences

- schema、sample record、validator、schema調査、詳細引継ぎを`ch32-device-data`へ移す
- Arduino側にはrepository境界とconsumer方針を残す
- consumer更新にはdata versionとhashの固定が必要になる
- data repository単独のCIとrelease運用が必要になる
- 公式PDF、EVT tree、旧core source、手製pin表は移入しない

## Validation

- `ch32-device-data`単独checkoutでschema validationとfallback validatorが通ること
- 将来のArduino releaseがnetworkなしでbuildできること
- data更新時にconsumerの生成差分と固定versionをreviewできること

## References

- [Device dataの配置と利用方針](../device-data.ja.md)
- `ch32-device-data/docs/handoff.ja.md`
