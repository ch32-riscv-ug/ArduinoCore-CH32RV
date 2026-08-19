# ADR-0004: runtimeはnewlib-nanoをdefaultとし、C++はGNU++17とする

- Status: Accepted
- Date: 2026-08-19
- Related questions: Q-022, Q-023

## Context

対象SKUはFlash 16K/RAM 2KB(CH32V003)〜が下限。newlibの構成とC++標準を、実測に基づいて決める必要がある。

## Decision drivers

- 小容量SKUで成立すること(実測: [実験0006](../experiments/0006-newlib-size-baseline.ja.md))
- ArduinoCore-APIの要求(C++11以上)と主要ライブラリ互換
- 選択肢はmenuとして提供し、defaultは安全側にする

## Options considered

### newlib(full)をdefault

printf %dで47.9KB、C++ `new`1つで114KB(失敗経路がfull stdioを引込む)。対象SKU群で成立しない。不採用。

### newlib-nanoをdefault(採用)

printf %d 4.9KB、new/delete 1.2KB、C++ virtual +32B。`%f`は`-u _printf_float`のopt-in(+19.5KB)。

### C++標準

gnu++11/14/17でサイズはバイト一致(実験0006/0007)。ArduinoCore-API(2025-10 commit)はgnu++17で警告ゼロcompile。新しい標準を選ばない理由がない。

## Decision

- **default runtimeはnewlib-nano**(`--specs=nano.specs`)。fullはmenuにも載せない(必要が実証されたら再検討)
- printfの浮動小数点対応はdefault無効。**menu opt-in**(`-Wl,-u,_printf_float`)で提供する
- **C++はGNU++17**、`-fno-exceptions -fno-rtti -fno-threadsafe-statics`をdefaultとする
- コアの公開API(Print/Serial等)は**newlibのprintfに依存しない**実装とし、printf系コストはユーザーが直接使った場合に限定する
- coreは`ltoa`/`ultoa`/`dtostrf`を提供する(newlib非搭載、実験0007)。CH32V003(RAM 2K)向け文書ではstdio/String非推奨を明示する

## Consequences

- nanoの`%f`非対応はArduino利用者の既知の落とし穴になる → boards.txtのmenu文言とdocumentで明示
- exceptions/RTTI無効は一部ライブラリと非互換の可能性(要request対応)
- `-fno-threadsafe-statics`はシングルコア前提(H417 dual core対応時に再検討)

## Validation

- sizebench([prototypes/sizebench/](../../prototypes/sizebench/README.ja.md))をtoolchain更新時に再実行し回帰を検出
- W-7 size baselineがdefaultフラグ変更の影響を検出

## References

- [実験0006](../experiments/0006-newlib-size-baseline.ja.md)、[実験0007](../experiments/0007-arduinocore-api-target-build.ja.md)
