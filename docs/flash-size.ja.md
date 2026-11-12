# Flash 使用量の調べ方

小容量の CH32 では、標準 library の一機能が sketch 本体より大きくなることがあります。
推測で API を避けるのではなく、ELF と linker map から原因を特定します。

## 大きくなりやすいもの

- `Serial.print(double)` は software floating-point 演算を取り込む
- printf の `%f` と `scanf("%f")` は、Floating Point のメニュー（`rtlib`）で有効にした変換 code を取り込む（[printf と浮動小数](printf-float.ja.md)）
- `printf` と `Serial.print` を併用すると別々の format 実装が残る
- `String` と動的確保は flash より RAM / heap の制約が先に問題になる場合がある

既定（`fltf`）は float で計算し、printf の `%f` を出します。`Serial.print(float)` と printf の `%f` は同じ
fixed-point の処理を使います。soft-float の引き算は足し算で行うので、両方を使っても足し算 1 つ分です。

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
llvm-size -A /tmp/ch32rv-build/<sketch>.ino.elf
```

大きい symbol の順:

```sh
llvm-nm --size-sort -S -r --radix=d \
  /tmp/ch32rv-build/<sketch>.ino.elf | head -30
```

tool は Board Manager が導入した ch32rv-toolchain の `bin/`、または `.tools/` にあります。

## Map file の読み方

リンカーは lld です。map は 1 行 1 項目の表で、列は `VMA LMA Size Align Out In Symbol` です。
section の行（`.text.<関数名>` など）に、置かれた番地、大きさ（16 進）、どの object から来たかが出ます。

```text
     718      718      42e     2         .../libclang_rt.builtins.a(adddf3.c.obj):(.text.__adddf3)
     718      718      42e     1                 __adddf3
```

この例（CH32V003、Floating Point が Double、`analogRead(A1) * 0.25 + 1.5` を `Serial.println`）では、double の足し算
`__adddf3`（0x42e = 1,070 B）が compiler-rt の software floating-point routine から入っています。

どの参照でライブラリの部品が入ったかは、lld の `--why-extract` で書き出せます。

```sh
arduino-cli compile ... --build-property "compiler.c.elf.extra_flags=-Wl,--why-extract=/tmp/why.txt" <sketch>
```

```text
reference                       extracted                                         symbol
.../<sketch>.ino.elf.lto.o      .../libclang_rt.builtins.a(adddf3.c.obj)          __adddf3
.../libclang_rt.builtins.a(muldf3.c.obj)  .../libclang_rt.builtins.a(muldi3.c.obj)  __muldi3
```

1 行目は、LTO の後のスケッチ（`.lto.o`）が double の足し算を呼んだことを示します。2 行目のように、ライブラリの部品が
別の部品を連れてくることもあります。

`--gc-sections` で消えた section は map に出ません。map に無い source を削っても、最終の binary は小さくなりません。

## 削減の優先順位

1. 浮動小数点表示が不要なら、固定小数点の整数として保持・表示する
2. `printf` と `Serial.print` のどちらかへ統一する
3. RAM の小さい型番では、`String` と一時 buffer の heap 使用も確認する
4. API 単位で sketch / library を分け、未使用 code を linker が除去できる境界にする

`-Os`、`-ffunction-sections`、`-fdata-sections`、`--gc-sections` は既定です。追加しても差は出ません。

`F()` と `PROGMEM` は AVR 互換 macro です。CH32 では文字列 literal は最初から flash の `.rodata` にあり、
これらを付けても別の address space へ移動しません。

runtime の選択理由は [ADR-0004](adr/0004-runtime-and-cxx.ja.md) を参照してください。
