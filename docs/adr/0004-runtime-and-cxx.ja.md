# ADR-0004: picolibc、compiler-rt、libc++ と GNU++17 を既定にする

- Status: Accepted

## 決定

- C ライブラリは picolibc、実行時のルーチンは compiler-rt、C++ は libc++（どれも ch32rv-toolchain に入れる、
  ADR-0002）。picolibc の printf / scanf は整数だけの版を既定にしてビルドする
- C++ は GNU++17、exceptions / RTTI / thread-safe statics は無効とする
- printf の系統（`Serial.printf`、`printf`、`sprintf`、`snprintf` など）、`Serial.print(float)`、`String(float)`、
  `dtostrf()` はコアの書式化（`cores/arduino/ch32rv_format.c`）で動かす。`rtlib` menu（表示は Floating Point）で、
  double / float の計算と printf の `%f` を選ぶ。picolibc の浮動小数の printf / scanf も選択肢に残す
- `strftime()`、`mktime()`、`gmtime_r()`、`localtime_r()`、`tzset()` と、いくつかの実行時のルーチン（32 bit の割り算、
  float と double の変換、`__clzsi2` / `__ctzsi2`）は、C ライブラリや compiler-rt より小さいコアのものを使う

`rtlib` の値は、double / float の計算と printf の `%f` の有無の 4 通り（`fltf`、`flt`、`dbl`、`dblf`）と、picolibc の
`libcfp`、`libcfs`、`dblfs`、`fltfs`、`libcfps` です。`parseFloat` の計算（double / 整数 / float）は別のメニュー
`parsefloat`（`auto`、`int`、`float`、`double`）で選び、どの組み合わせも選べます（[printf と浮動小数](../printf-float.ja.md)）。

既定は `fltf`（float の計算、printf の `%f` あり）と parseFloat の `auto`（float の計算では整数）です。小数を使うスケッチに
soft-double を入れず、`%f` の追加は数百 B で済みます。FPU のない系列では、小数の表示の float の計算を、ビットに対する
整数の計算で行います（出力は同じ）。メニューは既定を先頭に、残りを入る部品の少ない順に並べます。

## 理由

最小対象には flash 16 KB、RAM 2 KB の型番があります。浮動小数点の stdio を既定にすると基本 sketch でも
収まらないため、使用者が必要な変換だけを選ぶ構成にします。picolibc は newlib-nano より小さく（double の printf が
newlib の約 21 KB に対して約 6 KB）、16 KB の型番でも printf か scanf の片方なら浮動小数点を選べます。GNU++11/14/17 のサイズ差は
実質なく、ArduinoCore-API と一般的な library の互換性を優先して GNU++17 を使います。

printf の系統をコアの書式化にしているのは、C ライブラリの書式化が printf 1 回でスタックを 500〜700 B 使い
（newlib-nano で測定）、既定のスタックの確保（512 B）をはみ出したためです。コアの書式化は 240〜340 B のスタックで動き、
`Serial.print(float)` も `rtlib` の選択に従います（float を選ぶと float で計算する）。
