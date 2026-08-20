# 実験0014: libglossのsemihosting stubがheapとprintfを黙って壊していた

日付: 2026-08-20
board: CH32X035C8T6 + WCH-LinkE(probe FC928F068181)
関連: [ADR-0003](../adr/0003-startup-and-linker.ja.md)、[テスト計画](../../tests/TEST_PLAN.ja.md)

## 症状

グローバルに`String`を持つsketchが**何も出力しない**。
`setup()`の1行目、`String`に触る前の`Serial.println()`すら出ない。

```cpp
String g;
void setup(){ Serial.begin(115200); Serial.println("A: before String"); g = "xyz"; ... }
```

出力: `''`

`Serial.println()`だけのsketchは同じboardでpassする。

## 原因1: libglossのsemihosting `_sbrk`

`cores/arduino/syscalls.c`は`core.a`の中にあり、**アーカイブのメンバは
「まだ未定義のシンボルを定義しているとき」しか取り込まれない**。
素のsketchは`_sbrk`を参照しないので、`core.a`を走査する時点で`_sbrk`は未定義ですらない。
そのあとlibcの`_sbrk_r`が`_sbrk`を要求し、そこで初めて解決されるが、
リンク順は`core.a` → `-lc` → `-lgloss`なので**libglossのものが勝つ**。

libglossのそれはsemihosting実装だった。

```asm
00001a94 <_sbrk>:
    1aa2:  li      a7,214          # SYS_brk
    1aa8:  ecall
```

CH32にはenvironment-call handlerが無く、`mtvec`はvector table先頭 = リセットベクタ。
つまり**malloc → ecall → リセット**。1 msも経たずに再起動するので、
TXリングが吐き出される前に落ち、出力がまったく残らない。

`.map`にもはっきり出ていた。

```text
[!provide]   PROVIDE (_end = _ebss)      ← 誰も_endを参照していない = 我々の_sbrkが未リンク
20000570 b   heap_end.0                  ← libgloss側のstatic
```

## 原因2: printf bridgeが`return 0`にコンパイルされていた

原因1を直すと`String`は動いたが、`printf()`が`-1`を返し何も出ない。
`_write`を直接呼ぶと**0**が返る。

`HardwareSerial.cpp`は自分のヘッダを`Arduino.h`より先にincludeしている。
`SERIAL_PORT_MONITOR`は`HardwareSerial.h`が`CH32_SERIAL_DEFAULT`から定義するが、
その値はvariantの`pins_arduino.h`から来る。そして`pins_arduino.h`を読むのは
`Arduino.h`だけだった。

結果、**このtranslation unitだけ**`SERIAL_PORT_MONITOR`が未定義になり、

```cpp
extern "C" size_t ch32_serial_write_bytes(...)
{
#ifdef SERIAL_PORT_MONITOR
    ...
#else
    return 0;      // ← ここが選ばれていた
#endif
}
```

`.map`のセクションサイズ4バイト(`li a0,0; ret`)がそのまま証拠になっていた。

## 対処

| 原因 | 対処 | 場所 |
|---|---|---|
| 1 | `core.a`と`-lc`を`--start-group`/`--end-group`で囲み、libcが`_sbrk`/`_write`を要求したときに`core.a`を再走査させる | `platform.txt`の`recipe.c.combine.pattern` |
| 1 | `_sbrk`を`ch32_sbrk.c`へ分離 | `cores/arduino/ch32_sbrk.c`(新規) |
| 2 | `HardwareSerial.h`が`pins_arduino.h`を自分でinclude | `cores/arduino/HardwareSerial.h` |

`_sbrk`を分離したのはサイズのためです。最初は`-Wl,--require-defined=_sbrk`で
`syscalls.o`を強制的に引き込んだが、同じobjectにある`_write`が`HardwareSerial`を
連れてくるため、**heapを使わないBlinkですら932 → 2672バイト**に膨らんだ。
`_sbrk`だけを別objectにすれば、`new`だけ使うsketchはSerialドライバを引かない。

heapの上限も`_eusrstack`(RAM最上位)から`_heap_end`(予約stack領域の下端)へ直しました。
暴走したallocationがstackを黙って踏み潰す代わりに失敗します。

## 結果

X035実機。Blinkのサイズは**変わっていない**(932バイト、size baselineも無変更)。

```text
heap test start        stdio test start
string=abcdef          write=direct
length=6               write returned 14
malloc=in range        printf=42 str x
readback=ok            printf returned 16
reuse=ok               puts=line
oom=null               wide=deadbeef
heap test done         stdio test done
```

回帰テストは[`tests/sketches/basic/heap_string`](../../tests/sketches/basic/heap_string)と
[`stdio_printf`](../../tests/sketches/basic/stdio_printf)。
前者は`.init_array`で走るグローバル`String`を持たせてあり、
**壊れていれば`setup()`の1行目すら出ない**ので、最初のexpectが実質そのまま検出器になります。

## 学んだこと

- **bare-metalでnewlibを使う以上、libglossのsemihosting stubは常に敵**。
  `ecall`を出すものが1つでもリンクされたら、CH32ではリセットループになる。
  リンク順ではなく`--start-group`で潰すのが正しい
- `PROVIDE`が`[!provide]`のまま残っていたら、**その定義を参照するobjectがリンクされていない**。
  `.map`のこの1行だけで原因1は特定できた
- ヘッダは自分が必要とするものを自分でincludeする。
  「`Arduino.h`が先に読まれる前提」は、coreの内部では成り立たない
- compile testでは絶対に見つからない種類のバグ。**実機で1行出力を見るtestが要る**

## 続き: newlib-nanoの適用(ADR-0004)

このバグを追う過程で、`printf`を使うsketchが**48 KB**になることが分かりました。
CH32V003は16 KB flashなので、そもそも載りません。

[ADR-0004](../adr/0004-runtime-and-cxx.ja.md)は
「default runtimeはnewlib-nano、`%f`はmenu opt-in」と既に決めていたにもかかわらず、
`platform.txt`には`--specs=nano.specs`が入っていませんでした。決定どおりに実装しました。

- `platform.txt`: `--specs=nano.specs {build.printf_flags}`
- `boards.txt`(生成): `menu.printf` = `none`(既定) / `float`(`-Wl,-u,_printf_float`)

サイズ(`printf("%d")` + `printf("%.2f")`のsketch):

| | CH32X035 | CH32V003(16K) |
|---|---|---|
| 適用前(フルnewlib) | 48,492 | **リンク不能** |
| `printf=none` | **7,064** | **6,652** |
| `printf=float` | 25,912 | リンク不能(flash不足) |

X035実機で両方確認:

```text
printf=none                printf=float
float test start           float test start
int=42                     int=42
float=                     float=1.50
float test done            float test done
```

`printf=none`で`%f`が空になるのはnewlib-nanoの仕様どおりで、ADR-0004が
「Arduino利用者の既知の落とし穴」として明示を求めていた挙動です。

副次的に、`heap_string` / `serial_echo` / `stdio_printf`が**すべてCH32V003に載るようになりました**
(nanoのmalloc arenaが小さいため)。適用前は2 KB RAMを`.bss`が溢れてリンクできず、
sketchごとにboardを外す必要がありました。

## 残った課題

- `__stack_size`の既定が512バイト。`printf`は簡単に超える([todo](../todo.ja.md))
- `_fstat`が`st_blksize`を設定していない
