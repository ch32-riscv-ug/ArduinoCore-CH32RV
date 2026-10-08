# ADR-0010: Pad 名を安定した sparse pin 番号で表す

- Status: Accepted

## 決定

CH32 の pad は `PA0`、`PB3` のような名前で公開し、値は次の形にします。

```c
pin = ((port + 2) << 5) | bit
```

したがって `PA0` は 64、`PC13` は 141 です。0〜63 は generic board では無効とします。

基板に Arduino 形式の番号が印刷された製品 board だけは、variant の表で 0〜63 を pad へ変換します。
pad 名の値はどの board でも変えません。

## 理由

CH32 は複数 port を持ち、同じ series でも package によって露出 pad が違います。密な連番にすると package ごとに
番号と variant が変わり、`ANY` part を提供できません。port と bit を埋め込めば datasheet の名前と一対一になり、
変換 table なしで GPIO register を求められます。

port に 5 bit を使うのは、PA23 等の bit 15 を超える pad が存在するためです。0〜63 を空けるのは、製品 board の
印刷番号と pad encoding を衝突させないためです。
