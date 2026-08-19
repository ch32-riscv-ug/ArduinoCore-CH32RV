# ADR-0007: build.extra_flagsはコアで使わず、ユーザー注入専用に予約する

- Status: Proposed
- Date: 2026-08-19
- Related questions: Q-013(コアのフック要件)、Q-015

## Context

Arduinoの慣例プロパティ`build.extra_flags`は、利用者がarduino-cliの`--build-property`やboards.local.txtからビルドオプションを注入する事実上の標準経路になっている。多くのコアは未使用で空けているが、**ESP32はコア内部で使ってしまっており**、利用者が上書きするとコアの必須defineが消える事故が起きる。同種の事故を構造的に防ぐ必要がある。

## Decision drivers

- arduino-cli/CIからの`--build-property build.extra_flags=...`によるオプション注入を壊さない
- コアの必須フラグはユーザー上書きで消えてはならない
- ライブラリは-Dを注入できないというArduino仕様(ADR-0006のconfig問題とも関連)

## Decision

- **`build.extra_flags`はコア・boards.txt・generatorのいずれからも値を設定しない**(platform.txtで空定義し、全compile recipeの末尾側に`{build.extra_flags}`を展開する)。用途はユーザー注入(`--build-property`、boards.local.txt)専用
- コアが必要とするdefine/flagは専用プロパティ(`compiler.*`、`build.startup_defines`等)に置き、`build.extra_flags`と衝突させない
- boards.txt生成器は`build.extra_flags`を出力しないことを不変条件とする
- 将来、スケッチ単位の恒久的なオプション注入としてSTM32duino方式の`build_opt.h`(GCC @file)フックをコアへ実装する(ADR-0006のconfigフックと同時)
- **CIで恒久ガード**する: compile-matrixに「`--build-property build.extra_flags=-D<マーカー>`を渡し、sketch側の`#ifndef`で到達を検証する」テストを含める。コアが将来extra_flagsを消費し始めたら検出される

## Consequences

- 利用者はコアを壊さずに任意フラグを注入できる
- コア開発者はextra_flagsに依存できない(専用プロパティを増やす)
- boards.local.txtでboard/menu単位の恒久注入も可能(仕様準拠)

## Validation

- CI compile-matrixのextra_flags注入テスト(test_compile.sh内)

## References

- [Arduino platform仕様](https://docs.arduino.cc/arduino-cli/platform-specification/)、ESP32コアのextra_flags内部使用(R-16/R-03調査時に確認)
