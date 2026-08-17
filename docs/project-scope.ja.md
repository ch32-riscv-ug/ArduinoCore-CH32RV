# プロジェクトの目的とスコープ

## 背景と課題

CH32シリーズはSTM32に似た周辺機能構成を持ちますが、実際の開発環境はWCH EVT、ch32fun、STM32由来の独自SDK、各種HALなどに分かれています。同じ機能でもheader、define、初期化方法、割込み規約が一致しません。

旧ArduinoコアはEVTサンプルをほぼそのまま利用できることを重視していました。一方で、EVT全体をC++ビルドへ露出し、SDK更新のたびにpatchを追加する構成になっていました。

旧コアがEVTへ依存した主な理由の1つはサンプル数の多さでした。しかし、数の多さとArduino利用者にとっての実用性は一致しません。実際の開発ではEVTだけで足りず、datasheet/reference manualを読みながら`ch32fun`等を参考に直接レジスタを操作する場面が残っています。新コアではEVTサンプルの収録数を互換性や完成度の指標にせず、実用的なArduino sketchとlibrary APIで置き換えます。

本プロジェクトでは、Arduino利用者へ安定したAPIを提供しつつ、低レベルSDKの差異をコアの内部または明示的な互換レイヤーへ閉じ込めます。

## 目標

- Arduino IDE 2およびArduino CLIからインストール、ビルド、書き込みできる
- Arduinoの基本APIと標準的なライブラリAPIを一貫して提供する
- 公開APIのsignatureと挙動は可能な限りArduino標準と`ArduinoCore-API`へ準拠し、移植可能なsketchをCH32固有APIへ不必要に書き換えさせない
- 割込みなど低レベル知識が必要になりやすい機能もArduino APIと実用的なexampleから利用でき、通常利用でEVT APIへ降りる必要をなくす
- exampleは機能数ではなく、実用性、説明、対象SKU、compile結果、実機検証状態で評価する
- exact SKU、package、memory、clock、pin mapごとに正しい成果物を生成する
- CとC++の境界を明確にし、`-fpermissive`に依存しない
- 入力、toolchain、生成物、packageを固定し、オフラインで再現ビルドできる
- 新しいデバイスやEVT更新を、manifestとテストの追加として扱える
- 複数DUTを安全に識別し、自動書き込みとHILを実行できる
- 対応済み、compile-only、未検証を利用者が区別できる

## 初期スコープ案

初期段階では、次のRISC-V CH32系列を設計対象の候補とします。

- 小容量RV32E系
- CH32V1系
- CH32V2系
- CH32V3系
- CH32X0系
- CH32L1系

これはfamily全体の対応宣言ではありません。実際の対応単位は正確な型番とpackageです。

## 初期段階の非目標

- EVTに含まれる全サンプルの無条件な互換
- EVT APIまたはEVT exampleとの互換を初期releaseの完成条件にすること
- すべてのCH32/CH5xx製品への同時対応
- CH32FなどArm系列の初期リリースへの収容
- RTOS、BLE、Ethernet、USB Hostなど大規模stackの最初からの同梱
- ch32fun、EVT、STM32 HAL間の完全なソース互換
- 旧コアとのバイナリ互換
- 独自programmer hardwareを最初のリリース条件にすること

上記は将来の対応を否定するものではなく、最初の安定した縦切り実装から切り離すための境界です。

## 「対応」の定義案

| Tier | 意味 | リリース条件 |
|---|---|---|
| Tier A | 正式対応 | 全リリースでcompile、flash、基本HIL、主要周辺機能を確認 |
| Tier B | 制限付き対応 | nightly compile/HIL、既知の制限を文書化 |
| Experimental | 実験的 | compileまたは一部実機確認のみ。安定性を保証しない |
| Unsupported | 非対応 | 理由または不足している検証を記載 |

family名だけを`Supported`と表示しません。少なくともSKU、package、board、clock、pin map、memory構成を対応表へ含めます。

## 設計原則

1. 公開APIは安定させ、vendor固有APIを暗黙に公開しない
2. 正本を1つにし、重複する設定ファイルは生成する
3. 実測していないハードウェアを正式対応にしない
4. 曖昧な書き込み対象には書き込まない
5. vendor更新は自動mergeせず、差分とテスト結果をレビューする
6. 失敗時にbuild、flash、Serial、波形、fixture情報を追跡できるようにする
7. コードサイズとRAM使用量をAPI互換性と同じく回帰対象にする
8. 第三者コードの由来と利用条件を失わない
9. `ch32fun`を実用上の比較基準とし、同等以上を目標にする。ただし、不明確な利用・再配布条件と引き換えに最適化を追わない
10. silicon仕様はdatasheet/reference manualを一次情報とし、EVTと`ch32fun`は相互確認する参照実装として扱い、通常のArduino利用経路へ露出させない
11. 標準化されていない拡張は、ESP32等の主要Arduino coreで定着した公開APIと利用方法を先に調査し、合理的な既存慣例がない場合だけCH32固有APIを設計する

## プロジェクト識別子

GitHub organizationは`ch32-riscv-ug`です。Arduino packageのpackager IDには、公式性を誤認させる`WCH`ではなく、プロジェクトまたはorganization固有の識別子を用いる案が有力です。実際のpackager名、architecture名、FQBNは未決定です。
