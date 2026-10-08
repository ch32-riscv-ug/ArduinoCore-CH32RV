# ADR-0007: `build.extra_flags` を利用者向けに予約する

- Status: Accepted

## 決定

`build.extra_flags` は platform と生成器から設定せず、利用者の `--build-property` または
`boards.local.txt` による注入専用とします。core に必須の define と option は専用 property に置きます。

## 理由

Arduino の build property は代入で上書きされます。core の必須値を `build.extra_flags` に混ぜると、利用者が
一つの `-D` を追加しただけで必須値が消えます。用途を分離することで、利用者の拡張と platform の不変条件を
同時に保てます。
