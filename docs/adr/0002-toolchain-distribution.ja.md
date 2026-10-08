# ADR-0002: Compiler は xPack GCC を上流 release から取得する

- Status: Accepted

## 決定

既定 toolchain は xPack `riscv-none-elf-gcc` とし、Board Manager の tool 定義から
xPack の GitHub Releases 資産を checksum 固定で直接参照します。採用 version と host 別の URL、
checksum は `tools/index/tools_xpack_gcc.json` を正本とします。

## 理由

CH32V003 等に必要な RV32E / ILP32E multilib と、Linux、macOS、Windows の配布物を同じ系統で
揃えられるためです。compiler fork を既定にすると、host 対応、更新、source 提供、固有拡張への依存を
この project が継続して負担することになります。

上流資産を改変せず直接参照することで、この repository が巨大な compiler binary の再配布者になることも
避けます。上流の配布方法が利用できなくなった場合だけ、license 条件を満たす別配布を検討します。
