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

- [ADR-0001](0001-device-data-repository.ja.md): device databaseを独立repositoryに置く
- [ADR-0002](0002-toolchain-distribution.ja.md): toolchainはxPack riscv-none-elf-gccの直リンク参照
- [ADR-0003](0003-owned-startup-vector-linker.ja.md): owned startup/CRT/vector/linker
- [ADR-0004](0004-runtime-and-cxx.ja.md): newlib-nano defaultとGNU++17
- [ADR-0005](0005-board-structure-and-fqbn.ja.md): family board+pnumメニューとdevice-data生成
- [ADR-0006](0006-rtos-policy.ja.md): コアはベアメタル、RTOSは将来の同梱ライブラリ
- [ADR-0007](0007-user-build-option-injection.ja.md): build.extra_flagsはユーザー注入専用に予約

## 今後のADR候補

- ArduinoCore-APIの固定versionと取込方法(Q-010)
- 内部HAL contract(Q-013)
- upload frontendとprimary backend(Q-040系)
- 公開packager/表示名(Q-017)
- support tierとrelease gate
