# ArduinoCore-CH32RV

[English](README.md)

WCH CH32 RISC-V マイコン向けの、コミュニティ管理による Arduino コアです。
WCH の公式プロジェクトではありません。

## インストール

Arduino IDE の「ファイル → 基本設定 → 追加のボードマネージャの URL」に次を追加し、
ボードマネージャから「CH32 RISC-V」をインストールします。

```text
https://ch32-riscv-ug.github.io/ArduinoCore-CH32RV/package_ch32-riscv-ug_index.json
```

arduino-cli では次のようにします。

```sh
arduino-cli core install ch32-riscv-ug:ch32rv \
  --additional-urls https://ch32-riscv-ug.github.io/ArduinoCore-CH32RV/package_ch32-riscv-ug_index.json
```

toolchain と書き込みツール `ch32rv` も一緒にインストールされます。書き込み時は IDE の port で
WCH-Link、OEP probe、または対応ボードのブートローダを選び、「書き込み」を実行します。

系列ごとのビルド、書き込みの実装範囲は
[系列ごとの実装範囲](docs/support-status.ja.md) を参照してください。

## 設計上の前提

- 対象は CH32 の RISC-V 系列です。Arm 系の CH32F と無線 SoC は扱いません。
- Arduino API の基盤には固定した ArduinoCore-API 1.5.2 を使用します。
- startup、CRT、vector table、linker script と周辺機能の実装はこのプロジェクトが所有します。
- 型番、メモリ、pin、割り込み等のデータは `ch32-device-data` の固定 revision から生成します。
- 通常ビルドはネットワークへアクセスせず、生成物を使用します。
- コアはベアメタルです。RTOS は同梱しません。
- 書き込みと monitor は同梱の `ch32rv` に一本化します。

判断理由は [ADR](docs/adr/README.ja.md) に、利用者が必要な有効な仕様だけを記録しています。

## ビルドメニュー

| メニュー | 既定 | 内容 |
|---|---|---|
| Part Number (`pnum`) | `ANY` | 型番。`ANY` は系列内の最小 flash/RAM を宣言するため、同じ系列のどの型番にも載せられる |
| C Runtime Library (`rtlib`) | `nano` | `printf("%f")` / `scanf("%f")` の浮動小数点変換を追加するか |

既定の newlib-nano では `printf("%f")` と `scanf("%f")` の浮動小数点変換は入りません。
必要な場合だけ `nanofp`、`nanofs`、`nanofps` を選びます。これらはruntime codeを大きく
増やし、小容量flashには収まらない場合があります。正確な使用量はbuild size reportで確認します。
`Serial.print(1.5, 2)` はこのメニューに依存しません。

## リポジトリ構成

リポジトリのルートが Arduino platform ディレクトリです。

```text
platform.txt          ビルド、書き込み、monitor、debug の recipe
boards.txt            生成物。手編集禁止
cores/arduino/        コア本体と ArduinoCore-API の固定コピー
variants/             pin 定義と linker script。生成物
libraries/            同梱ライブラリと examples
tools/generate/       ch32-device-data からの生成
tools/index/          Board Manager 配布物の生成
tools/vendor/         第三者ソースの固定内容を検証するツール
vendor/               上流 revision、hash、license の lock file
docs/                 現行仕様、利用手順、ADR
tests/                実機不要の検査と新テストの雛形
tests-legacy/         旧テスト・ベンチ設定
```

Board Manager の配布物は allowlist 方式で作られます。正確な対象は
`tools/index/gen_index.py` の `PLATFORM_ENTRIES` が正本です。

## ドキュメント

- [ドキュメント一覧](docs/README.ja.md)
- [系列ごとの実装範囲](docs/support-status.ja.md)
- [ペリフェラル対応状況](docs/peripheral-support.ja.md)
- [デバッグ出力](docs/debug-output.ja.md)
- [デバッガ](docs/debugger.ja.md)
- [フラッシュ使用量の調べ方](docs/flash-size.ja.md)
- [設計判断](docs/adr/README.ja.md)

## ライセンス

本プロジェクトが作成したコードと文書は [MIT License](LICENSE) です。
第三者成果物には各成果物のライセンスが適用されます。

| path | 由来 | ライセンス |
|---|---|---|
| `cores/arduino/api/` | ArduinoCore-API 1.5.2 | LGPL-2.1-or-later |
| `libraries/TinyUSB/src/` | TinyUSB 0.21.0 の固定コピー | MIT |

固定 revision とファイル hash は `vendor/*.lock.toml` に記録し、検証ツールで一致を確認します。
