# フラッシュを食っているものの見つけ方

CH32V003は**フラッシュ16 KB・RAM 2 KB**です。Arduinoの書き方をそのまま持ち込むと、
1行で半分が消えることがあります。このページは「何が高いのか」と
「map fileを見てどう削るのか」の手順書です。

数字はすべて**このリポジトリでの実測**で、出典を各行に付けてあります。
推測は推測と書きます。

---

## 先に結論: 高いものは3つだけ

| やること | コスト | 出典 |
|---|---:|---|
| `Serial.print(1.5, 2)` など**浮動小数点の印字** | **+9,428 B** | 本稿(2026-08-22、`core_api`/V003) |
| `printf` を**newlib full**でリンク | +43,020 B (`%d`, nano比) | [実験0006](experiments/0006-newlib-size-baseline.ja.md) |
| `printf("%f")` を**nano+`-u _printf_float`**で使う | +20,016 B (nano比) | 同上 |

残りは誤差の範囲です。**C++のvirtualは+32 B**、`new`/`delete`はnanoなら1.2 KB。
「C++は重い」「virtualは避けろ」はこのcoreには当てはまりません。

| case | libc | text |
|---|---|---:|
| 空`main()`(crt0のみ) | − | 360 |
| C++ virtual | nano/full同一 | 392 |
| C++ `new`/`delete` | nano | 1,212 |
| C++ `new`/`delete` | **full** | **114,084** |
| `puts` | nano | 3,084 |
| `printf %d` | nano | 4,932 |
| `printf %d` | **full** | **47,952** |
| `printf %f` | nano(そのまま) | 5,476 (**出力されない**) |
| `printf %f` | nano+`-u _printf_float` | 24,948 |

`--specs=nano.specs`は**既定**です([ADR-0004](adr/0004-c-runtime-and-libc.ja.md))。
`%f`は`menu.printf`で明示的に有効化したときだけ入ります。
fullを引くのはmenuを`full`にしたときだけなので、上の「full」行は
**そうしたらこうなる**という警告として読んでください。

---

## なぜ浮動小数点の印字がそこまで高いのか

`Print::printFloat(double, int)`が`double`を取ります。rv32ec/rv32imacに
**FPUは無い**ので、libgccのsoft-floatが丸ごと付いてきます。

| symbol | bytes | 出どころ |
|---|---:|---|
| `__adddf3` | 2,346 | libgcc |
| `__subdf3` | 2,252 | libgcc |
| `__divdf3` | 1,818 | libgcc |
| `__muldf3` | 1,510 | libgcc |
| `Print::printFloat` | 468 | core |
| `__clz_tab` / `__ltdf2` / `__gtdf2` ほか | 約1,000 | libgcc |

`Serial.print(255, HEX)`や`Serial.print(-42)`は整数経路(`printNumber`、108 B)なので
安いままです。**高いのは小数点だけ**です。

`printFloat`の署名はArduinoCore-API由来で、[ADR-0009](adr/0009-vendored-arduino-core-api.ja.md)により
無改変でvendorしています。**こちらで`float`版に差し替えることはしません**
(`private`かつ非`virtual`なので派生クラスからも触れず、`float`版を足しても
`print(1.5, 2)`の`1.5`は`double`リテラルなので救われない)。

---

## 手順1: どこに効いているかを測る

ビルド成果物は`--build-path`に出ます。ELFとmap fileが両方あります。

```sh
arduino-cli compile --fqbn ch32-riscv-ug:ch32v:CH32V003:pnum=ANY \
    --build-path /tmp/b <sketch>
```

まず全体。`arduino-cli`が出す行と同じものです。

```sh
riscv-none-elf-size /tmp/b/<sketch>.ino.elf
```

次に**大きい順のsymbol一覧**。ここで犯人はだいたい分かります。

```sh
riscv-none-elf-nm --size-sort -S -r --radix=d /tmp/b/<sketch>.ino.elf | head -30
```

`riscv-none-elf-*`は`<repo>/.tools/xpack-riscv-none-elf-gcc/<version>/bin/`にあります。

---

## 手順2: **なぜ**それが入ったのかをmapで見る

`nm`は「何が大きいか」しか言いません。**「誰が呼んだから入ったのか」はmapにあります。**

map fileは`/tmp/b/<sketch>.ino.map`です(platform.txtが`-Wl,-Map`を常に渡しています)。
先頭のほうに

```
Archive member included to satisfy reference by file (symbol)
```

という節があり、ここが答です。

```
libgcc.a(adddf3.o)
      core.a(Print.cpp.o) (__adddf3)
```

「`adddf3.o`は`Print.cpp.o`が`__adddf3`を参照したから入った」と読みます。
`Print.cpp`ということは`Serial.print`のどれかで、`__adddf3`ということは`double`。
これで**sketchのどの行か**まで絞れます。

配置と実サイズは後半の`.text.<symbol>`の並びで見ます。

```
 .text.__adddf3
                0x0000182c      0x92a  .../libgcc.a(adddf3.o)
                0x0000182c                __adddf3
```

`0x92a` = 2,346バイト。

**`Discarded input sections`**の節も見る価値があります。ここに載っているものは
`--gc-sections`が既に捨てたもので、**削っても効きません**。

---

## 手順3: 削る

効く順に並べます。

### 浮動小数点の印字をやめる

一番効きます。整数でスケールすれば9.4 KBが丸ごと消えます。

```cpp
Serial.print(mv / 1000); Serial.print('.');       // 1234 mV を 1.234 と出す
Serial.println(mv % 1000);
```

小数が本当に要るなら、**その部品を別sketchに分ける**か、62 KB以上のboardに限定します。

### `printf`と`Serial.print`を混ぜない

両方使うと両方の経路が入ります。どちらかに寄せてください。
`Serial.print`のほうが安く、`printf`は`%d`だけでも4.9 KBです。

### `String`はRAMを見る

CH32V003のRAMは2 KBです。`String`はヒープを使い、`tests/sketches/basic/heap_string`が
**そのために**残してあります。フラッシュより先にRAMで詰まります。

### `F()`と`PROGMEM`はここでは無意味

CH32はflashもRAMも同じアドレス空間なので、
`cores/arduino/api/deprecated-avr-comp/avr/pgmspace.h`で
`PROGMEM`は空、`PSTR(str)`は`(str)`です。文字列リテラルは**最初から`.rodata`
(=flash)にあり、RAMへはコピーされません**。`F()`を付けても外しても1バイトも変わりません。
AVR向けのコードを移植するときに迷いやすいところなので、明記しておきます。

### 効かないもの

- `-Os`は既定です
- `-ffunction-sections -fdata-sections` + `--gc-sections`も既定です。
  「使っていない関数を消す」手当ては**もう済んでいます**
- virtualを避けても+32 Bしか変わりません

---

## 回帰を見る仕組み

一度削っても、次のcore変更で戻ります。2つ用意してあります。

| | 何を見るか |
|---|---|
| [`tests/compile/sizes_baseline.json`](../tests/compile/sizes_baseline.json) | 全122 part numberのサイズ。変わると`compile/test_compile_matrix.py`が落ちる。意図した変更なら`tests/compile/check_sizes.py --update`で基準線を更新する |
| [`tests/sizebench/`](../tests/sizebench/) | libc変種ごとの代表機能コスト。toolchainを上げたら回す |

sketchが載らなくなったときは、**boardをprofileから外すのではなくcaseを分割**します
(理由と実例は[tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md))。

---

## 検討したが入れていないもの

- **CH32V003で高コスト関数の呼び出しをビルドエラーにするboard設定**。
  `-Wl,--wrap`や`--defsym`で`__adddf3`等を弾けば「知らずに9.4 KB持っていかれる」
  事故は防げるが、**逃げ道の設計(意図的に使いたいときにどう外すか)が要る**。
  2026-08-22時点で**入れない**判断。
- **`float`版`printFloat`の追加**。ArduinoCore-APIの署名を変えることになり、
  かつ`print(1.5, 2)`は`double`リテラルなので効かない。
  やるなら[上流](https://github.com/arduino/ArduinoCore-API)へ。
