# printf と浮動小数

`Serial.printf()`、`printf()`、`sprintf()` / `snprintf()` と、`Serial.print(float)`、`String(float)`、`dtostrf()` の書式化、それに `Serial.parseFloat()` の読み取りについて説明します。書式化はコア（`cores/arduino/ch32rv_format.c`）で、読み取りの計算もコア（`cores/arduino/ch32rv_parse_float.h`）で動きます。浮動小数の扱いは **Floating Point** のメニュー（`rtlib`）で選びます。

## Floating Point のメニュー

コアの選択肢は、次の 3 つの組み合わせ全部（12 通り）です。

- **計算**：Double (64-bit) か Float (32-bit) か
- **printf の `%f`**：出さない（書式のまま）か、コアで出すか（`+ printf %f`）
- **parseFloat**：double で計算するか、整数で計算するか、float で計算するか

| 選択肢の id | 計算 | printf の `%f` | parseFloat |
|---|---|---|---|
| `dbl_pd`、`dbl_pi`、`dbl_ps` | double | 出さない | `pd`：double、`pi`：整数、`ps`：float |
| `dblf_pd`、`dblf_pi`、`dblf_ps` | double | コア | 同上 |
| `flt_pd`、`flt_pi`、`flt_ps` | float | 出さない | 同上 |
| `fltf_pd`、**`fltf_pi`（既定）**、`fltf_ps` | float | コア | 同上 |

newlib を使う選択肢です。parseFloat は、計算が Double なら double、Float なら整数で計算します。

| 選択肢の id | 計算 | printf の `%f` | scanf の `%f` |
|---|---|---|---|
| `nanofp` Double (64-bit) + newlib printf %f %e %g | double | newlib | なし |
| `nanofs` Double (64-bit) + newlib scanf %f | double | 出さない | newlib |
| `dblfs` Double (64-bit) + printf %f + newlib scanf %f | double | コア | newlib |
| `fltfs` Float (32-bit) + printf %f + newlib scanf %f | float | コア | newlib |
| `nanofps` Double (64-bit) + newlib printf/scanf %f %e %g | double | newlib | newlib |

- **計算が float の選択肢**（`flt_*`、`fltf_*`、`fltfs`）:
  - 小数の定数（`3.3` など）を float として扱います（`-fsingle-precision-constant`）。
  - `Serial.print(float)`、`String(float)`、`dtostrf()`、`%f` も float で計算します。
  - FPU のない系列（V00x、X035、L103 など）では、この float の計算を、float の値のビットに対する整数の計算で行います。丸めも float の計算と同じなので、出力は 1 文字も変わりません。小数の表示のためだけに soft-float が入ることはありません。
  - AVR の Arduino（double が 32 bit）と同じ精度で、有効数字は 7 桁ほどです。
  - double の正確な定数が要るときは、`(double)3.14159265358979L` と書きます。`L` を付けずにキャストしただけでは、float の精度のままです。
- **計算が double の選択肢**：`Serial.print(float)`、`String(float)`、`dtostrf()` の出力は、以前のコアと同じです。
- **newlib の選択肢**（`nanofp`、`nanofps`）：printf の系統は newlib の書式化に戻ります。`%e` と `%g` も出せますが、newlib-nano の浮動小数の書式化が入るので、約 21 KB 増えます。
- **printf の `%f` を有効にしていない選択肢**：`%f` は書式の文字列（`%.2f` など）のまま出ます。浮動小数の処理は何もつながりません。

## 既定と選び方

### 大きさを決めるもの

スケッチの大きさは、次の部品のどれが入るかで、ほぼ決まります（V003 / X035 / V307、2026-10-29。小数の表示を整数の計算にした後の値）。

| 部品 | 中身 | 大きさ | 入る条件 |
|---|---|---|---|
| soft-double | double の四則演算をソフトで行う関数 | 約 9.2 / 8.4 / 14.5 KB | Double の選択肢で小数を使う（`* 3.3` や `print(float)` だけでも）。`_pd` で parseFloat を使う。`double` と書いた変数を使う |
| soft-float | float の四則演算をソフトで行う関数 | 使う演算の分だけ、最大で約 4.6 / 4.6 / 約 0.9 KB（V307 は FPU） | Float の選択肢で小数を計算する（表示だけなら入らない） |
| printf の `%f` | printf の中の小数の処理 | 0.9 / 0.8 / 1.5 KB（小数を使っていなくても、これだけ） | `%f` ありの選択肢で printf を使う。Double の選択肢（`dblf_*`）では soft-double も一緒に入る |
| parseFloat の整数の計算（`_pi`） | 64 bit の割り算 | 0.8 / 0.4 / 1.5 KB | `_pi` で parseFloat を使う |

V003 で、機能を足していったときの実測です。

| スケッチ | `fltf_pi`（既定） | `flt_pi` | `dbl_pd` |
|---|---|---|---|
| 何もしない | 2280 | 2280 | 2280 |
| 整数を `Serial.printf` | 4612 | 3736 | 3736 |
| ＋ `analogRead() * 3.3 / 1023` を `Serial.println` | 7356 | 7136 | 14448 |
| ＋ `Serial.printf("%.2f")` | 7392 | 7168（`%.2f` のまま出る） | 14484（`%.2f` のまま出る） |

小数の表示を整数の計算にする前（2026-10-29 の朝）は、`fltf_pi` が上から 8984、10620、10660、`flt_pi` が 3736、9852、10068 でした。

### 既定（`fltf_pi`）にした理由

- **小数を使わず printf も使わないスケッチ**は、どの選択肢でも同じ大きさです。V003 で一番小さくするなら、この書き方にします。
- **小数を使うスケッチ**は、Float なら soft-double（9〜15 KB）が入りません。`%f` を足しても 0.2〜0.3 KB です。
- **損をするのは、小数を使わずに printf を使うスケッチだけ**です（V003 で +0.9 KB、X035 で +0.8 KB、V307 で +1.5 KB）。16 KB の部品では、printf はもともと贅沢なので、`%f` も書けるほうが混乱が少ないと判断しました。
  - これを省くには、`Float (32-bit)` の選択肢（`flt_*`）を選びます。
- **Double が要るとき**は、`dbl_*` か `dblf_*` を選びます。GPS の緯度経度（float では 0.5 m ほどの誤差）や、小さな値を何万回も足す計算です。ARM や ESP32 の Arduino と同じ結果がほしいときも同様です。

### 並び順

既定を先頭に置き、残りは入る部品の少ない順です。

1. `fltf_pi`（既定）
2. `flt_ps`、`flt_pi`、`fltf_ps`（soft-float だけ）
3. `flt_pd`、`fltf_pd`（parseFloat を使うと soft-double）
4. `dbl_ps`、`dbl_pi`、`dbl_pd`（小数を使うと soft-double）
5. `dblf_ps`、`dblf_pi`、`dblf_pd`（printf を使うと soft-double）
6. newlib の `nanofp`、`nanofs`、`dblfs`、`fltfs`、`nanofps`（+21〜31 KB）

FPU のない系列では、スケッチによって 2 番目と 3 番目の一部が入れ替わることがあります。

### 大きさの実測（スケッチ全体、2026-10-28。V003・X035 の float の列は 2026-10-29）

| スケッチ | 系列 | 以前（newlib） | `dbl_*` | `dblf_*` | `flt_*` | `fltf_*` |
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

`Serial.parseFloat()` などの `Stream::parseFloat()` は、読んだ数字から float を作ります。その計算を 3 通りから選べます。正しさを取るか、大きさを取るかの選択です。

| parseFloat | 結果 | 引き込む処理 |
|---|---|---|
| `pd` double | どの入力でも、いちばん近い float（上流の Arduino と同じ） | double の計算 |
| `pi` 整数 | 有効数字 11 桁までの入力なら、いちばん近い float | 64 bit の整数の割り算 |
| `ps` float | 8 つに 1 つほどの入力で、最後の 1 ビットがずれる | float の計算だけ |

- `pi` で 12 桁以上の入力が 1 ビットずれるのは、値が 2 つの float のちょうど中間に、11 桁目より細かいところまで近いときだけです。
- 以前のコアは、Float の選択肢で `0.1` が float の定数になっていました。そのため、`pd` の計算でも、0.27% ほどの入力が 1 ビットずれていました。今はどの選択肢でも double の `0.1` を使います。

### 大きさ（スケッチ全体、2026-10-28。V003・X035 の `flt_*` の 1 行目は 2026-10-29）

| スケッチ | 系列 | `dbl_pd` | `dbl_pi` | `dbl_ps` | `flt_pd` | `flt_pi` | `flt_ps` |
|---|---|---|---|---|---|---|---|
| `parseFloat` の値を `Serial.println(v, 6)` | V003 | 11148 | 13352 | 12588 | 8104 | 6476 | 5732 |
| | X035 | 11132 | 12888 | 12500 | 8272 | 6396 | 6036 |
| | V307 | 16948 | 16992 | 15788 | 11816 | 5320 | 3872 |
| `parseFloat` の値を 1000 倍して整数で表示 | V003 | 8240 | 5836 | 5076 | 8240 | 5836 | 5076 |
| | X035 | 8412 | 5740 | 5376 | 8412 | 5740 | 5376 |
| | V307 | 11552 | 5056 | 3596 | 11552 | 5056 | 3596 |

- 以前のコアでは、`parseFloat` のスケッチは V003 で 11176 B と 10556 B でした（Float の選択肢では 13892 B と 10556 B）。`pd` は、結果を変えずに約 2.3 KB 小さくなりました（`int` から double への変換が 1 回で済むようにしたため）。
- **double の計算がすでに入っているスケッチ**（Double の選択肢で float を表示するもの）では、`pd` がいちばん小さくなります。
- **それ以外**では、`pi` と `ps` が 1.6〜11 KB 小さくなります。

## printf が扱う書式

コアの書式化（newlib の選択肢以外）が扱うものです。

- **変換**：`d i u o x X c s p %`、`%f %F`（メニューで有効にしたとき）
- **フラグ**：`-` `0` `+` 空白 `#`
- **幅と精度**：数字か `*`
  - 精度は、整数なら最低の桁数（`%.5d` → `00042`）、文字列なら最大の文字数（`%.3s`）、`%f` なら小数点以下の桁数です。
- **長さ**：`hh h l z t`

出せないもの（`%e %g %a`、`%lld` などの 64 bit、`%n`）は、**書式の文字列のまま**出します。黙って捨てることはしません。そのあとの引数もずれません（newlib-nano の `%lld` は、後ろの値を読み違えていました）。

### C の printf との違い

- **`%f` の丸め**：`Serial.print(double)` と同じく、最後の桁の 0.5 を足してから切り捨てます。ちょうど半分の値（`123456.78125` を 4 桁）では、C は偶数に丸めて `.7812`、このコアは `.7813` を出します。
- **大きすぎる値**：絶対値が 4294967040 を超えると `ovf` を出します（`Serial.print` と同じ）。
- **`nan` と `inf`**：`%f` では、`nan`、`inf`、`-inf` を出します。`Serial.print` では、今までどおり符号なしの `nan`、`inf` です。
- **newlib の中から呼ばれる printf**：`strftime()`、`asctime()`、`strsignal()` は、newlib の `sniprintf` / `siprintf` を呼びます。コアはこの名前（と `iprintf`、`viprintf`、`vsniprintf`、`vsiprintf`）も定義しているので、これらもコアの書式化で動きます。
  - コアの名前と newlib の名前がぶつかって、リンクに失敗することはありません。
  - `strftime()` の書式は `%s`（1970 年からの秒数）だけが `%lld` を使うので、`%lld` と出ます。ほかの書式は newlib と同じ文字を出します。

### スタックと出力のしかた

- **スタック**：printf 1 回で、以前は 500〜700 B 使っていました。スタックの確保（512 B）をはみ出して、ヒープを壊すことがありました。今は 240〜340 B です。
- **出力のしかた**：行を一時領域に組み立てずに、出力先へ少しずつ書きます。ヒープも使いません。
- **`printf()`、`puts()`、`putchar()`**：newlib の FILE を通さず、すぐに標準出力の先（`Serial` など）へ書きます。
  - この 3 つを混ぜても、順番どおりに出ます。
  - `fputs()`、`fwrite()`、`fprintf()` で `stdout` に書くものは newlib のまま（行ごとのバッファ）です。同じ行で混ぜると、順番が入れ替わることがあります。
- **newlib の選択肢では**、printf の系統は newlib のものに戻ります。

## 確かめたこと

- **PC での試験**（`tests/unit/test_core_format.py`）：メニューの組み合わせ（Float は、整数で行う手順と float で行う手順の両方）の 6 通りそれぞれで、PC の `snprintf` と出力を比べました（380〜500 件）。
- **float の計算を整数で行う手順**（2026-10-29）：float で計算する元の手順と、PC で出力を突き合わせました。
  - 乱数の 200 万個の値 × 桁数 0〜12 × 書式 4 通り（1 億 400 万件）で全件一致
  - float の全ビットパターン（約 43 億）を 2 桁と 6 桁で表示し、全件一致
  - V003、X035、V307 の実機で、6 万件の出力のハッシュが PC と同じ
- **parseFloat の PC での試験**（`tests/unit/test_core_parse_float.py`）：3 通りの計算それぞれで、30 万件の入力を PC の `strtof` と比べました。
  - `pd`：全件一致
  - `pi`：12 桁以上の 3 件が 1 ビットずれ
  - `ps`：約 15% が 1 ビットずれ（2 ビット以上のずれはなし）
- **実機**：V003、X035、V307 で確かめました。
  - Double の選択肢の `Serial.print`、`String(float)`、`dtostrf()` は、以前のコアと 1 文字も変わりませんでした。
  - Float の選択肢の出力は、3 系列で同じでした。
  - `snprintf` の切り詰めと戻り値、`printf()` と `puts()` の順番を確かめました。
  - `parseFloat` の 30 の入力を、3 系列それぞれで `pd`、`pi`、`ps` に通しました。float のビットは、PC で同じ計算をしたものと全部一致しました。
