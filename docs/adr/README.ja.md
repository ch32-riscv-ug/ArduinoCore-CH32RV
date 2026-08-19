# Architecture Decision Records (ADR)

長期的な設計判断を、結論だけでなく背景と代替案を含めて記録します。

## 命名

```text
0001-short-title.ja.md
0002-next-decision.ja.md
```

初期の日本語ADRは`.ja.md`とし、利用者向けに英語版が必要になった場合は同じ番号とslugの`.md`を追加します。番号は再利用しません。

## Status

- `Proposed`: 議論または実験中
- `Accepted`: 採用済み
- `Rejected`: 検討したが採用しない
- `Superseded by ADR-NNNN`: 後の判断で置換された
- `Deprecated`: 新規利用を止めた

### 承認プロセス

`Accepted`はmaintainerが明示的に承認したものだけに付けます。調査・実験の結果として書かれた段階では`Proposed`のままにします。

2026-08-19に、ADR-0001〜0008を`Accepted`から`Proposed`へ戻しました。これらは事前調査フェーズの成果として書かれ、maintainerの明示承認を経ていなかったためです。prototype/CIで実測裏付けがあるもの(0002/0003/0004/0005)も、実測は「その選択肢が成立する」ことの証明であって採用の承認ではないため、同じ扱いにしています。

## Template

```markdown
# ADR-NNNN: Title

- Status: Proposed
- Date: YYYY-MM-DD
- Related questions: Q-NNN

## Context

何を決める必要があり、どの制約があるか。

## Decision drivers

- 判断基準

## Options considered

### Option A

利点、欠点、検証結果。

### Option B

利点、欠点、検証結果。

## Decision

選択した案と適用範囲。

## Consequences

得られるもの、失うもの、移行作業。

## Validation

この判断を継続的に確認するtestまたはmetric。

## References

- 一次資料、issue、実験artifact
```

## ADR一覧

いずれもmaintainer承認前(`Proposed`)です。

| ADR | 内容 | Status |
|---|---|---|
| [ADR-0001](0001-device-data-repository.ja.md) | device databaseを独立repositoryに置く | Proposed |
| [ADR-0002](0002-toolchain-distribution.ja.md) | toolchainはxPack riscv-none-elf-gccの直リンク参照 | Proposed |
| [ADR-0003](0003-owned-startup-vector-linker.ja.md) | owned startup/CRT/vector/linker | Proposed |
| [ADR-0004](0004-runtime-and-cxx.ja.md) | newlib-nano defaultとGNU++17 | Proposed |
| [ADR-0005](0005-board-structure-and-fqbn.ja.md) | family board+pnumメニューとdevice-data生成 | Proposed |
| [ADR-0006](0006-rtos-policy.ja.md) | コアはベアメタル、RTOSは将来の同梱ライブラリ | Proposed |
| [ADR-0007](0007-user-build-option-injection.ja.md) | build.extra_flagsはユーザー注入専用に予約 | Proposed |
| [ADR-0008](0008-upload-strategy.ja.md) | 書き込みdefaultはWCH-LinkE、経路カバレッジは段階追加 | Proposed |
| [ADR-0009](0009-arduinocore-api-import.ja.md) | ArduinoCore-APIを固定versionのvendored snapshotで取り込む | Proposed |
| [ADR-0010](0010-pin-numbering.ja.md) | ピン番号はポート埋め込み(`PA0`形式)、連番を採らない | Proposed |

## 今後のADR候補

- 内部HAL contract(Q-013)
- upload frontend実装とbackend実機認定(Q-040系。方針はADR-0008)
- 公開packager/表示名(Q-017)
- support tierとrelease gate
