# 実験0011: Milestone 1達成 — CH32V003実機で`Serial.println()`

実施日: 2026-08-20
対象: Milestone 1(主要boardで`Serial.println()`が通る)、Q-013(内部HAL contract)
実施環境: WSL2 + usbip、arduino-cli 1.3.1、xPack riscv-none-elf-gcc 14.3.0-1、
**CH32V003(16K/2K)+ WCH-LinkE v2.12**

## 結果

[受け入れsketch](../../tests/sketches/basic/serial_println/serial_println.ino)が実機で通った。

```
hello from ch32
int=42
hex=BEEF
```

送受信とも動作する(`echo:X` / `echo:y`を別sketchで確認)。
`t=1000` / `t=2000` / `t=3000`が1秒間隔で出るので、SysTickも正しい。
`F_CPU=24000000`をsketchから出力させ、HSI直結24MHzで動いていることも確認した。

再現: `tests/manual/smoke.py --board CH32V003 --port /dev/ttyACM4`([手順](../../tests/manual/README.ja.md))。

## 実装したもの

| file | 内容 |
|---|---|
| `cores/arduino/ch32_registers.h` | 自前の最小レジスタmap。structではなくoffsetで書く。EVTのheaderは**照合にのみ**使用 |
| `cores/arduino/ch32_gpio.h` | port幅(8/16/24)差を吸収するinline GPIO primitive |
| `cores/arduino/wiring_digital.c` | `pinMode`/`digitalWrite`/`digitalRead` |
| `cores/arduino/wiring_time.c` | `SystemInit`(HSI直結)、SysTick 1kHz、`millis`/`micros`/`delay` |
| `cores/arduino/HardwareSerial.{h,cpp}` | 割込み駆動のTX/RX ring buffer。AFIO remapも適用 |
| `cores/arduino/syscalls.c` | `_write`(printf→Serial)、heap/stack衝突を防ぐ`_sbrk` |
| `cores/arduino/itoa.c` / `dtostrf.c` | `ltoa`/`ultoa`と、upstream `.c.impl`の設置 |
| `cores/arduino/Arduino.h` | `api/ArduinoAPI.h`へ接続 |

family差は`build.core_defines`(生成物)で渡す。値の出どころはEVTの
peripheral headerを読んで確認した実測表:

| family | GPIO port幅 | SysTick CNT/CMP | HSI |
|---|---:|---|---:|
| V003 / V006 | 8 | 32bit | 24 MHz |
| X033 / X035 | **24**(`CFGXR`/`BSXR`) | 64bit | 48 MHz |
| V20x / V307 / L103 | 16 | 64bit | 8 MHz |
| V205 / V407 / X315 / M030 | 16 | 32bit | 8 / 20 MHz |

**SysTickのregister offsetは全familyで同じ**(CTLR 0x00 / SR 0x04 / CNT 0x08 / CMP 0x10)。
32bit品はCNTを8バイトへpaddingしているのでCMPの位置がずれない。

## 見つかったbug: ベクタテーブルのベースアドレス

**症状**: `Serial.println()`が1文字も出ず、`millis()`が0のまま。
CPUは走っている(loop回数が伸びる)のに、SysTick割込みが一度も入らない。

**原因**: **QingKe V2(V003/V00x)の`mtvec`はベースアドレスの下位ビットを捨てる。**

crt0は`.init`に`_start: j handle_reset`を置き、別の`.vector`セクションに
テーブルを置いていた。そのためテーブルは**アドレス8**から始まり、
`mtvec = _vector_base | 3 = 0x0B`を書いていた。実機で`mtvec`を読み戻すと**`0x00000003`**。
つまりベースが0に丸められ、SysTick(IRQ 12)はアドレス`0 + 48`を読む。
そこは`.init`のpaddingなので0 → アドレス0へジャンプ → **1msごとに再起動**していた。

64バイト境界へ揃えても直らなかった(`mtvec`は`0x03`のまま)ので、必要なalignmentは
64より大きい。EVTのV003 startupは**テーブルをアドレス0に置き、slot 0を`j handle_reset`
そのものにしている**。これに合わせた。

**修正**: `.vector`をFLASHの先頭に置き、**リセットジャンプをテーブルのentry 0にする**。
ベースが0なら世代を問わずalignedなので、全familyで同じlayoutにできる。
`sections.ld`に`ASSERT(_vector_base == 0, ...)`も入れた。

### なぜCIで捕まらなかったか

startup等価性harnessは**ベクタテーブルの中身**は比較するが、
**その配置アドレスは比較していなかった**。`compare.py`には

```python
# mtvec differs by design only in symbol, value check: both write mtvec with table|3.
```

とあり、mtvecを比較対象から除外していた。「テーブルの中身が正しい」ことと
「そのテーブルが使われる」ことは別問題である。

`compare.py`に`_vector_base == 0`のcheckを追加した(13 variantすべてpass)。

## そのほか確定したこと

- **debugger接続はtargetをresetする**。RAMはresetで消えないので、`.bss`の外側
  (V003なら`0x20000100`付近)へmarkerを書くと、resetを跨いで観測できる。
  peripheral registerを直接読むと**reset後の値**しか見えず、誤診の原因になる
- `pin_functions.csv`のUART signal名は**family間で正規化されていない**
  (`UTX`/`UART_TX`/`TX1`/`USART1_TX`)。生成器側で吸収した
- **TXとRXは同一routeから選ぶ必要がある**。route別に最良を選ぶと、
  V205でTX=PA6(af-2)・RX=PA10(af-3)という同時に使えない組み合わせが出た
- `default`以外のrouteは**AFIO `PCFR1`の設定が要る**。`remap_fields.csv`の
  bit listは非連続のことがある(V003 `USART1_REMAP`は**bit 2と21**)ので、
  値をbit listへLSBから分配する
- device-dataに**X033/X035のUSART1 remapフィールドが無い**。生成器は
  「programmableなrouteを優先」するのでUSART2(default route)へ落ちるが、
  upstreamへ報告する

## サイズ

Blink(`pinMode`/`digitalWrite`/`delay`)の実装後サイズ。`--gc-sections`が
未使用のSerialを落とすので、GPIOしか使わないsketchは小さいまま。

| board | text | data | bss | 備考 |
|---|---:|---:|---:|---|
| CH32V003/ANY | 804 | 4 | 520 | stub時代は476 |
| 受け入れsketch(Serial込み) | 2516 | - | 196 | 16Kの15% |

## 未確認

- X035 / L103 / V20x はまだ実機で回していない([runner](../../tests/manual/README.ja.md)は用意済み)
- `af-N` routeの3 series(V205/X305/X315)はper-pin alternate-function方式で、コアが未対応
- `analogRead`/`analogWrite`/SPI/I2C/割込みAPIは未実装
