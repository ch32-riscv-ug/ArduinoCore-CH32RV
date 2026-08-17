# 引継ぎメモ

文書基準日: 2026-08-17

## 現在地

`ArduinoCore-CH32`は、古くなった[`arduino_core_ch32_riscv_noneos`](https://github.com/ch32-riscv-ug/arduino_core_ch32_riscv_noneos)をそのまま修復するのではなく、長期保守を前提に新規設計するために作成されました。

現在のリポジトリには設計文書だけがあり、コア実装、生成器、CI、Board Manager packageはまだありません。

## 決定済み

- 新規リポジトリ名は`ArduinoCore-CH32`とする
- 旧リポジトリはlegacyとして保持し、新リポジトリで設計をやり直す
- 初期コストが増えても、更新・試験・リリースの継続コストを下げる
- 最初に仕様、対応範囲、検証方法を明文化する
- 日本語文書は`.ja.md`、成熟した利用者向け英語文書は`.md`とする。現時点の英語文書はトップREADMEだけとする
- 通常のArduino利用は公開Arduino APIで完結させ、EVT APIやvendor headerへ降りることを要求しない
- EVT example数ではなく、実用的でcompile/HIL状態を追跡できるArduino exampleの完成度を目標にする
- 公開APIは可能な限りArduino標準と`ArduinoCore-API`へ準拠する。小容量SKUの未対応機能は明示し、標準APIの意味をCH32都合で変更しない
- 標準外の拡張は主要Arduino coreで定着した慣例を優先し、合理的な既存形がない場合だけCH32固有APIを追加する

## 有力な提案

- デフォルトコアを小さくし、EVT互換を初期release要件から外す。将来需要が確認できた場合だけ別artifactとして再評価する
- `Arduino.h`からEVTの全headerを公開しない
- `ArduinoCore-API`を固定versionで利用する
- exact SKUとpackageを正本にして`boards.txt`等を生成する
- startup、CRT、vector、linkerをプロジェクト側で管理する
- vendorソースはcommit、SHA-256、allowlist、通知文を固定する
- uploaderは安定したフロントエンドを持ち、backendを交換可能にする
- `probe-rs`をWCH-Link backendの第一候補として実機認定する
- host/compile/HIL/logic analyzer/replayを段階的にCIへ組み込む

これらはまだ正式なADRではありません。[未決定事項](open-questions.ja.md)と実験結果を確認して決定してください。

## 初期調査で分かったこと

- 旧リポジトリはコアそのものではなく、WCH EVTを取得してArduino packageを生成するoverlayです
- 追跡63ファイル中33ファイルがEVT依存patchでした
- EVTの取得URL、入力checksum、生成環境が固定されておらず、patch失敗も厳密に判定していません
- `Arduino.h`からEVT header群をC++へ露出することが、C++ linkage/header修正を繰り返す主因です。constructorやweak ISRには別のruntime設計問題もあります
- board定義はfamily中心で、exact SKU、package、startup、linker、pin、memory設定の対応が不十分です
- 公式WCH Arduino coreのBoard Manager版もGCC 8.2.0と古いOpenOCDに依存しています
- `ch32fun`は活発で重要な参照実装ですが、Arduino APIやEVT互換の土台として直接forkするものではありません
- EVTだけでは実用実装に足りず、datasheet/reference manualと`ch32fun`等を見ながら直接レジスタを操作する場面が残るため、その必要をArduino API、library、検証済みexampleで減らすことが新コアの価値です
- 2026年7月の`probe-rs 0.32.0`でCH32/WCH-Link対応が大きく改善しましたが、対象機種すべてでの実機認定はまだです
- WCH-LinkはUSB serialを恒久的な個体IDとして信用できない可能性があります。serial単独には依存せず固定lane/topologyを使い、読める機種ではDUT UIDも照合する案を検証します
- `host-arduino-core`と`I2CDeviceDB`には、テスト構造とlogic analyzer制御に再利用できる設計があります

詳細は[旧コア監査](legacy-audit.ja.md)、[外部調査](ecosystem.ja.md)、[テスト戦略](test-strategy.ja.md)を参照してください。

## 次に始める作業

次のセッションでは、いきなり多数のデバイスを実装せず、以下を順に進めるのが妥当です。

1. [project-scope.ja.md](project-scope.ja.md)と[open-questions.ja.md](open-questions.ja.md)をレビューする
2. 最初の正確なSKUと評価ボードを2種類程度選ぶ
3. device/board manifestの最小schemaを決める
4. toolchain候補でstartup、constructor、割込み、LTO、サイズを比較する
5. `probe-rs`で選定ボードのflash/verify/reset/read-uidを検証する
6. 「Blinkをcompile → flash → Serial READY → GPIO波形判定」までを1本通す

最初の2機種候補は、制約の厳しいRV32E機と標準的なRV32I系を1つずつ選ぶ案です。例としてCH32V003F4P6とCH32V203系が挙がっていますが、所有している実機、package、fixture配線を確認してから確定してください。

## 新しいスレッドでの開始文

必要なら、次の内容で作業を再開できます。

> `/home/mt/dev_wch/ArduinoCore-CH32`でCH32向けArduinoコアを新規設計しています。まず`docs/handoff.ja.md`と関連文書を読み、決定済み事項・提案・未決定事項を区別してください。次は初期対象SKU、device/board schema、toolchain認定条件を固めたいです。旧コアは参照のみとし、新リポジトリへ無断でコピーしないでください。
