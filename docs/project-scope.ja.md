# プロジェクトの対象

## 対象

ArduinoCore-CH32RV は、WCH の CH32 RISC-V マイコンを Arduino IDE と arduino-cli から
利用できるようにする platform です。

- Generic board は silicon series 単位で提供する
- 型番ごとの flash、RAM、package を Part Number メニューで選択できる
- Arduino 標準 API と、CH32 固有機能に必要な同梱ライブラリを提供する
- compiler、書き込みツール、Board Manager 配布物を再現可能に固定する
- 対応状況を「ビルド可能」「書き込み可能」「実機確認済み」に分けて表示する

個別系列と型番の状態は [support-status.ja.md](support-status.ja.md)、API ごとの状態は
[peripheral-support.ja.md](peripheral-support.ja.md) が正本です。

## 対象外

- Arm 系の CH32F
- 無線 stack を必要とする CH32V208W、CH58x、CH59x 等の無線 SoC 機能
- WCH EVT / SPL 互換 API
- コア内蔵 RTOS
- ベンダ IDE プロジェクトとのバイナリ互換
- センサ、表示器、ファイルシステム等の汎用デバイスドライバ

これらを必要とする機能は、Arduino API または独立ライブラリの境界で実装します。
コアへベンダ API の互換層を追加しないのは、系列差と SDK 更新を利用者 API から隔離するためです。

## 対応の意味

「対応」は一つの状態ではありません。

1. 生成データがあり、対象 FQBN をビルドできる
2. 同梱 `ch32rv` が対象を識別し、安全に書き込める
3. 実機でコアの動作を確認している

README や board 名だけで 3 を暗示せず、[support-status.ja.md](support-status.ja.md) に明記します。
