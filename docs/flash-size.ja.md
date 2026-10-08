# Flash 使用量の調べ方

小容量の CH32 では、標準 library の一機能が sketch 本体より大きくなることがあります。
推測で API を避けるのではなく、ELF と linker map から原因を特定します。

## 大きくなりやすいもの

- `Serial.print(double)` は software floating-point 演算を取り込む
- `printf("%f")` / `scanf("%f")` は `rtlib` menu で有効にした変換 code を取り込む
- `printf` と `Serial.print` を併用すると別々の format 実装が残る
- `String` と動的確保は flash より RAM / heap の制約が先に問題になる場合がある

既定は newlib-nano で、stdio の浮動小数点変換は無効です。`Serial.print(float)` は newlib stdio とは
別実装なので、`rtlib` menu に依存しません。

## 成果物を残して build する

```sh
arduino-cli compile \
  --fqbn ch32-riscv-ug:ch32rv:CH32V003:pnum=ANY \
  --build-path /tmp/ch32rv-build \
  <sketch>
```

`platform.txt` は ELF、map、hex、bin を `--build-path` に出します。

全 section の概要:

```sh
riscv-none-elf-size -A /tmp/ch32rv-build/<sketch>.ino.elf
```

大きい symbol の順:

```sh
riscv-none-elf-nm --size-sort -S -r --radix=d \
  /tmp/ch32rv-build/<sketch>.ino.elf | head -30
```

tool は Board Manager が導入した xPack GCC の `bin/`、または `.tools/` にあります。

## Map file の読み方

map 冒頭の `Archive member included to satisfy reference by file (symbol)` は、archive 内の object が
どの参照によって取り込まれたかを示します。

```text
libgcc.a(adddf3.o)
      core.a(Print.cpp.o) (__adddf3)
```

この例では `Print.cpp` の double 加算が `libgcc` の software floating-point routine を取り込んでいます。
後半の `.text.<symbol>` で配置 address と実 size を確認できます。

`Discarded input sections` にある section は `--gc-sections` が既に除去しています。そこに出る source を
削っても最終 binary は小さくなりません。

## 削減の優先順位

1. 浮動小数点表示が不要なら、固定小数点の整数として保持・表示する
2. `printf` と `Serial.print` のどちらかへ統一する
3. RAM の小さい型番では、`String` と一時 buffer の heap 使用も確認する
4. API 単位で sketch / library を分け、未使用 code を linker が除去できる境界にする

`-Os`、`-ffunction-sections`、`-fdata-sections`、`--gc-sections` は既定です。追加しても差は出ません。

`F()` と `PROGMEM` は AVR 互換 macro です。CH32 では文字列 literal は最初から flash の `.rodata` にあり、
これらを付けても別の address space へ移動しません。

runtime の選択理由は [ADR-0004](adr/0004-runtime-and-cxx.ja.md) を参照してください。
