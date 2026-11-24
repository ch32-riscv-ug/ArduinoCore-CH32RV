# スケッチを小さくする

CH32V003 のような 16 KB の石で、スケッチを収めるための選び方です。ツールメニューの設定、出力の経路、
コードの書き方の順に、効き目を数字で並べます。大きくなった原因を調べる手順は
[Flash 使用量の調べ方](flash-size.ja.md) にあります。

数字は 2026-11-24 に、ch32rv-toolchain 23.1.6-1、このコアの main で測った flash / RAM のバイト数です
（`llvm-size -A` の `.text` などの合計）。系列やほかのコードとの組み合わせで変わるので、目安として使って
ください。

## 何もしないときの大きさ

| スケッチ | V003 | X035 |
|---|---|---|
| 空の `setup()` / `loop()` | 480 / 8 | 504 / 12 |
| Blink（`pinMode()`、`digitalWrite()`、`delay()`） | 592 / 8 | 684 / 16 |
| `Serial.print()` で数を出す | 1960 / 188 | 2516 / 212 |

## ツールメニュー

### Optimize

既定の「Small (-Oz) with LTO」がほとんどの場合でいちばん小さくなります。

| 例 | 既定（-Oz, LTO） | -msave-restore | -Os | LTO なし |
|---|---|---|---|---|
| SerialPacketReceiver（V003） | 2660 | 2680（+0.8 %） | 3048（+15 %） | 6424（2.4 倍） |
| I2CScanner（V003） | 6156 | 6084（−1.2 %） | 6848（+11 %） | 11256（1.8 倍） |
| ButtonKeyboard（X035） | 19348 | 18972（−1.9 %） | 22388（+16 %） | 26512（1.4 倍） |
| ButtonKeyboard（V203） | 19676 | 19216（−2.3 %） | 22328（+13 %） | 27744（1.4 倍） |

- **-msave-restore**（「Smallest for large sketches」）: 関数の出入りでのレジスタの退避を共通の手続きに
  まとめます。6 KB くらいから 1〜2 % 小さくなりますが、関数の呼び出しが遅くなり、3 KB に満たないスケッチでは
  かえって少し大きくなります。あと少しで収まらないときに試すものです。
- **-Os / -O2 / -O3**: 速さのために大きくなります。
- **LTO なし**: 使わない部分を落とせないので、大きく増えます。小さくしたいときに選ぶものではありません。

### Floating Point と parseFloat

既定は「Float (32-bit) + printf %f」です。`Serial.print(float)` は、このメニューの Float と Double で
大きさが大きく変わります。

| スケッチ | Float, %f なし | Float + %f（既定） | Double + %f |
|---|---|---|---|
| `Serial.println(v, 2)`（V003） | 3532 | 3528 | 6200 |
| `Serial.printf("%.2f\n", v)`（V003） | 4384（%f は出ない） | 5404 | 9336 |

- `Serial.print(float)` は、%f の有無で変わりません（コアの固定小数点の処理を使う）。Double にすると
  double の計算を全部取り込むので、約 2.7 KB 増えます。
- printf の `%f` を使わないなら、「Float (32-bit)」（%f なし）で printf の分が約 1 KB 減ります。
- `Stream::parseFloat()` を使うなら、parseFloat のメニューの「Float」がいちばん小さくなります
  （8 回に 1 回、最後の桁が 1 つずれる）。

### 端子の割り当て（USART・I2C・SPI）

同じ種類の周辺を 1 つも使わなければ、Auto のままでも費用は 0 です。1 つでも使うと、ほかの Auto の
インスタンスは `setPins()` や端子つきの `begin()` の移り先として表が残ります。使わないものを
「Disabled」にすると、その分が消えます。

| 例 | 全部 Auto | 使わないものを Disabled |
|---|---|---|
| X035 で `Serial` だけ（USART2〜4 が使える石） | 2496 / 208 | 2220 / 188 |
| V203 で `Wire` だけ（I2C2） | 4516 / 152 | 4224 / 148 |
| V307 で `SPI` だけ（SPI2・SPI3） | 2520 / 64 | 2304 / 52 |

Disabled にすると、`Serial`、`Serial1` … の名前が詰まります（有効な USART を番号順に並べた名前のため）。

### Stack Size・Stack Check

Stack Size は RAM の予約で、flash は変わりません。Stack Check（On）は、あふれを見張る処理のぶん flash が
増えます（`Serial` のスケッチで V003 +244 / +4、X035 +240 / +4）。小さくしたいときは既定の Off のままにします。

## 出力の経路

デバッグ用の出力は、UART の `Serial` より小さくできます。どれもデバッガ（WCH-LinkE や OEP のプローブ）を
つないだまま使います。

| 経路 | V003 | X035 | 向き | 使える条件 |
|---|---|---|---|---|
| `Serial`（UART） | 1960 / 188 | 2516 / 212 | 双方向 | UART の端子 |
| `SerialSDI` | 1036 / 36 | 1020 / 40 | 石 → PC だけ | WCH-LinkE の SDI print に対応した石 |
| `SerialRTT` | 1316 / 380 | 1588 / 384 | 双方向 | RAM のリング（ここでは RAM が多い） |
| `SerialDMDATA` | 1492 / 52 | 1464 / 56 | 双方向 | デバッガ |
| `SerialDMSeq` | 2188 / 76 | 2168 / 80 | 双方向 | デバッガ。通し番号と CRC で欠けを見つける |

- いちばん小さいのは `SerialSDI` です（V003 で `Serial` より約 900 B、RAM 150 B 少ない）。返事を受け取る
  必要がなければ、これで足ります。
- `SerialDMSeq` は欠けや重なりを見つけるための作りで、V003 では `Serial` より少し大きくなります。
- デバッグの経路は、走っている石に接続するときにクロックが組み直されることがあり、V103 / V205 では
  レジスタが 1 つ壊れることがあります（[系列ごとの注意点](chip-caveats.ja.md)）。シリアルモニターでは、
  ツールメニューの Monitor Source で経路を選びます（[デバッグ出力](debug-output.ja.md)）。

## コードの書き方

`Serial` に数を出すスケッチ（V003 で 1960 B）と比べた増え方です。

| 書き方 | V003 | X035 | 増え方（V003） |
|---|---|---|---|
| `Serial.print("count "); Serial.println(n);` | 1960 | 2516 | 基準 |
| `Serial.printf("count %u\n", n);` | 4552 | 4868 | +2.6 KB |
| `String s = "count " + String(n); Serial.println(s);` | 3164 | 3968 | +1.2 KB と heap |
| `Serial.println(v, 2)`（`float v`） | 3528 | 4060 | +1.6 KB |
| `Serial.printf("%.2f\n", v)` | 5404 | 5724 | +3.4 KB |

- **`printf()` より `print()` / `println()`**: printf の書式の解釈だけで 2.6 KB です。printf を 1 か所でも
  使うと、この分が入ります。
- **`String` を避ける**: 連結のたびに heap を使い、RAM の少ない石では断片化も心配になります。`print()` を
  続けて呼べば、同じ出力になります。
- **`float` を避ける**: 浮動小数の計算と表示で 1.6 KB 増えます。値を 100 倍の整数で持ち、整数部と
  小数部を分けて出すと、浮動小数の部品が入りません。`double` はさらに大きくなります。
- **同じ機能は 1 つの書き方にそろえる**: `print()` と `printf()`、`float` と `double` を混ぜると、
  両方の部品が入ります。

## 測り方

自分のスケッチで確かめるときは、ビルドの成果物を残して `llvm-size` で見ます（手順は
[Flash 使用量の調べ方](flash-size.ja.md)）。メニューを変えて比べるときは、FQBN のオプションで渡せます。

```sh
arduino-cli compile -b ch32-riscv-ug:ch32rv:CH32V003:opt=ozltosr,rtlib=flt --build-path /tmp/b <sketch>
llvm-size -A /tmp/b/<sketch>.ino.elf
```
