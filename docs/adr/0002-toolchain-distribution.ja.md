# ADR-0002: Compiler は自前でビルドした LLVM（ch32rv-toolchain）を使う

- Status: Accepted

## 決定

toolchain は [ch32rv-toolchain](https://github.com/ch32-riscv-ug/ch32rv-toolchain) の 1 本だけにします。
上流の LLVM の正式リリースから RISC-V だけの clang と lld をこの organization の CI でビルドし、
picolibc、compiler-rt、libc++、GDB を同じアーカイブに入れて、Board Manager の tool として配ります。
採用する版と host 別の URL、checksum は `tools/index/tools_ch32rv_toolchain.json` を正本とします。
GCC は使わず、GCC と clang を両方選べる形も作りません。

LLVM への変更は `patches/` に置いたものだけです（今は WCH の `WCH-Interrupt-fast` 属性）。

## 理由

- **大きさ:** clang の `-Oz` と LTO、WCH の XW（LLVM の Xwchc）で、CH32V003 では GCC（xPack）の `-Os` より
  3〜22% 小さくなりました。上流の GCC は XW を知りません。
- **WCH の機能:** XW と、HPE に合わせた割り込みの入口（`WCH-Interrupt-fast`）が使えます。上流の GCC には
  どちらも無く、WCH 版の GCC は source と host の追跡が難しいためです。
- **配布:** C ライブラリまで入れて host あたり 48〜64 MB（Windows は約 260 MB）で、xPack の GCC（400 MB 超）の
  約 1/8 です。
- **一本化:** コンパイラごとの分岐、試験の 2 本立て、メニューが無くなります。

自前でビルドする負担（host ごとの CI、LLVM の更新、GDB とその依存の source の公開）は、この project が持ちます。
32 bit の Windows は扱いません。
