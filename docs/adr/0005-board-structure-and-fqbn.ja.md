# ADR-0005: Generic board は series 単位、型番は menu で選ぶ

- Status: Accepted

## 決定

- FQBN は `ch32-riscv-ug:ch32rv:<board>` とする
- Generic board は `Generic <SERIES>` と表示し、一つの silicon series を表す
- `pnum` menu の先頭に `ANY`、続いて exact part number を置く
- `ANY` は同じ series の最小 flash / RAM を宣言する
- board ID は part menu で変えず、part と series はそれぞれ `CH32RV_PART_*`、`CH32RV_SERIES_*` で渡す
- 実機確認状況を board 名へ埋め込まず、`docs/support-status.ja.md` で管理する
- `boards.txt` と generic variant は device-data から生成する

## 理由

EVT の family 名は利用者が chip 刻印から判断できず、一つの family 内でも vector や memory layout が異なる
場合があります。series 単位なら表示名が刻印と対応し、実行に必要な差分も一意になります。

exact part を知らない場合でも `ANY` で安全な小さい memory layout を選べます。検証状態を board 名から
分離するのは、確認が進むたびに FQBN や表示名を変更しないためです。

architecture を `ch32rv` とするのは、他の CH32 core が使う `ch32v` と区別し、library が別 core の API を
誤って選ばないためです。
