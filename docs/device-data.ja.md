# Device dataの配置と利用方針

文書状態: 決定済みの配置と、未決定のconsumer設計

文書基準日: 2026-08-17

## 配置

機械可読なCH32 device databaseの正本は、独立repository [`ch32-device-data`](https://github.com/ch32-riscv-ug/ch32-device-data)に置きます。このrepositoryに仮置きしていたschema、8 sample record、validator、調査文書は移動済みです。

この決定の背景と境界は[ADR-0001](adr/0001-device-data-repository.ja.md)に記録します。schema、対象family、初期対応SKUまで確定したという意味ではありません。

## Repositoryごとの役割

| Repository | 役割 |
|---|---|
| `ch32-device-data` | schema、検証済みsource data、validator、provenance、data releaseの正本 |
| `ArduinoCore-CH32` | 固定data versionを読むconsumer、Arduino用descriptor・pin table・linker入力等のgeneratorと生成物 |
| `ch32_riscv_tools` | 将来のpin検索viewer、CSV/表等の生成先候補。既存手製表は正本にしない |
| family別datasheet/EVT mirror | 公式資料の取得・履歴・hash対象。構造化dataの正本にはしない |

公式PDF、EVT tree、旧Arduino core source、`ch32_riscv_tools`の手製pin表は`ch32-device-data`へコピーしません。data recordには公式URL、mirror commit/path、file SHA-256、文書version、locatorを保持します。

## Arduino側への渡し方

通常のArduino build中にnetwork取得は行いません。次の流れを候補として検証します。

1. `ch32-device-data`のtagまたはfull commitをlockする
2. archive/tree hash、schema version、入力record hashを検証する
3. Arduino用artifactを生成する
4. generator version、data lock、review可能な生成差分をこのrepositoryへ保存する
5. Board Manager releaseを生成済みartifactだけでoffline build可能にする

release方式、lock file形式、生成物の範囲はまだ未決定です。

## Versioning案

- schema compatibilityはrecord内の`schema_version`で判定する
- data repository releaseはSemVerを候補とする
- consumerはfloating branchではなくtagまたはfull commitとhashを固定する
- source document更新とdata訂正をchangelogで区別する
- `coverage`や`verification`の低下を通常更新として黙って受け入れない

## 次に決めること

- canonical signal IDとvendor表記の分離
- silicon/package/exact SKUの正規化
- pinを持たないinternal routeの表現
- verificationの粒度
- CH32F等のArm系を同じdatabaseへ含めるか
- data releaseとArduino consumer lockの形式

schema作業の詳細は`ch32-device-data/docs/handoff.ja.md`を参照してください。
