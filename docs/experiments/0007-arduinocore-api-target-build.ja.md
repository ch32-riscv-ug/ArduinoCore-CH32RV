# 実験0007: ArduinoCore-APIのtarget compile/link/サイズ

実施日: 2026-08-19
対象question: Q-010、Q-023、Q-003(サイズ面)
実施環境: WSL2 Linux x86_64、xPack riscv-none-elf-gcc 14.3.0-1、newlib-nano。サイズのみ(実行なし)

## 目的

コア実装の入口であるArduinoCore-APIが、最小SKU向け構成(rv32emc_zicsr/ilp32e、GNU++17、-fno-exceptions/-fno-rtti/-fno-threadsafe-statics)で無改変compileできるか、不足シンボルは何か、String/Printの実コストはいくらかを実装前に確認する。

## 入力の固定

- [arduino/ArduinoCore-API](https://github.com/arduino/ArduinoCore-API) commit `0f4e57ea193a00163ae59f0f0ff478feae7eb5db`(2025-10-12、tagなし)
- toolchain/ld/crt0は実験0001/0002と同一(統合crt0+own sections.ld、計測用MEMORY 1M/128K)

## 結果

1. **api/の全8 .cppがrv32emc_zicsr/ilp32e + GNU++17で警告ゼロcompile**(CanMsg, CanMsgRingbuffer, Common, IPAddress, PluggableUSB, Print, Stream, String)
2. **link時の不足シンボル(=coreが提供すべきもの)を特定**:
   - newlib(-nano)は`itoa`/`utoa`を持つが、**`ltoa`/`ultoa`は持たない**(String(long)系で必要)
   - **`dtostrf`はnewlibに存在しない**(String(float/double)で必要)。AVR系遺産で、samd/stm32等の他コアと同様にcore側で実装する
3. サイズ(text/data/bss、nano、crt0+syscall stub込み):
   - Print(println int + println float): **10,612 / 60 / 524**。float出力はPrint::printFloatのdouble演算がsoft-double(libgcc)を引き込み、約5〜6KBを占める
   - String(int/long/unsigned long連結+Print出力、floatなし): **7,492 / 108 / 852**(malloc/realloc込み)
   - 上記+String(float)(dtostrf簡易実装): **7,636 / 108 / 852**
4. **C++標準によるサイズ差はゼロ**: 同一ケースをgnu++11/14/17でビルドしtext/data/bssがバイト一致

## 結論

- ArduinoCore-APIは対象最小構成で**無改変利用できる見込みが高い**(compile層の障害なし)。Q-010の残論点はversion固定方法とLGPL-2.1の配布満足のみ
- core実装の必須提供物リストに`ltoa`/`ultoa`/`dtostrf`(+`millis`等のHAL関数群)を含める
- String利用sketchは約7.5KB+RAM(heap)を要するため、16K/2K級SKUの案内(String非推奨)が必要。Printのfloat出力コスト(soft-double)は将来の最適化候補(single precision化等)
- gnu++17採用にサイズ上の代償はない(→[ADR-0004](../adr/0004-runtime-and-cxx.ja.md))

## 再現手順(要点)

ArduinoCore-APIを上記commitでclone → `api/*.cpp`を`-march=rv32emc_zicsr -mabi=ilp32e -Os -std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics`でcompile → Print/String使用ケースを統合crt0+syscall stub+`--specs=nano.specs`でlink(ltoa/ultoa/dtostrfはスタブ提供)し`size`計測。
