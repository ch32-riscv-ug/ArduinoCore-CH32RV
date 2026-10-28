# ADR-0004: newlib-nano と GNU++17 を既定にする

- Status: Accepted

## 決定

- C runtime は `--specs=nano.specs` を既定とする
- C++ は GNU++17、exceptions / RTTI / thread-safe statics は無効とする
- `printf` / `scanf` の浮動小数点変換は `rtlib` menu で明示的に追加する
- full newlib は menu に含めない
- （2026-10-28 追記）printf の系統（`Serial.printf`、`printf`、`sprintf`、`snprintf` など）、`Serial.print(float)`、
  `String(float)`、`dtostrf()` はコアの書式化（`cores/arduino/ch32rv_format.c`）で動かす。`rtlib` menu
  （表示は Floating Point）で、double / float の計算と printf の `%f` を選ぶ。newlib の printf / scanf の浮動小数も
  選択肢に残す

`rtlib` の値は、double / float の計算、printf の `%f` の有無、`parseFloat` の計算（double / 整数 / float）の組み合わせ 12 通り
（`dbl_pd`（既定）〜`fltf_ps`）と、newlib の `dblfs`、`fltfs`、`nanofp`、`nanofs`、`nanofps` です
（[printf と浮動小数](../printf-float.ja.md)）。

## 理由

最小対象には flash 16 KB、RAM 2 KB の型番があります。full newlib や浮動小数点 stdio を既定にすると
基本 sketch でも収まらないため、使用者が必要な変換だけを選ぶ構成にします。GNU++11/14/17 のサイズ差は
実質なく、ArduinoCore-API と一般的な library の互換性を優先して GNU++17 を使います。

2026-10-28 に、printf の系統をコアの書式化に替えました。newlib-nano の書式化は printf 1 回でスタックを 500〜700 B 使い、
既定のスタックの確保（512 B）をはみ出していました。また `String(float)` が newlib の浮動小数の printf（約 21 KB）を必ず
つないでいました。コアの書式化は 240〜340 B のスタックで動き、整数だけの printf で 1.3〜3 KB 小さくなります。
`Serial.print(float)` も `rtlib` の選択に従います（float を選ぶと float で計算する）。
