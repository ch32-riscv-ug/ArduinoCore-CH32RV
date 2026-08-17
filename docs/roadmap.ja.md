# 初期ロードマップ

文書状態: 提案

期間ではなく、検証可能な完了条件で段階を区切ります。

## Phase 0: 設計baseline

作業:

- 初期対象SKU、package、評価boardを選定
- project scopeとsupport tierを確定
- device/board/fixture manifestの最小schemaを作成
- 開発用の暫定packager/architecture/FQBNを決める
- vendor provenanceとライセンス扱いを確認
- toolchain候補と認定matrixを確定
- uploader frontendのcontractを定義
- host testを外部host core、HAL mock、native unitのどれで成立させるか決める
- 最初のADRを作成

完了条件:

- 最初の2 boardについて、CPU、memory、clock、pin、programmer、fixtureが一次資料と実機に対応している
- 未決定事項がADRまたは明示的な保留として記録されている
- 「正式対応」の判定方法がtestで表現されている

## Phase 1: 最小vertical slice

候補は、RV32E小容量機と標準的なRV32I系を1種類ずつです。

作業:

- modern toolchain package
- owned startup/CRT/vector/linker
- ArduinoCore-API integration
- `pinMode`、digital read/write
- `millis`、`micros`、`delay`
- 最小Serial
- ELF/BIN生成
- 1 backendでのflash/verify/reset
- 1 fixtureの独立health check、candidate READY、GPIO capture
- 最初のGPIO `.sr`、decoder、golden corpus、replay test

完了条件:

- clean checkoutから再現buildできる
- constructor、weak handler、memory layout testが通る
- 2つのABI系でBlink/Serialをcompileできる
- 少なくとも1 boardでflashからlogic analyzer判定まで無人実行できる
- 保存したGPIO captureを通常CIで再decodeできる
- code/RAM size baselineが保存される

## Phase 2: Arduino基本周辺機能

作業:

- GPIO interrupt
- PWM/timer
- ADC
- SPI
- Wire/I2C
- pin alternate function生成
- host contractとHIL oracle
- fixture controllerによるGPIO/ADC刺激とSPI/I2C/UART peer

完了条件:

- 各APIの正常系、境界値、error pathをtestできる
- Serial/PWM/SPI/I2Cをlogic analyzerで判定できる
- 対象boardのcapabilityとpin制約が生成文書に反映される

## Phase 3: device展開

作業:

- FPU/上位系列を含む代表device追加
- CH32X0/L1等の異なる周辺構成追加
- 全board/example compile matrix
- Tier A/B fixture拡充
- package install CI

完了条件:

- board追加がmanifest、variant、test targetの追加として完結する
- family固有の手編集`boards.txt`やstartup wrapperを必要としない
- support matrixがmanifestから生成される

## Phase 4: 実用exampleとArduino API完成度

作業:

- GPIO interrupt、timer、ADC、Serial、SPI、Wireを組み合わせた実用example
- 割込み、pin remap、buffer overflow、timeout、bus recoveryなど分かりにくい挙動の説明とexample
- exampleごとの対象capability、必要配線、期待結果、検証状態を表すmanifest
- 全対象boardでのcompile matrixと、代表boardでのHIL
- EVTを参照して確認した低レベル挙動のtest化
- compile/HIL済みArduino example catalog

完了条件:

- 通常利用者がEVT APIやvendor headerを使わず主要周辺機能を利用できる
- 各exampleの対象SKU、必要配線、期待動作、compile/HIL状態を追跡できる
- vendor更新によるArduino APIとexampleの回帰が自動で検出される

EVT Compatibility Packはrelease roadmapへ含めません。上記を達成した後にも利用者需要が確認できた場合だけ、別artifactとして再評価します。

## Phase 5: 2.0 release準備

作業:

- migration guide
- Board Manager beta/stable index
- Linux、Windows、macOS package install
- SBOM、第三者notice、checksum
- release runbook
- rollback/撤回方針

完了条件:

- release artifactを一度buildし、その同一artifactを全試験と公開に使用する
- package indexの過去entryを変更しない
- Tier A全fixtureと保存capture replayが通る
- 旧1.4利用者が互換性差分と移行方法を確認できる

## Release後の定常運用

- PR: schema、unit、host、代表compile
- main: 信頼済みHIL smoke
- nightly: 全compile、拡張HIL、size regression
- 定期vendor check: 差分PRを作成するが自動mergeしない
- release candidate: 全Tier A、package install、SBOM/license確認
- hardware failure: fixture healthとして切り分け、core failureと混同しない
