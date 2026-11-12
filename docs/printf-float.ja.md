# printf と浮動小数

`Serial.printf()`、`printf()`、`sprintf()` / `snprintf()` と、`Serial.print(float)`、`String(float)`、`dtostrf()` の書式化、`Serial.parseFloat()` の読み取り、それに `strftime()` などの時刻の関数について説明します。書式化はコア（`cores/arduino/ch32rv_format.c`）で、読み取りの計算もコア（`cores/arduino/internal/ch32rv_parse_float.h`）で動きます。浮動小数の扱いは **Floating Point**（`rtlib`）と **parseFloat**（`parsefloat`）の 2 つのメニューで選びます。どちらの項目の組み合わせも選べます。

## Floating Point のメニュー

| 項目の id | 計算 | printf の `%f` | scanf の `%f` |
|---|---|---|---|
| **`fltf`（既定）** Float (32-bit) + printf %f | float | コア | なし |
| `flt` Float (32-bit) | float | 出さない（書式のまま） | なし |
| `dbl` Double (64-bit) | double | 出さない | なし |
| `dblf` Double (64-bit) + printf %f | double | コア | なし |
| `libcfp` Double (64-bit) + picolibc printf %f %e %g | double | picolibc | なし |
| `libcfs` Double (64-bit) + picolibc scanf %f | double | 出さない | picolibc |
| `dblfs` Double (64-bit) + printf %f + picolibc scanf %f | double | コア | picolibc |
| `fltfs` Float (32-bit) + printf %f + picolibc scanf %f | float | コア | picolibc |
| `libcfps` Double (64-bit) + picolibc printf/scanf %f %e %g | double | picolibc | picolibc |

「なし」の項目でも scanf は使えます。整数だけの版（picolibc の既定）で、`%f` は読みません。

## parseFloat のメニュー

`Serial.parseFloat()` の計算です（後の「parseFloat の計算」）。

| 項目の id | 計算 |
|---|---|
| **`auto`（既定）** | 計算に合わせる。Float の項目では整数、Double の項目では double |
| `int` | 整数 |
| `float` | float |
| `double` | double |

- **計算が float の選択肢**（`flt`、`fltf`、`fltfs`）:
  - 小数の定数（`3.3` など）を float として扱います（`-fsingle-precision-constant`）。
  - `Serial.print(float)`、`String(float)`、`dtostrf()`、`%f` も float で計算します。
  - FPU のない系列（V00x、X035、L103 など）では、この float の計算を、float の値のビットに対する整数の計算で行います。丸めも float の計算と同じなので、出力は 1 文字も変わりません。小数の表示のためだけに soft-float が入ることはありません。
  - AVR の Arduino（double が 32 bit）と同じ精度で、有効数字は 7 桁ほどです。
  - double の正確な定数が要るときは、`(double)3.14159265358979L` と書きます。`L` を付けずにキャストしただけでは、float の精度のままです。
- **計算が double の選択肢**：`Serial.print(float)`、`String(float)`、`dtostrf()` の出力は、以前のコアと同じです。
- **picolibc の選択肢**（`libcfp`、`libcfps`）：printf の系統は picolibc の書式化（double の版）に戻ります。`%e` と `%g` も出せます。picolibc の浮動小数の書式化が入るので、約 6 KB 増えます（newlib の頃は約 21 KB）。
- **printf の `%f` を有効にしていない選択肢**：`%f` は書式の文字列（`%.2f` など）のまま出ます。浮動小数の処理は何もつながりません。

## 既定と選び方

### 大きさを決めるもの

スケッチの大きさは、次の部品のどれが入るかで、ほぼ決まります（V003 / X035 / V307、2026-10-29。小数の表示を整数の計算にした後の値）。

| 部品 | 中身 | 大きさ | 入る条件 |
|---|---|---|---|
| soft-double | double の四則演算をソフトで行う関数 | 約 9.2 / 8.4 / 14.5 KB | Double の選択肢で小数を使う（`* 3.3` や `print(float)` だけでも）。parseFloat を double で計算する。`double` と書いた変数を使う |
| soft-float | float の四則演算をソフトで行う関数 | 使う演算の分だけ、最大で約 4.6 / 4.6 / 約 0.9 KB（V307 は FPU） | Float の選択肢で小数を計算する（表示だけなら入らない） |
| printf の `%f` | printf の中の小数の処理 | 0.9 / 0.8 / 1.5 KB（小数を使っていなくても、これだけ） | `%f` ありの選択肢で printf を使う。Double の選択肢（`dblf`）では soft-double も一緒に入る |
| parseFloat の整数の計算（`int`） | 64 bit の割り算 | 0.8 / 0.4 / 1.5 KB | parseFloat を整数で計算する |

soft-float と soft-double の引き算は、コアが足し算で行います（`cores/arduino/ch32rv_softfloat.c`。結果はビット単位で同じ）。上の大きさは、引き算を足し算と別に持っていたときの値です。足し算と引き算の両方を使うスケッチは、float で約 1.2 KB、double で約 2.2 KB（V307 は 3.9 KB）小さくなります。引き算だけを使うスケッチは、float で約 100 B 小さく、double で 50〜120 B 大きくなります。

`Serial.print(float)` などで float を double に広げる関数も、コアの小さいもの（V003 で 120 B、libgcc は 174 B）です。結果は libgcc とビット単位で同じです（NaN の符号と中身も残します）。典型的なスケッチで V003 は 56 B、X035 は 64 B、V307 は 504 B 小さくなります。

soft-float が先頭の 0 のビットを数える関数（`__clzsi2`）もコアのもので、libgcc の 256 B の表を使いません。結果は libgcc と同じです（32 bit の全部の値を PC で、100 万件を V003 で libgcc のものと照合）。soft-float か soft-double を使うスケッチは、V003・X035 で約 310 B 小さくなります。

V003 で、機能を足していったときの実測です。

| スケッチ | `fltf`（既定） | `flt` | `dbl` |
|---|---|---|---|
| 何もしない | 2280 | 2280 | 2280 |
| 整数を `Serial.printf` | 4612 | 3736 | 3736 |
| ＋ `analogRead() * 3.3 / 1023` を `Serial.println` | 7356 | 7136 | 14448 |
| ＋ `Serial.printf("%.2f")` | 7392 | 7168（`%.2f` のまま出る） | 14484（`%.2f` のまま出る） |

小数の表示を整数の計算にする前（2026-10-29 の朝）は、`fltf` が上から 8984、10620、10660、`flt` が 3736、9852、10068 でした。

### 既定（`fltf`）にした理由

- **小数を使わず printf も使わないスケッチ**は、どの選択肢でも同じ大きさです。V003 で一番小さくするなら、この書き方にします。
- **小数を使うスケッチ**は、Float なら soft-double（9〜15 KB）が入りません。`%f` を足しても 0.2〜0.3 KB です。
- **損をするのは、小数を使わずに printf を使うスケッチだけ**です（V003 で +0.9 KB、X035 で +0.8 KB、V307 で +1.5 KB）。16 KB の部品では、printf はもともと贅沢なので、`%f` も書けるほうが混乱が少ないと判断しました。
  - これを省くには、`Float (32-bit)` の選択肢（`flt`）を選びます。
- **Double が要るとき**は、`dbl` か `dblf` を選びます。GPS の緯度経度（float では 0.5 m ほどの誤差）や、小さな値を何万回も足す計算です。ARM や ESP32 の Arduino と同じ結果がほしいときも同様です。

### 並び順

どちらのメニューも、既定を先頭に置きます。Floating Point の残りは、入る部品の少ない順です。

1. `fltf`（既定）
2. `flt`（soft-float だけ）
3. `dbl`（小数を使うと soft-double）
4. `dblf`（printf を使うと soft-double）
5. picolibc の `libcfp`、`libcfs`、`dblfs`、`fltfs`、`libcfps`（+5〜9 KB）

picolibc の項目の大きさ（2026-11-12）は、`sscanf("%f")` と `snprintf("%f %e %g")` を 1 回ずつ使うスケッチで、
`dbl` より増える分です。X035 で `libcfp` +5.8 KB、`libcfs` +5.0 KB、`dblfs` +9.3 KB、`fltfs` +5.9 KB、`libcfps` +9.3 KB。
V003 と V307 もこれと 1 KB 以内の差で、V003（16 KB）では `dblfs` が 16,328 B、`libcfps` が 16 B はみ出しました。

### 大きさの実測（スケッチ全体、2026-10-28。V003・X035 の float の列は 2026-10-29。GCC と newlib の頃の測定）

| スケッチ | 系列 | 以前（newlib） | `dbl` | `dblf` | `flt` | `fltf` |
|---|---|---|---|---|---|---|
| `Serial.printf` で整数 | V003 | 5304 | 3736 | 13576 | 3736 | 4612 |
| | X035 | 5768 | 4176 | 13188 | 4176 | 5008 |
| | V307 | 6308 | 4700 | 19836 | 4700 | 6232 |
| アナログ値を float にして `Serial.print` | V003 | 12856 | 12948 | 12948 | 7096 | 7096 |
| | X035 | 12660 | 12732 | 12732 | 7484 | 7484 |
| | V307 | 20016 | 20084 | 20084 | 5608 | 5608 |
| `printf()` で整数 | V003 | 6728 | 3732 | 13568 | 3744 | 4624 |
| `snprintf` で整数 | V003 | 5156 | 3820 | 13656 | 3828 | 4704 |
| `String(float)` | V003 | 入らない | 11024 | 11024 | 4292 | 4292 |
| | X035 | 27420 | 11040 | 11040 | 4760 | 4760 |

- **`%f` を有効にする費用**：printf の書式化の中に `%f` の処理があると、`%f` を書かなくても、浮動小数の計算の処理がつながります。
  - double のとき：FPU のない系列で約 10 KB
  - float のとき：FPU のない系列で約 0.9 KB（float の計算を整数で行うため。以前は soft-float が入って約 5 KB）、V30x・X3x5・H41x（単精度の FPU あり）で約 1.5 KB
- スケッチがもともと浮動小数を表示していれば、その処理はすでに入っているので、増えるのは数百 B です。
- **FPU のある系列では、float にするとはっきり小さくなります。**
- **定数と `printFloat` の片方だけを float にすると、逆に大きくなります**（float と double の両方の処理が入るため）。そのため、メニューでまとめて切り替える作りにしています。

## parseFloat の計算

`Serial.parseFloat()` などの `Stream::parseFloat()` は、読んだ数字から float を作ります。その計算を、parseFloat のメニューで 3 通りから選べます（既定の `auto` は、Float の項目で `int`、Double の項目で `double`）。正しさを取るか、大きさを取るかの選択です。

| parseFloat | 結果 | 引き込む処理 |
|---|---|---|
| `double` | どの入力でも、いちばん近い float（上流の Arduino と同じ） | double の計算 |
| `int` 整数 | 有効数字 11 桁までの入力なら、いちばん近い float | 64 bit の整数の割り算 |
| `float` | 8 つに 1 つほどの入力で、最後の 1 ビットがずれる | float の計算だけ |

- `int` で 12 桁以上の入力が 1 ビットずれるのは、値が 2 つの float のちょうど中間に、11 桁目より細かいところまで近いときだけです。
- 以前のコアは、Float の選択肢で `0.1` が float の定数になっていました。そのため、double の計算でも、0.27% ほどの入力が 1 ビットずれていました。今はどの選択肢でも double の `0.1` を使います。

### 大きさ（スケッチ全体、2026-10-28。V003・X035 の `flt` の 1 行目は 2026-10-29）

| スケッチ（Floating Point ＋ parseFloat） | 系列 | `dbl`＋`double` | `dbl`＋`int` | `dbl`＋`float` | `flt`＋`double` | `flt`＋`int` | `flt`＋`float` |
|---|---|---|---|---|---|---|---|
| `parseFloat` の値を `Serial.println(v, 6)` | V003 | 11148 | 13352 | 12588 | 8104 | 6476 | 5732 |
| | X035 | 11132 | 12888 | 12500 | 8272 | 6396 | 6036 |
| | V307 | 16948 | 16992 | 15788 | 11816 | 5320 | 3872 |
| `parseFloat` の値を 1000 倍して整数で表示 | V003 | 8240 | 5836 | 5076 | 8240 | 5836 | 5076 |
| | X035 | 8412 | 5740 | 5376 | 8412 | 5740 | 5376 |
| | V307 | 11552 | 5056 | 3596 | 11552 | 5056 | 3596 |

- 以前のコアでは、`parseFloat` のスケッチは V003 で 11176 B と 10556 B でした（Float の選択肢では 13892 B と 10556 B）。double の計算は、結果を変えずに約 2.3 KB 小さくなりました（`int` から double への変換が 1 回で済むようにしたため）。
- **double の計算がすでに入っているスケッチ**（Double の選択肢で float を表示するもの）では、`double` がいちばん小さくなります。
- **それ以外**では、`int` と `float` が 1.6〜11 KB 小さくなります。

## printf が扱う書式

コアの書式化（picolibc の選択肢以外）が扱うものです。

- **変換**：`d i u o x X c s p %`、`%f %F`（メニューで有効にしたとき）
- **フラグ**：`-` `0` `+` 空白 `#`
- **幅と精度**：数字か `*`
  - 精度は、整数なら最低の桁数（`%.5d` → `00042`）、文字列なら最大の文字数（`%.3s`）、`%f` なら小数点以下の桁数です。
- **長さ**：`hh h l ll j z t`
  - `%lld`、`%llu`、`%llx` などの 64 bit は、libgcc の 64 bit の割り算を使わずに、32 bit の割り算で 16 bit ずつ出します。printf を使うスケッチは、使わなくても V003 で約 50 B 増えます（X035 は約 25 B）。
  - そのかわり、printf の数字は 1 桁ごとに割り算が 4 回になり、以前より遅くなります。`Serial.print(int)` は別の処理なので変わりません。

出せないもの（`%e %g %a`、`%n`）は、**書式の文字列のまま**出します。黙って捨てることはしません。そのあとの引数もずれません。

### C の printf との違い

- **`%f` の丸め**：`Serial.print(double)` と同じく、最後の桁の 0.5 を足してから切り捨てます。ちょうど半分の値（`123456.78125` を 4 桁）では、C は偶数に丸めて `.7812`、このコアは `.7813` を出します。
- **大きすぎる値**：絶対値が 4294967040 を超えると `ovf` を出します（`Serial.print` と同じ）。
- **`nan` と `inf`**：`%f` では、`nan`、`inf`、`-inf` を出します。`Serial.print` では、今までどおり符号なしの `nan`、`inf` です。
- **picolibc の中から呼ばれる printf**：picolibc の関数が `snprintf` などを呼ぶと、コアの定義が先に見つかるので、コアの書式化で動きます。
  - コアの名前と picolibc の名前がぶつかって、リンクに失敗することはありません。
  - `strftime()` はコアにあります（次の節）。

### スタックと出力のしかた

- **スタック**：printf 1 回で、以前は 500〜700 B 使っていました。スタックの確保（512 B）をはみ出して、ヒープを壊すことがありました。今は 240〜340 B です。
- **出力のしかた**：行を一時領域に組み立てずに、出力先へ少しずつ書きます。ヒープも使いません。
- **`printf()`、`puts()`、`putchar()`**：picolibc の FILE を通さず、すぐに標準出力の先（`Serial` など）へ書きます。
  - この 3 つを混ぜても、順番どおりに出ます。
  - `fputs()`、`fwrite()`、`fprintf()` で `stdout` に書くものは picolibc の FILE を通りますが、バッファを持たず 1 文字ずつすぐに書くので、混ぜても順番は入れ替わりません（`cores/arduino/ch32rv_stdio_file.c`）。
- **picolibc の選択肢では**、printf の系統は picolibc のものに戻ります。

## 時刻の関数

`strftime()`、`gmtime_r()`、`gmtime()`、`localtime_r()`、`localtime()`、`tzset()` は、コアのものを使います（`cores/arduino/ch32rv_strftime.c`、`ch32rv_gmtime.c`、`ch32rv_localtime.c`、`ch32rv_tzset.c`）。`mktime()` もコアのものです（`ch32rv_mktime.c`）。出す文字と結果は newlib 4.5 と同じです。`time()`、`ctime()` などは picolibc のものです。

C ライブラリが newlib だった頃は、そのままでは次のものが一緒に入っていました（picolibc と比べた大きさは、まだ測っていません）。

- `strftime()`：newlib の printf と、TZ を読むための sscanf と malloc
- `gmtime_r()`：64 bit の割り算（RV32 で 1.9 KB）
- `tzset()`：sscanf と malloc（`localtime_r()` と `configTzTime()` が呼びます）

コアのものは、これらを使いません。

| スケッチ（Float + printf %f、GCC の頃） | 系列 | newlib | コア |
|---|---|---|---|
| `gmtime_r()` の結果を `Serial.print` | V003 | 5640 | 2960 |
| | X035 | 5384 | 3208 |
| `gmtime_r()` と `strftime("%F %T %s")` | V003 | 入らない | 8384 |
| | X035 | 17900 | 8312 |
| `setenv("TZ")`、`tzset()`、`localtime_r()`、`strftime("%F %T %Z %z")` | V003 | 入らない | 12576 |
| | X035 | 18884 | 12364 |
| 例 NetworkTime | V307 | 61060 | 54636 |

使い方は C の標準どおりです。newlib との違いは次の 2 つだけです。

- **`strftime()` の `%s`、`%z`、`%Z`**：`tzset()` を呼びません。最後に `tzset()`、`configTzTime()`、`localtime_r()` のどれかで読んだ時間帯を使います。`setenv("TZ", ...)` だけで変えた TZ は、次の `tzset()` のあとから使われます。
- **`strftime()` の大きさ 0**：何も書かずに 0 を返します（newlib は、渡した領域の外に書いていました）。

`tzset()` は、前回読んだ TZ の文字列をハッシュ（32 bit）で覚えていて、同じなら読み直しません（newlib は、ヒープに写しを取って比べていました）。

`gmtime_r()` は、newlib と同じく、日数を 32 bit で数えます。紀元から約 580 万年より先では、日数がひと回りします。

## 確かめたこと

- **PC での試験**（`tests/unit/test_core_format.py`）：メニューの組み合わせ（Float は、整数で行う手順と float で行う手順の両方）の 6 通りそれぞれで、PC の `snprintf` と出力を比べました（380〜500 件）。
- **float の計算を整数で行う手順**（2026-10-29）：float で計算する元の手順と、PC で出力を突き合わせました。
  - 乱数の 200 万個の値 × 桁数 0〜12 × 書式 4 通り（1 億 400 万件）で全件一致
  - float の全ビットパターン（約 43 億）を 2 桁と 6 桁で表示し、全件一致
  - V003、X035、V307 の実機で、6 万件の出力のハッシュが PC と同じ
- **時刻の関数**（2026-10-29）：newlib 4.5 のソース（`strftime.c`、`gmtime_r.c`、`tzset_r.c`）を PC でコンパイルして、コアのものと突き合わせました。
  - `strftime()`：乱数の日時と書式（全部の変換、`0` `+` と幅、`E` `O`、出力先の大きさ）3000 万件で全件一致。`tests/unit/test_core_strftime.py` に newlib の出力を表で残しています
  - `gmtime_r()`：3000 万件で全件一致。試験では PC の `gmtime_r()` と 300 万件比べます
  - `tzset()`：壊れた文字列を含む TZ 300 万件で、名前、時差、夏時間の規則まで全件一致。試験には 47 件を表で残しています
  - X035 の実機で、夏時間のある TZ（CET、AEST）と JST の 9000 件の `gmtime_r()`、`localtime_r()`、`strftime()` の出力のハッシュが、newlib のものと同じ。V003 でも同じハッシュ
- **`%lld`**：PC の `snprintf` と、端の値と乱数 2000 件で比べました。
- **parseFloat の PC での試験**（`tests/unit/test_core_parse_float.py`）：3 通りの計算それぞれで、30 万件の入力を PC の `strtof` と比べました。
  - `double`：全件一致
  - `int`：12 桁以上の 3 件が 1 ビットずれ
  - `float`：約 15% が 1 ビットずれ（2 ビット以上のずれはなし）
- **実機**：V003、X035、V307 で確かめました。
  - Double の選択肢の `Serial.print`、`String(float)`、`dtostrf()` は、以前のコアと 1 文字も変わりませんでした。
  - Float の選択肢の出力は、3 系列で同じでした。
  - `snprintf` の切り詰めと戻り値、`printf()` と `puts()` の順番を確かめました。
  - `parseFloat` の 30 の入力を、3 系列それぞれで `double`、`int`、`float` に通しました。float のビットは、PC で同じ計算をしたものと全部一致しました。
