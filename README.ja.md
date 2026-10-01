# ArduinoCore-CH32RV

[English](README.md)

WCH CH32マイコン向けの、コミュニティ管理によるArduinoコアを新規設計するプロジェクトです。

> [!IMPORTANT]
> β版です（0.0.x）。本番前のため、版の間で破壊的変更が入ることがあります。

## インストール

Arduino IDE の「ファイル → 基本設定 → 追加のボードマネージャのURL」に次を足し、ボードマネージャで
「CH32 RISC-V」を入れます。toolchain と書き込みツール（ch32rv）も一緒に入ります。

```text
https://ch32-riscv-ug.github.io/ArduinoCore-CH32RV/package_ch32-riscv-ug_index.json
```

arduino-cli なら:

```sh
arduino-cli core install ch32-riscv-ug:ch32rv --additional-urls https://ch32-riscv-ug.github.io/ArduinoCore-CH32RV/package_ch32-riscv-ug_index.json
```

書き込みは、IDE の port で WCH-Link の serial port か `wchlink://…`、OEP の probe なら `oep://…` を選んで
「書き込み」を押すだけです（programmer の選択は要りません）。変更の一覧は [CHANGELOG.md](CHANGELOG.md)。

ボードメニューには全系列が `Generic <系列>` として並びます。どの系列・型番を実機で確かめたか、どれがまだ
書き込めないかは [対応状況](docs/support-status.ja.md) にあります。

本プロジェクトはWCHの公式プロジェクトではありません。プロジェクト名の`CH32`は対象デバイス系列を示すもので、公式性を示すものではありません。

## 目的

- 現代的なtoolchainとArduino環境で継続的にビルドできること
- 正確な型番・package単位で、対応範囲と検証状況を説明できること
- 外部SDKやEVTの変更をArduino APIから隔離すること
- 通常利用者がEVT APIへ降りずに使える、実用的なArduino APIとexampleを提供すること
- 小容量SKUの制約を明示しながら、可能な限りArduino標準APIへ準拠すること
- ビルド、書き込み、実機試験、Board Manager配布を自動化すること
- 初期実装の速さよりも、更新時の作業量と故障解析コストを小さくすること

## 現在の設計方針

現時点では、次の構成を有力案としています。まだ最終決定ではありません。

- 小さなArduinoコアを既定経路とし、EVT Compatibility Packは初期release要件にしない
- Arduino API境界には固定版の`ArduinoCore-API`を利用する
- startup、CRT、vector table、linker scriptは本プロジェクトが所有する
- WCH由来コードは必要なファイルだけをversion・hash・由来付きで取り込む
- デバイス、ボード、CI対象を宣言的manifestから生成する
- 書き込みフロントエンドと実際のprogrammer backendを分離する
- host test、compile test、HIL、logic analyzerを組み合わせる

初期実装の対象はCH32のRISC-V系列を想定しています。CH32FなどのArm系列、無線SoC、RTOSを含む最終的な対応範囲は未決定です。

## ビルドメニュー

| メニュー | 既定 | 内容 |
|---|---|---|
| Part Number (`pnum`) | `ANY` | 型番。`ANY`はseries内で最小のflash/RAMを宣言するので、どの型番にも載る |
| C Runtime Library (`rtlib`) | `nano` | `printf("%f")` / `scanf("%f")` を使えるようにするか。WCH 公式 core や STM32duino と同じ名前 |

**`printf`の既定では`%f`が何も出力しません。** ランタイムがnewlib-nanoで、
浮動小数点の変換が入っていないためです([ADR-0004](docs/adr/0004-runtime-and-cxx.ja.md))。
Arduinoの他コアから来ると必ず引っかかる点なので明記します。

必要なときはメニューを選んでください。増える flash は CH32X035 の実測で、`Newlib Nano + Float Printf`(`nanofp`)が
約 21 KB、`+ Float Scanf`(`nanofs`)が約 24 KB、両方(`nanofps`)が約 31 KB です。16 KB の型番(CH32V003 など)には
どれも入りません。

`Serial.print(1.5, 2)`はコアの実装で、`printf`とは無関係に常に動きます。

## リポジトリ構成

リポジトリのルートがそのままArduino platformディレクトリです。開発時は
`<sketchbook>/hardware/ch32-riscv-ug/ch32rv` へこのルートをsymlinkして使います。

```text
platform.txt          ビルドrecipe
boards.txt            生成物(tools/generate)。手編集禁止
cores/arduino/        コア本体
  api/                ArduinoCore-API 1.5.2 の無改変snapshot(LGPL-2.1-or-later)
variants/<SERIES>/    pin定義とlinker script(いずれも生成物)
tools/                generate(boards/ld生成)、index(Board Manager index)、vendor(取込検証)
tests/                compile matrix、startup等価性、sizebench
docs/                 設計文書、ADR、実験記録
vendor/               上流のpin。ArduinoCore-API / TinyUSBのsnapshotと、
                      ch32-device-data.lock.toml(boards.txtとvariants/が
                      どの表から生成されたかを記録している唯一の場所)
```

release archiveへ入るのは`platform.txt` / `boards.txt` / `cores` / `variants` /
`libraries`だけです(`tools/index/gen_index.py`の`PLATFORM_ENTRIES`)。

## ドキュメント

次の作業を始める場合は、まず[引継ぎメモ](docs/handoff.ja.md)を参照してください。

- [ドキュメント一覧](docs/README.ja.md)
- [目的とスコープ](docs/project-scope.ja.md)
- [アーキテクチャ案](docs/architecture.ja.md)
- [タイマー基盤の再設計案](docs/timer-design.ja.md)
- [旧コアの監査結果](docs/legacy-audit.ja.md)
- [外部エコシステム調査](docs/ecosystem.ja.md)
- [vendor取込方針](docs/vendor-policy.ja.md)
- [toolchain方針](docs/toolchain.ja.md)
- [フラッシュサイズの削り方](docs/flash-size.ja.md)
- [開発ワークフローとテスト計画](docs/development-workflow.ja.md)
- [テスト計画（入口と実行手順）](tests/TEST_PLAN.ja.md)
- [ロードマップ](docs/roadmap.ja.md)
- [未決定事項](docs/open-questions.ja.md)

## ライセンス

本プロジェクト自身のコードと文書は[MIT License](LICENSE)です。

第三者成果物には別のライセンスが適用され、ルートのMIT Licenseはそれらを再ライセンスしません。

| path | 由来 | License |
|---|---|---|
| `cores/arduino/api/` | [arduino/ArduinoCore-API](https://github.com/arduino/ArduinoCore-API) tag `1.5.2` の無改変copy | **LGPL-2.1-or-later**([同梱LICENSE](cores/arduino/api/LICENSE)) |

固定commitと全ファイルのSHA-256は[`vendor/arduino-core-api.lock.toml`](vendor/arduino-core-api.lock.toml)に記録し、CIの`api-sync` jobがupstreamとのbyte一致を毎PR検証します(方針: [ADR-0009](docs/adr/0009-arduinocore-api-import.ja.md))。

WCH EVTその他の再配布・改変条件は未確定で、現時点で取り込んでいるファイルはありません。
