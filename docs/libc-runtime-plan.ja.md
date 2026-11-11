# libc と実行時ライブラリの計画（2026-11-11）

clang への移行の段 2（GCC を外す）に向けて、C ライブラリ、実行時ライブラリ（libgcc に当たるもの）、C++ のヘッダーをどうするかの下調べと計画です。コアが自前で持っている printf などの最適化も含めます。結論はまだ出しません。候補を同じ物差しで測ってから選びます。clang そのものの評価は docs/clang-evaluation.ja.md にあります。

## 今の形（段 1）

GCC でも Clang でも、次のものを xPack GNU RISC-V Embedded GCC 15.2.0 から使っています。

- newlib-nano（`libc_nano.a`）と `libm.a`
- libgcc（整数の割り算、soft-float、soft-double など）
- libstdc++ のヘッダー（`libstdc++.a` と `libsupc++.a` はリンクしていない。下の実測で 0 件）

コアは、大きさに効くところをすでに自前で持っています。

| 部品 | 場所 | 置き換えたもの |
|---|---|---|
| printf の系統（printf、sprintf、snprintf、vprintf など）、puts、putchar | ch32rv_format.c、ch32rv_stdio_*.c | newlib の stdio |
| `%f` の書式化（Float では float のビットを整数で計算） | ch32rv_format.c | newlib の浮動小数の書式化 |
| strftime | ch32rv_strftime.c | newlib |
| memmove、memset、文字列の関数（RV32E 以外） | ch32rv_string.c など | newlib |
| float と double の引き算、float から double への変換、`__clzsi2` | ch32rv_softfloat.c | libgcc |
| `_sbrk`、`_write` などのシステムコール | コア | libgloss |

以前 picolibc を見送ったのは、picolibc の大きな利点の tinystdio（小さい printf）が、自前の printf があるコアでは効かないからでした。この判断は今も同じです。ただ、段 2 で GCC を外すと、printf 以外のもの（malloc、strtol、ctype、libm、scanf、libgcc の役目）の出どころが要ります。picolibc は、その候補の 1 つとして測り直します。

## 実測: 例が何をリンクしているか

例の全部（V003 63 本、X035 61 本、V307 60 本、H417 6 本、GCC、LTO）のマップファイルから、ライブラリのどのオブジェクトが入ったかを数えました。数字は text と data の合計です。

| | V003 | X035 | V307 | H417 |
|---|---|---|---|---|
| libc_nano を使う例 | 61 / 63 | 54 / 61 | 53 / 60 | 3 / 6 |
| その中央値 | 18 B（strlen だけ） | 18 B | 18 B | 676 B（malloc） |
| その最大 | 1,135 B（SerialEcho） | 1,185 B（PrintFormatting） | 1,185 B | 684 B |
| libgcc を使う例 | 61 / 63 | 21 / 61 | 26 / 60 | 6 / 6 |
| その中央値 | 126 B（割り算） | 364 B | 1,056 B | 1,056 B |
| その最大 | 2,224 B（AnalogRead） | 1,900 B | 2,230 B | 2,190 B |
| libm を使う例 | 0 | 0 | 0 | 0 |

- libc から入るのはほとんど strlen です。大きいのは malloc の一式で、`mallocr`、`freer`、`reallocr`、`sbrkr`、`lock`、`impure` などを合わせて約 700 B です。そのうち `impure` の 80 B は RAM の data なので、2 KB の V003 では効きます。
- libgcc から入るもの:
  - `div.o`（126 B）: 32 bit の割り算。乗除算のない V003 では、ほとんどの例に入ります。
  - `_ctzsi2.o`（108 B）と、それが使う `_clz.o` の 256 B の表: 呼んでいるのはコアの `ch32rv_exti_dispatch`（`__builtin_ctz`）。ピン割り込みを使う X035 と V307 の例に入ります。コアの `__clzsi2` は libgcc の soft-float 用で、この経路には効いていません。
  - `truncdfsf2.o`（1,056 B）: 呼んでいるのはコアの `HardwareSerial::printf`。可変引数で double になった値を、Float の書式化のために float へ戻しています。V307 と H417 で printf を使う例のすべてに入ります。
  - `_udivdi3.o`（878〜1,202 B）: 64 bit の割り算。AdcStream、LogicCapture、TimerInput、USB オーディオのライブラリが、レートの計算で使っています。
  - soft-float と soft-double の四則演算: 浮動小数の足し算・引き算・掛け算と `Serial.print` を使う V003 のスケッチ（FloatMix）で、`__addsf3` が 1,340 B、`__adddf3` が 2,288 B。スケッチ全体は 9.7 KB です。
- Clang（ビットコードだけの LTO、lld）でも、コアの置き換え（引き算、`__extendsfdf2`、`__clzsi2`）が libgcc より先に選ばれていました（FloatMix、V003。`__subsf3` が 10 B、`__subdf3` が 10 B）。lld は、実行時ライブラリの関数（`__subsf3` など）を定義しているビットコードのアーカイブメンバーを、LTO の前に取り出します（lld/ELF/Driver.cpp の `handleLibcall`）。そのため、GCC の fat オブジェクトに頼らなくても同じ結果になります。

## 候補

### C ライブラリ

| | 今（借りる） | picolibc | newlib を clang で作る | LLVM libc | コアが持つ |
|---|---|---|---|---|---|
| ライセンス | newlib（いろいろ） | BSD 系 | newlib（いろいろ） | Apache-2.0 WITH LLVM-exception | MIT（コアと同じ） |
| rv32e / ilp32e | あり | あり（Zephyr SDK が clang で rv32e、rv32emc をビルドして QEMU で試験） | GCC の multilib にある。clang では未確認 | 見つからない。RISC-V はビルドだけの CI | 作り次第 |
| clang でのビルド | — | 1.4.7 から対応、meson | Arm の ATfE が clang で作って配っている | clang だけ | — |
| malloc | nano-malloc（約 700 B） | 内蔵（大きさは非公表） | nano-malloc | freelist_heap | 小さいものを書く |
| libm | newlib | float 版あり（libc に統合） | newlib | 517 の関数。双曲線関数は最近まで無かった | 持たない（どこかから借りる） |
| scanf の `%f` | newlib（約 24 KB） | tinystdio | newlib | 浮動小数は既定で無効 | 書く必要がある |
| 使っている所 | 今のコア | Zephyr（唯一の libc）、ESP-IDF 6（既定）、ATfE | ATfE（追加の sysroot） | ATfE 24 で既定になる予定 | ch32fun（malloc と math は持たない） |

### 実行時ライブラリ

| | libgcc（借りる） | compiler-rt builtins | コアが持つ範囲を広げる |
|---|---|---|---|
| ilp32e | あり | `save.S` と `restore.S` に ilp32e の経路がある。Zephyr と ESP-IDF の clang が使っている | — |
| 大きさ | 基準 | RISC-V の比較の数字は公表されていない。丸めのモードや例外フラグを扱わない分、soft-float は小さい可能性がある | コアの引き算などと同じやり方で、使うものだけを小さくする |
| GCC への依存 | 残る | 無くなる | 無くなる（全部を持てば） |

### C++

例は `libstdc++.a` と `libsupc++.a` をリンクしていません（実測で 0 件）。要るのはヘッダーだけです。

| | libstdc++ のヘッダー | libc++ のヘッダー |
|---|---|---|
| 今の状態 | GCC から借りている | — |
| ライブラリ本体 | 要らない（今の例では） | `std::string` は extern template、`std::function` の `bad_function_call` は `src/functional.cpp` にあるので、libc++.a か、その部分を外す設定が要る |
| GCC を外すとき | ヘッダーだけを自分の道具に入れて配る（GPLv3 + 実行時の例外） | 例外と RTTI を外した libc++ を作る（libc++ の CI に picolibc との組み合わせがある） |

## 自前の最適化（どの libc を選んでも効くもの）

コアの中で直せて、GCC と Clang の両方に効くものです。大きさは上の実測から見込んだ値で、直したあとに測り直します。

| 項目 | 内容 | 見込み |
|---|---|---|
| A. printf の float の取り出し | Float の書式化で、可変引数の double を `__truncdfsf2` で float に戻している。double のビットから、float に丸めた値を整数の計算で直接作れば、libgcc の 1,056 B が要らない。丸めの結果は libgcc と同じにし、全部の float と、境目の double で照合する | V307 / H417 で printf を使う例の全部で −1 KB 前後 |
| B. `__ctzsi2` をコアで持つ | `ch32rv_exti_dispatch` の `__builtin_ctz` で、libgcc の `_ctzsi2` と 256 B の表が入る。表のない小さい実装（Zbb のある H417 では命令 1 つ）にする | ピン割り込みを使う例で −300 B 前後 |
| C. malloc | nano-malloc と `impure`（RAM 80 B）で約 700 B。コアが小さい割り当て器を持つか、libc の候補のものを比べる。String が malloc を使うので、String を使う例で効く | 測ってから決める |
| D. 32 bit の割り算（V003） | `div.o` の 126 B。compiler-rt のものと大きさ・速さを比べる | 小 |
| E. 64 bit の割り算 | ライブラリのレートの計算で `_udivdi3` が 0.9〜1.2 KB 入る。割る数が定数か 2 のべき乗にできるところは、ライブラリ側で避ける | 該当する例で −0.9 KB 前後 |
| F. soft-float の四則演算 | V003 で浮動小数を使うと、足し算だけで 1.3 KB（float）、2.3 KB（double）。compiler-rt と比べ、小さい方を使う。結果がビット単位で同じかを、今の引き算と同じやり方で照合する | 測ってから決める |
| G. newlib の printf / scanf を使うメニュー | Floating Point の `nanofp`、`nanofs`、`dblfs`、`fltfs`、`nanofps` は newlib の printf / scanf に頼っている（printf で約 21 KB、scanf で約 24 KB）。段 2 で newlib を外すなら、コアで `%e` / `%g` と scanf の `%f` を持つか、選んだ libc のもの（picolibc の tinystdio など）にする | 段 2 では必須 |
| H. 書式化そのもの | コアの printf の大きさを GCC と Clang で測り直し、`-Oz` で増えるところや重複を探す | 測ってから決める |

## 進め方

0. **コアの libc の内部への依存をなくす。** 3 の結果から、tzset、localtime、mktime、strftime の `%Z` をコアの状態で動かし、`putchar` の `#undef`、`CLOCKS_PER_SEC`、システムコールの名前の違い（`sbrk`、`gettimeofday`）を吸収する。newlib のままでも結果を変えない。
1. **compiler-rt の builtins を測る。** rv32ec/ilp32e、rv32imac/ilp32、rv32imafc/ilp32f 向けに、手元の clang で compiler-rt の builtins を作ります。関数ごとの大きさを libgcc と比べ（`__addsf3`、`__mulsf3`、`__divsf3`、`__adddf3`、`__udivsi3`、`__udivdi3` など）、例を libgcc の代わりにこれでリンクして比べます。速さも H417 と V003 で測ります。
2. **自前の最適化 A と B。** libc の選択と関係なく効き、GCC にも効くので先にやります。照合の試験は、今の soft-float と同じ形で書きます。
3. **picolibc を試しに作る。** 3 つの multilib 向けに、clang でビルドします。設定は single-thread、tinystdio、libm 入りです。例を newlib-nano の代わりに picolibc でリンクし、flash、RAM（`impure` や TLS の分も）、malloc の大きさを比べます。コアの printf と文字列の関数が、picolibc のものより先に選ばれることも確かめます。
4. **newlib を clang で作ったものと LLVM libc を、同じ物差しで測る。** LLVM libc は、ilp32e でビルドできるかどうかから確かめます。
5. **C++ のヘッダー。** libstdc++ のヘッダーのままと、例外と RTTI を外した libc++ で全部の例をビルドし、大きさとビルドが通るかを比べます。
6. **メニューの G。** 1〜4 の結果を見て、コアで書くか libc のものを使うかを決めます。
7. **選んで配る形を決める。** clang の BareMetal ドライバーが読む `lib/clang-runtimes/riscv32-unknown-elf/multilib.yaml` の形で、ch32rv-clang に入れるか、ホストによらない別の道具（ClangBuiltArduino が libc を分けているのと同じ形）にするかを決めます。

各段で、V003、X035、V307、H417 の例の flash と RAM を表にして、この文書に足していきます。

## 結果

### 1. compiler-rt の builtins（2026-11-11）

LLVM 23.1.5 の compiler-rt/lib/builtins を、手元の clang で rv32ec/ilp32e、rv32imac/ilp32、rv32imafc/ilp32f 向けに `-Oz` でビルドしました（156 ファイル。失敗した 16 は bf16、tf、stdint の型が要るもので、使わない）。

関数ごとの大きさ（rv32ec/ilp32e、バイト）:

| 関数 | libgcc | compiler-rt |
|---|---|---|
| `__addsf3` | 1,352 | 722 |
| `__mulsf3` | 886 | 584 |
| `__divsf3` | 942 | 802 |
| `__adddf3` | 2,300 | 1,148 |
| `__muldf3` | 1,686 | 950 |
| `__divdf3` | 1,924 | 1,382 |
| `__udivdi3` | 1,370 | 506（`__udivmoddi4` を呼ぶ） |
| `__udivsi3` | 44（アセンブリ） | 296（C） |
| `__umodsi3` | 8 | 290 |
| `__truncdfsf2` | 344 | 332 |

例を Clang で、libgcc の代わりに compiler-rt を先に置いてリンクした差（compiler-rt − libgcc、text）:

| | 組 | 中央値 | 小さくなった | 大きくなった | 最小 | 最大 |
|---|---|---|---|---|---|---|
| V003 | 63 | +168 | 4 | 56 | −796（PulseMeasure） | +516（ServoPulseControl） |
| X035 | 106 | 0 | 7 | 1 | −692（PulseMeasure） | +4 |
| V307 | 108 | 0 | 34 | 1 | −832（CardInfo） | +4 |
| H417 | 6 | −144 | 6 | 0 | −832（LogicBurst） | −144 |

- 乗除算のない V003 では、32 bit の割り算が C で書かれている分だけ大きくなります。soft-float と 64 bit の割り算では小さくなります。
- data と bss は、どの例でも変わりませんでした。
- compiler-rt を先に置いても libgcc から取られたのは、V003 の `multi3.o`（7 本）だけでした。
- 見込み: compiler-rt に、32 bit の割り算（と乗算）の小さいアセンブリを組み合わせれば、V003 でも libgcc 以下になります。アセンブリは、libgcc（GPL + 実行時の例外）を写さずに書きます。soft-float の結果が libgcc とビット単位で同じかは、まだ照合していません（NaN の中身が違う可能性がある）。

### 2. 自前の最適化 A と B（2026-11-11）

`cores/arduino/ch32rv_softfloat.c` に `__truncdfsf2` と `__ctzsi2` を足しました。照合はホストで行いました。`__ctzsi2` は 0 以外の値をホストの ctz と、`__truncdfsf2` は丸めの境目の下位の語を付けた double をホストの `(float)` と、ビット単位で比べています。わざと丸めを壊したコピーでは、試験が失敗することも確かめました。

| 例 | GCC 前 → 後 | Clang 前 → 後 |
|---|---|---|
| H417 HsAdcRead（A） | 9,632 → 8,760（−872） | 8,872 → 8,044（−828） |
| X035 PinInterrupt（B） | 5,300 → 4,988（−312） | 4,004 → 4,004（clang は ctz を呼び出しにしない） |
| V307 PrintFormatting | 変わらない | 変わらない |
| V003 FloatMix | 変わらない | 変わらない |

- NaN の扱い（符号と上位の中身を残し、quiet にする）が libgcc と同じかは、実機で確かめていません。
- 最初の版は `-0.0f` を浮動小数の定数として置き、V307 で `.data` が 4 B 増えました。整数のまま返すように直しました。小さい定数（`.srodata`）は、リンカースクリプトで RAM の `.data` に置いています（gp から近い番地にして、短い命令で読むため）。

### 3. picolibc を試しに作る（2026-11-11）

picolibc 1.8.12 を、手元の clang で rv32ec/ilp32e、rv32imac/ilp32、rv32imafc/ilp32f 向けにビルドしました（meson。single-thread、TLS なし、`atomic-ungetc` なし（rv32ec に A 拡張がない）、semihost なし、`-Oz`）。

**コアが newlib の内部に頼っているところ。** picolibc のヘッダーでコアを通すと、エラーは 11 件で、原因は次の 3 種類でした。C++ のファイルはすべてそのまま通りました。

| 箇所 | newlib に頼っているもの | picolibc では |
|---|---|---|
| ch32rv_tzset.c、ch32rv_localtime.c、ch32rv_strftime.c | `sys/_tz_structs.h`、`__gettzinfo()`、`_tzset_unlocked()`、`__tzcalc_limits()`、`__tz_lock()`、`__month_lengths`、`_daylight`、`_timezone`、`_tzname` | 同じ名前の多くは内部の local.h にだけあり、`__gettzinfo` などはない。`tzname` は `const` |
| ch32rv_stdio_out.c | `putchar` を関数として定義 | `putchar` はマクロ（`#undef` が要る） |
| syscalls.c | `CLOCKS_PER_SEC` | 定義されていない |
| ch32rv_sbrk.c、syscalls.c | `_sbrk`、`_gettimeofday`（newlib のシステムコールの名前） | `sbrk`、`gettimeofday` を呼ぶ |

newlib 以外の libc に移るには、タイムゾーンの状態（規則、`daylight`、`timezone`、`tzname`）と、それを使う関数（tzset、localtime、mktime、strftime の `%Z`）をコアが自分で持つ必要があります。コアはすでに tzset の読み取りと gmtime を持っているので、残りは状態の置き場と localtime、mktime です。この作業は newlib のままでも害がなく、どの libc にしても要るので、次の段で行います。

**大きさ（仮の比較）。** スクラッチに置いたコアのコピーで、上の 3 つを仮に避けました（タイムゾーンのファイルを空にし、`sbrk` と `gettimeofday` を別名で足した）。そのうえで、Clang と libgcc の組み合わせのまま、libc だけを newlib-nano から picolibc に替えて全部の例をリンクしました（picolibc − newlib-nano）。

| | 例 | text の中央値 | text の最小 | text の最大 | data | bss |
|---|---|---|---|---|---|---|
| V003 | 63 | 0 | −316（ButtonBlinkConsole） | 0 | 0 〜 −84 | +4 〜 −352 |
| X035 | 106 | 0 | −332（ButtonBlinkConsole） | +76（PrintFormatting） | 0 〜 −84 | +4 〜 −352 |
| V307 | 114 | 0 | −512（HttpGet） | +7,676（RamDisk など 3 本） | 0 〜 −152 | +160 〜 −352 |

- malloc を使う例（String、Network など）で、text が 250〜510 B、RAM の data が 84 B（newlib の `impure`）小さくなります。SystemInfo などでは、bss も 352 B 小さくなりました（newlib の stdio の FILE）。
- V307 のファイルシステムの例 3 本が 7.6 KB 大きくなりました。主な理由は 2 つです。1 つは、タイムゾーンのファイルを空にしたので picolibc の tzset が入り、それが sscanf と vfscanf を引き込んだこと（コアがタイムゾーンを持てば消える）。もう 1 つは、picolibc の strtod（atof から）が ryu の表を使うことで、こちらは別に測ります。
- NetworkTime は、タイムゾーンを外したのでリンクできませんでした（想定どおり）。

### ほかに見つかったこと

- Clang で V307 の Network の例 7 本が、lwIP の `"%"U16_F` でコンパイルエラーになっていました。原因は、arduino-cli がライブラリを探すときに .c のファイルも C++ のコマンドに通すことです。g++ は警告を出してマクロを展開し、clang++ はエラーで止まります。Clang の C++ のフラグに `-Wno-reserved-user-defined-literal` を足して、7 本とも通るようにしました（platform.txt）。

## GCC を外すときに残るもの

- objcopy と size: llvm-objcopy と llvm-size が ch32rv-clang に入っているので、置き換えられます。
- デバッガ: `debug.toolchain` は xPack の gdb を使っています。GCC を外すなら、gdb をどこから配るかを別に決める必要があります。

## 測ってからでないと答えられないこと

1. 同じスケッチの flash と RAM を、libc の候補ごと、libgcc と compiler-rt の組み合わせごとに測った値。
2. compiler-rt の soft-float と割り算の、rv32ec での大きさと速さ（libgcc との差）。
3. LLVM libc と libc++ が ilp32e でビルドでき、試験が通るか。
4. 2 KB の RAM での malloc の大きさ: picolibc、newlib-nano、小さい割り当て器。
5. libm の float の関数（sinf、expf、powf）の精度と大きさ。
6. libc++（例外と RTTI なし）が libstdc++ のヘッダーに比べて何を足すか。
7. XW を付けて作った libc は効くか、それとも素の rv32ec のものを共有すれば足りるか。
8. multilib の全部（4〜6 通り × libc、compiler-rt、C++）を入れた道具の大きさ。
