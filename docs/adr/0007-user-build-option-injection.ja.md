# ADR-0007: `build.extra_flags` を利用者向けに予約する

- Status: Accepted

## 決定

`build.extra_flags` は platform と生成器から設定せず、利用者の `--build-property` または
`boards.local.txt` による注入専用とします。core に必須の define と option は専用 property に置きます。

## 理由

Arduino の build property は代入で上書きされます。core の必須値を `build.extra_flags` に混ぜると、利用者が
一つの `-D` を追加しただけで必須値が消えます。用途を分離することで、利用者の拡張と platform の不変条件を
同時に保てます。

## 追記: スケッチの `build_opt.h`（2026-10-14）

スケッチと同じフォルダーに置いた `build_opt.h` を、そのビルドのすべての C / C++ / アセンブラのファイル
（スケッチ・コア・ライブラリ）にコンパイラオプションとして渡します。arduino-cli 自体にはこの機能が無いので、
platform.txt のビルド前フックでビルドディレクトリへコピーし（無ければ空のファイルを作る）、gcc の `@ファイル`
で読ませます。ESP32 コアと同じ方式です。

- 専用の property（`build.opt.flags`）に置き、`compiler.<lang>.extra_flags` とは分けます。利用者が
  `--build-property` でそれらを上書きしても `build_opt.h` が外れないようにするためです。
- フックは OS ごとに別の書き方（Linux / macOS は bash、Windows は cmd）なので、CI のパッケージ導入試験
  （`tools/index/install_check.py`）で3つの OS それぞれについて、スケッチとコアの両方に届くことを確かめます。

**位置付け**: コアとライブラリは、`build_opt.h` を使わなくても普通に使える作りを本来の形とします。
ペリフェラルの選択はボードメニューとスケッチ上の宣言で済むようにし、`build_opt.h` はバッファの大きさの
ような「ビルド時に決まってしまう値」を、必要な人が変えるための逃げ道として扱います。
