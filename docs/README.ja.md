# ドキュメント

このディレクトリには、利用方法、保守に必要な仕様、変更時に必要な設計判断だけを置きます。
過去の調査、実験、作業メモ、完了済み TODO は Git 履歴で参照します。

文書と実装が矛盾する場合は、生成元、コード、設定、lock file の順で事実を確認し、同じ変更で文書を直します。

## 利用者向け

| 文書 | 内容 |
|---|---|
| [support-status.ja.md](support-status.ja.md) | 系列ごとのビルド、書き込みの実装範囲 |
| [upload-routes.ja.md](upload-routes.ja.md) | 書き込み経路、デバッグ線、書き込みモードへの入り方 |
| [peripheral-support.ja.md](peripheral-support.ja.md) | ペリフェラル別の実装範囲と制約 |
| [usb.ja.md](usb.ja.md) | USB Device・Host・PD、全38サンプルの入口、配線・操作と USB 書き込み |
| [debug-output.ja.md](debug-output.ja.md) | UART、SDI、RTT、DMDATA、DMSEQ の使い分け |
| [debugger.ja.md](debugger.ja.md) | `arduino-cli debug` と GDB server の設定 |
| [uiapduino-hid-upload.ja.md](uiapduino-hid-upload.ja.md) | UIAPduino の HID 書き込みと復旧 |
| [flash-size.ja.md](flash-size.ja.md) | flash 使用量の調査と削減 |

各同梱ライブラリの API と例は `libraries/<name>/README.md` / `README.ja.md` に置きます。

## 保守者向け仕様

| 文書 | 正本とする内容 |
|---|---|
| [project-scope.ja.md](project-scope.ja.md) | 対象と非対象 |
| [architecture.ja.md](architecture.ja.md) | コンポーネント境界と依存方向 |
| [device-data.ja.md](device-data.ja.md) | `ch32-device-data` の固定と生成物の更新方法 |
| [board-layer-rules.ja.md](board-layer-rules.ja.md) | series、part、製品 board、sketch の責務 |
| [timer-design.ja.md](timer-design.ja.md) | SysTick/TIM の所有権と競合規則 |
| [usb-superspeed-design.ja.md](usb-superspeed-design.ja.md) | CH32H417 の SS 対応に必要な共通層・ドライバ・Arduino 側の改修と、既存 TinyUSB PR の採用検討 |
| [network-stack.ja.md](network-stack.ja.md) | IP スタックの選定（検討中の提案）：候補の比較、大きさ、系列ごとの段階、H417 の LAN |
| [h417-bench.ja.md](h417-bench.ja.md) | CH32H417 評価ボードで SS を挿したまま書き込みと復旧を回す手順、やってはいけないこと |
| [software-peripherals.ja.md](software-peripherals.ja.md) | SoftSPI / SoftWire 等の採用基準 |
| [toolchain.ja.md](toolchain.ja.md) | compiler と C/C++ runtime の固定方針 |
| [vendor-policy.ja.md](vendor-policy.ja.md) | 第三者ソースの取込、lock、license |
| [../tests/TEST_PLAN.ja.md](../tests/TEST_PLAN.ja.md) | テストの保証対象、設備、環境分離、責務分担 |
| [test-coverage.ja.md](test-coverage.ja.md) | 実装範囲と既存テストの保証・不足 |
| [adr/README.ja.md](adr/README.ja.md) | 現行設計の判断理由 |

生成器と配布ツールの操作は、それぞれ
[`tools/generate/README.ja.md`](../tools/generate/README.ja.md) と
[`tools/index/README.ja.md`](../tools/index/README.ja.md) を参照してください。

## 文書を追加する基準

次のいずれかに当てはまる内容だけを追加します。

- 利用者が API やツールを正しく使うために必要
- コードだけでは表現できない制約や互換性契約
- 将来の変更時に、同じ判断をやり直さないための決定と理由
- 第三者成果物の由来、固定方法、ライセンス上の扱い

調査過程、比較候補の羅列、セッション引き継ぎ、承認待ち一覧、完了済み作業は置きません。
未完了作業は issue、release ごとの差分は GitHub Releases と Git 履歴、再現可能な事実はテストまたは生成器で管理します。
