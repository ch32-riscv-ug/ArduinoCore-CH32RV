# 環境整備計画(実機なしフェーズ、repository分離、自動化)

文書基準日: 2026-08-19
文書状態: 方針は決定済み、個別の構成は提案

## 決定済みの方針

- 実機が使えない期間は、**実機なしで進められる環境整備を優先**する
- 初期コストが増えても、**運用コスト最小・自動化最大**の構造にする(既存方針の再確認)
- バッチ・定期処理は**GitHub Actions**で実行する。OSSプロジェクトのためActionsの実行コストは制約にしない
- 静的ページは**GitHub Pages**で公開できる
- 単一リポジトリへ集約せず、**必要に応じて適切なリポジトリへ分離**する

## 実機なしで完了できる作業(workstream)

[roadmap](roadmap.ja.md)のPhase 0〜1のうちhardware非依存の部分と、Phase 3で必要になる自動化基盤を前倒しで作る。実機が使えるようになったら、flash/HIL部分だけを後から差し込む。

| ID | 作業 | 実機なしで検証できる範囲 | 関連 |
|---|---|---|---|
| W-1 | toolchain認定(静的部分) | multilib実物確認、family別march/mabiのcompile smoke、ELFサイズ計測(実行なし) | Q-020, Q-026, [R-04](research/toolchain-distributions.ja.md) |
| W-2 | 統合startup/vector/linker prototype | 全familyのassemble成功、ELF検査(section配置、vector table内容、シンボル)をtest化 | Q-012, [R-01](research/startup-files.ja.md) |
| W-3 | 最小platform prototype | 暫定packager/FQBN、platform.txt/boards.txt骨格、arduino-cliでのBlink compile(symlink方式) | Q-015, [R-15](research/local-install-and-test-env.ja.md) |
| W-4 | device-data→生成パイプライン | tables CSVからboards.txt/variant/ld/vector includeの生成、生成物のdiffレビュー運用 | Q-011, Q-014, [R-03](research/board-variants-and-menus.ja.md) |
| W-5 | package index生成+install検証 | index JSON生成、ローカルHTTP(またはPages)経由のclean install→compileをCIで再現 | Q-054, R-15方式B |
| W-6 | host test基盤 | ArduinoCore-API host build、HAL mockによるcontract test | Q-016 |
| W-7 | size regression基盤 | empty/Blink/Serial相当のELFサイズをCIで記録・比較(実行はしない) | Q-051 |

実機が必須で保留になるもの: flash/verify/reset(Q-040系)、HIL、割込みlatency実測(Q-021)、clock実測、fixture整備(Q-050)。

## Repository分離案(提案)

| repository | 役割 | CI(GitHub Actions) | GitHub Pages |
|---|---|---|---|
| **ArduinoCore-CH32**(本repo) | コア実装、platform定義、生成物(boards.txt/variant/ld)のcommit先、設計docs | compile matrix(全board×代表sketch)、host test、ELF検査、生成物の再生成差分check、vendor-sync検証 | (必要なら)support matrix等の生成ページ |
| **ch32-device-data**(既存) | デバイスデータ正本、tables、validator | schema check、table build、release作成 | データviewer、tables公開 |
| **(当面)index配信は本repoから** | Board Manager index JSONの生成・配信(単一コアの間はコア直配信が最もシンプル) | index生成・検証(checksum/size)、過去entry不変check、install smoke | package_*.jsonの配信 |
| (条件付き新規)merged index repo | 同一packager名前空間の複数コアを統合したindexの配信 | 各コアのrelease完了をkickに統合indexを再生成 | 統合package_*.jsonの配信元 |
| (将来)uploader tool repo | probe-rs frontend等の独立tool | tool自体のbuild/test/release | − |

index配信の方針(前例に基づく決定済みの方向):

- 前例: [tanakamasayuki/lang-ship-arduino-core](https://github.com/tanakamasayuki/lang-ship-arduino-core)は、`host-arduino-core`と`native-arduino-core`の**統合package indexをGitHub Pagesで配信**し、各ソースrepositoryのrelease workflow完了をkickにGitHub Actionsで統合indexを再生成している
- 統合indexが必要になる理由: Arduino CLIには**同一packager名前空間のindexが複数あると共存できないケース**があるため、同じ名前空間のコアが複数になったらindexを1つへマージする必要がある
- したがって: **コアが1つの間は本repoから直接配信する(シンプル)**。同一名前空間のコアが増えた時点でlang-ship方式(merged index repo+release kick)へ移行する
- 本プロジェクトは**ch32-riscv-ug**(ユーザーグループ。WCH公式ではない)配下であり、lang-ship系とは別の名前空間。旧CH32コア(同じch32-riscv-ug配下)のindex/名前空間との衝突は「**旧のは捨てる**」方針で解消する
- generatorの置き場所は判断ポイント(後述)。データ正本はch32-device-data、生成物の消費者は本repoという境界は[ADR-0001](adr/0001-device-data-repository.ja.md)の通り

## 生成物の扱い(提案)

- boards.txt、variant、ld、vector include等の生成物は**本repoへcommitする**(build時生成にしない)。理由: 差分がPRでレビューできる、利用者のBoard Manager installがgenerator実行に依存しない、offline buildが成立する([vendor-policy](vendor-policy.ja.md)と同じ原則)
- CIは「device-dataの固定releaseから再生成して、commit済み生成物と一致するか」を検証する(乖離したらfail)。更新はbot PRで差分を人間がレビュー
- 手編集の禁止はesp8266方式(生成物ヘッダに警告+CIで手編集PRを拒否)を採用する

## GitHub Actions設計(提案)

| workflow | トリガ | 内容 |
|---|---|---|
| compile-matrix | PR/push | xPack GCCをキャッシュし、全board×代表sketchをarduino-cliでcompile。ELFサイズをartifact化 |
| elf-inspect | PR/push | 統合startupのELF検査(section、vector、entry、gp/sp) |
| host-test | PR/push | host build+unit test |
| generated-sync | PR/push | device-data固定releaseから再生成→commit済み生成物と比較 |
| package-install | nightly/release前 | index生成→ローカルHTTP配信→新規data dirへcore install→compile(R-15方式B) |
| vendor-check | 定期 | EVTミラーの更新検出→差分PR作成(自動mergeしない) |
| size-regression | nightly | 代表sketchサイズの推移記録、閾値超過で通知 |

- toolchain(1 host約400MB)は`actions/cache`でキャッシュし、xPack GitHub Releasesへの毎回DLを避ける
- Actionsは全ホスト(ubuntu/windows/macos、必要ならarm64 runner)でpackage-installを検証できる

## GitHub Pages用途候補(提案)

- package index JSONの配信(URLの安定性が利用者導線になる)
- support matrix・対応SKU一覧(device-data+CI結果から生成)
- size regressionやcompile状況のダッシュボード(静的生成)

現状(2026-08-19確認): 本repoのPagesは有効化済みで、`/`全体が https://ch32-riscv-ug.github.io/ArduinoCore-CH32/ で公開されている。index配信に使う場合のURL候補は `https://ch32-riscv-ug.github.io/ArduinoCore-CH32/package_ch32-riscv-ug_index.json`。JSONやarchiveを確実に生配信するには`.nojekyll`の追加(Jekyll処理の無効化)を推奨(未適用。判断ポイント)。

## 実機なしフェーズの作業順序(提案)

1. **W-1**: xPack multilib確認とfamily別compile smoke(linux-x64分は[実験0001](experiments/0001-xpack-multilib-smoke.ja.md)で完了。Windows/macOSはW-6のCI matrixで実施)
2. **W-2**: 統合startup prototypeを書き、全family分をassemble+ELF検査するローカルscriptを作る(**13バリアントで完了**、[実験0002](experiments/0002-unified-startup-poc.ja.md)と[tests/startup/](../tests/startup/README.ja.md)。V103/H417とCI化が未了)
3. **W-3**: 暫定packager/FQBNを決め、symlink方式でBlink compileを通す(**完了**、[実験0003](experiments/0003-arduino-cli-platform-poc.ja.md)と[tests/compile/](../tests/compile/README.ja.md)。暫定FQBN=`ch32-riscv-ug:ch32v:CH32V00X:pnum=...`)
4. **W-4**: tablesからV00X familyのboards.txt/ld/variantを生成する最小generatorを作り、W-3のplatformへ接続(**boards.txt+ldは完了**、[実験0004](experiments/0004-boards-generator-poc.ja.md)と[tools/generate/](../tools/generate/README.ja.md)。26/26 SKU compile matrix成功。variant生成とdata lockが未了)
5. **W-5**: index生成とローカルinstall検証をscript化(**完了**、[実験0005](experiments/0005-package-index-install.ja.md)と[tools/index/](../tools/index/README.ja.md)。xPack直リンクtool参照のarchive形式・tool解決を実物確認)
6. **W-6/W-7**: host testとsize計測をCI化し、上記すべてをGitHub Actionsへ載せる(**workflow作成済みで初回実行はall green確認済み**(2026-08-19、ubuntu/macos): [.github/workflows/ci.yml](../.github/workflows/ci.yml)。startup-equivalence(EVTミラーをsparse clone)/generated-sync(boards.txtヘッダのlocked commitでdevice-dataをcheckout)/compile-matrix(size summary付き)/install-testの4 job。その後**windows-latestをcompile-matrixとinstall-testへ追加し、3 OSでall green確認済み**(2026-08-19)。**W-7のsize回帰gateも実装済み**: 26 SKUのBlinkサイズを[sizes_baseline.json](../tests/compile/sizes_baseline.json)に固定し、compile-matrixが完全一致を検証(toolchain固定下ではビルドは決定的。意図的な変化は同一PRで`check_sizes.py --update`により再生成してレビュー)。host test(W-6の一部)のみ未了)

この順序は「後の工程が前の工程の生成物を消費する」依存関係に沿っており、各段階の成果物がそのままCIのtestになる。

## 判断ポイント

- generator(boards.txt/variant/ld/vector生成)を本repoに置くかch32-device-dataに置くか。提案は**本repo**(生成物の形式はArduino都合で決まり、データ側を汚さない)
- ~~CH32コアをlang-ship系と同一packager名前空間に入れるか~~ → **解決**: 本プロジェクトはch32-riscv-ug配下の独立名前空間。開発用の暫定packagerは`ch32-riscv-ug`とし(Q-015)、公開時のIDと表示名はQ-017のADRで確定する
- Pagesの公開単位(repoごとに持つか、index repoに集約するか)
- 暫定packager/architecture ID(Q-015)。install検証を始めるW-3までに仮決めが必要
- arm64 runner(linux-arm64/macos arm64)をcompile matrixへ含める範囲
