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

## 初期ADR候補

- ArduinoCore-APIの採用と固定方法
- exact SKUを正本とするdevice/board manifest
- owned startup/CRT/vector/linker
- EVT Compatibility Packの分離
- default toolchainとC++ standard
- upload frontendとprimary backend
- fixtureのprobe識別方法
- support tierとrelease gate
