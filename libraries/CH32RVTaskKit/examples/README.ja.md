# CH32RVTaskKit サンプルの使い方

[English](README.md)

起動条件で動く短い仕事の例です。どれも `loop()` の `runOnce()` だけが仕事を動かします。[BasicWork](BasicWork/README.ja.md) から始め、[ButtonBlinkConsole](ButtonBlinkConsole/README.ja.md) で仕上げます。考え方は [スケッチの組み立て方](../../../docs/sketch-guide.ja.md) で順に説明しています。

## ボードと接続

どの系列でも動きます（CH32V003 を含む）。ログは `Serial` のハードウェア UART へ 115200 bps・8N1 で出します。端子は各 `.ino` の定数で変えます。

## 一覧

| 例 | 学ぶこと |
|---|---|
| [BasicWork](BasicWork/README.ja.md) | 起動条件があるときだけ動く 2 つの仕事です。 |
| [WorkFromInterrupt](WorkFromInterrupt/README.ja.md) | 割り込みは仕事をせず、仕事を頼みます。 |
| [WorkStats](WorkStats/README.ja.md) | どの仕事が遅いかを数字で見ます。 |
| [ButtonBlinkConsole](ButtonBlinkConsole/README.ja.md) | ボタン、点滅、UART のコマンドを 1 つのスケッチにまとめます。4 種類の起動条件と、割り込みを止める 1 か所。 |
