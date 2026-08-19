# ADR-0002: default toolchainはxPack riscv-none-elf-gccをGitHub Releases直リンクで参照する

- Status: Proposed
- Date: 2026-08-19
- Related questions: Q-020, Q-026

## Context

全CH32ファミリのISA/ABI(rv32ec/rv32emc+ilp32e、rv32imac/ilp32、rv32imafc/ilp32f、V407系のZve64x/Zvbb)を、Win/Linux/macOSで、自前ビルド・同梱なしに賄えるGCC配布物が必要。Board Managerのtool参照とGPL遵守も満たすこと。

## Decision drivers

- rv32e系multilib(ilp32e)が必須(CH32V003/V00X)
- 5 host(win-x64、linux-x64/arm64、mac-x64/arm64)
- 再配布・改変条件が明確で、対応sourceが入手できる
- compiler forkをdefault前提にしない([toolchain方針](../toolchain.ja.md))

## Options considered

### Espressif riscv32-esp-elf

rv32e/ilp32e multilibがなくCH32V003/V00X系をビルドできない。不採用([R-04](../research/toolchain-distributions.ja.md))。

### riscv-collab nightly

Linux x64のみで不採用。

### WCH MounRiver fork(GCC8/GCC12)

XW拡張・`WCH-Interrupt-fast`を持つが、patchedソースの公開を確認できず、linux-arm64がなく、更新が停滞。**比較lane限定**とする。

### xPack riscv-none-elf-gcc(採用)

rv32e系6種を含むmultilib、5 host、活発な保守、公式ソース無改変+ビルドスクリプト公開。multilib実物・全core向けcompile・C++/FPU/vector拡張・Board Manager経由installまで検証済み(実験0001/0005)。STM32duino公式が同じ直リンク参照方式を実運用している。

## Decision

- default toolchainは**xPack `riscv-none-elf-gcc`**とし、package indexのtoolsは**xpack-dev-toolsのGitHub Releases資産へ直接URL参照**する(再ホストしない)
- 認定候補versionは**14.3.0-1**(Zve64x/Zvbb対応と安定性のバランス)。tool定義の正本は[tools/index/tools_xpack_gcc.json](../../tools/index/tools_xpack_gcc.json)(6 host、公式checksum)
- versionの更新は、認定matrix通過とADR追記を条件にindexへ追加する(過去entryは変更しない)
- WCH fork laneは`ch32-riscv-ug`ミラー参照の比較用として維持し、defaultにしない

## Consequences

- 自前でGPLバイナリをconveyしないため、ソース提供義務を負わない(checksum固定で改竄検知)
- xPack/GitHub側の資産削除・障害が利用者のinstall失敗に直結する(旧資産の長期残存実績はあり)。将来必要なら再ホスト(GPL§6(d)対応)へ切替可能
- アーカイブは1 host約400MB(全multilib同梱)。分割再パッケージはしない(義務発生を避ける)

## Validation

- CI: install-test(3 OS)がindex→tool→platform→compileを常時検証
- [toolchain方針](../toolchain.ja.md)の認定matrix(ch32fun比較によるsize/性能の非劣化確認)を初期release前に通す。閾値未達の場合はversion選定を再検討する(配布物と参照方式の決定は維持)

## References

- [R-04調査](../research/toolchain-distributions.ja.md)、[実験0001](../experiments/0001-xpack-multilib-smoke.ja.md)、[実験0005](../experiments/0005-package-index-install.ja.md)
