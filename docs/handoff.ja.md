# 引継ぎメモ

文書基準日: 2026-08-19

## 現在地

`ArduinoCore-CH32`は、古くなった[`arduino_core_ch32_riscv_noneos`](https://github.com/ch32-riscv-ug/arduino_core_ch32_riscv_noneos)をそのまま修復するのではなく、長期保守を前提に新規設計するために作成されました。

現在のリポジトリには、設計文書に加えて[事前調査](research/README.ja.md)(startup、EVT構造、SKU/board構造、toolchain)、[環境整備計画](infrastructure.ja.md)、[実験記録](experiments/0001-xpack-multilib-smoke.ja.md)(0001〜)、および実機なしで検証済みのprototype([統合startup+等価性検証ハーネス](../prototypes/startup/README.ja.md)、[最小platform](../prototypes/platform/README.ja.md)、[boards.txt/ld generator](../prototypes/generator/README.ja.md))があります。コア本体(実API)の実装、CI、Board Manager packageの公開はまだありません。

device schema、8 sample record、validator、詳細引継ぎは独立[`ch32-device-data`](https://github.com/ch32-riscv-ug/ch32-device-data) repositoryへ移動しました。境界は[device dataの配置と利用方針](device-data.ja.md)を参照してください。

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
- 書き込み先は複数台から決定的に指定できなければならず、USB PPPSで他portを切断して1台に見せる方式は採用しない
- 既存toolで要件を満たせない場合は新規toolも開発対象にできる。Arduino専用に閉じず、`ch32fun`等からも利用できる独立toolを目標にする
- 初期fixtureはWCH-LinkE 1台、8ch/8MHz運用のFX2LP系logic analyzer 1台、DUT 1台とし、Arduino `Serial`にはWCH-LinkE内蔵の物理UARTを使用する
- logic analyzerのchannelとDUT pinの割当、adapter connector、電源構成は初期board選定後に決める
- device databaseは独立`ch32-device-data` repositoryを正本とし、Arduino coreを固定versionのconsumerにする。配置だけを決定し、schema・対象family・対応SKU・release形式は未決定のままとする
- 対象は特定familyではなく全CH32ファミリ(11 family / 27 series / 103型番)とする
- 実機が使えない期間は、実機なしで進められる環境整備を優先する
- バッチ・定期処理はGitHub Actionsで実行する(OSSのためActionsコストは制約にしない)。静的ページはGitHub Pagesで公開できる。必要に応じてリポジトリを分離する
- 本プロジェクトは`ch32-riscv-ug`(ユーザーグループ。WCH公式ではない)配下で運営する。旧コアのpackage index/名前空間との互換は保たず「旧のは捨てる」
- Board Manager indexは、コアが1つの間は本repoから直接配信する。同一名前空間のコアが増えたらlang-ship方式(統合index repo+release完了kick)へ移行する

## 有力な提案

- デフォルトコアを小さくし、EVT互換を初期release要件から外す。将来需要が確認できた場合だけ別artifactとして再評価する
- `Arduino.h`からEVTの全headerを公開しない
- `ArduinoCore-API`を固定versionで利用する
- exact SKUとpackageを正本にして`boards.txt`等を生成する
- `ch32_riscv_tools`を将来の固定data releaseのviewer・生成物consumerにする
- startup、CRT、vector、linkerをプロジェクト側で管理する
- vendorソースはcommit、SHA-256、allowlist、通知文を固定する
- uploaderは安定したフロントエンドを持ち、backendを交換可能にする
- compiler forkはversion追従コストが高いためdefault前提にせず、書き込みtoolやESP32系/RP2040 programmerは既存toolで解決できない課題に対する候補として評価する
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
- schema prototypeはCH32V003F4P6、CH32X035F8U6、CH32M030C8T7のcomplete package/pin-functionを表現でき、非連続register bit、reserved selector値、OPA input selection、QFN exposed pad、MV/HV I/O、内蔵Rdを扱えます
- CH32M030とCH32V003のreference manualにはpin selector表とregister説明間の矛盾が見つかり、recordへ根拠と未実機確認を記録しています
- signal名のcanonical化、silicon/package/exact SKUの正規化、pinを持たないinternal route、verification粒度は未決定です

詳細は[旧コア監査](legacy-audit.ja.md)、[外部調査](ecosystem.ja.md)、[テスト戦略](test-strategy.ja.md)を参照してください。

## 次に始める作業

実機なしフェーズの進捗と残作業は[環境整備計画](infrastructure.ja.md)のW-1〜W-7が正本です。2026-08-19時点でW-1(xPack toolchain検証)、W-2(統合startup等価性検証、13バリアント)、W-3(暫定FQBNでのBlink compile)、W-4のboards.txt/ld生成(26/26 SKU compile matrix)、W-5(index生成+clean install検証)が完了しています。

CI([workflow](../.github/workflows/ci.yml): startup-equivalence/generated-sync/compile-matrix/install-test)は**ubuntu/macos/windowsの3 OSでall green確認済み**(2026-08-19)。crt0の`.init_array`呼び出しは実装済み・静的検査済み(実行確認はHIL待ち)。

W-7のsize回帰gate(baseline完全一致チェック)も実装済み。R-09の計測は[実験0006](experiments/0006-newlib-size-baseline.ja.md)で完了(default=newlib-nano、%fはmenu opt-inが結論)。実機なしフェーズの計画項目はhost test(Q-016)を残して完了。

1. コア本体の実装開始(ArduinoCore-API統合、pinMode/digitalWrite/millis/delay/Serial。入口はQ-010=ArduinoCore-APIの固定versionとLGPL配布方法、Q-013=内部HAL contract)
2. host test基盤(Q-016。コア実装と並走)
3. (実機が使えるようになったら)`probe-rs`でflash/verify/reset/read-uidを検証し、「Blinkをcompile → flash → Serial READY → GPIO波形判定」までを1本通す

最初の2機種候補は、制約の厳しいRV32E機と実用的なRV32I系を1つずつ選ぶ案です。小容量側はCH32V003F4P6、実用側はCH32X035、CH32M030、CH32V203系などが候補ですが、所有実機、USB PDを初期範囲へ含めるか、package、fixture配線を確認してから合意してください。

## 新しいスレッドでの開始文

必要なら、次の内容で作業を再開できます。

> `/home/mt/dev_wch/ArduinoCore-CH32`でCH32向けArduinoコアを新規設計しています。まず`docs/handoff.ja.md`と`docs/device-data.ja.md`を読み、device data作業では兄弟repository `/home/mt/dev_wch/ch32-device-data`の`docs/handoff.ja.md`も読んでください。配置は独立repositoryを正本とすることで決定済みですが、schema、対象family、初期対応SKU、consumer lock、toolchain認定条件は未決定です。これらは順に合意してください。旧コア、EVT、公式PDF、手製pin表は参照のみとし、新repositoryへ無断でコピーしないでください。
