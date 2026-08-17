# Vendor取込方針

文書状態: 提案

## 目的

vendor更新を「最新EVTを取得してpatchが通れば完了」ではなく、入力と差分を特定できるreview可能な変更にします。

## 基本方針

- build時にvendor archiveをdownloadしない
- 取り込むファイルをallowlistで限定する
- source URL、commit/version、SHA-256、通知文を記録する
- 取り込んだsnapshotはリポジトリに格納し、offline buildを可能にする
- vendor CはCとしてbuildする
- vendor headerを`Arduino.h`から暗黙に公開しない
- EVT full treeをcoreへ複製しない
- updateはbotまたはscriptでPR化し、人間が差分を確認する

## Lock manifest案

```toml
[[source]]
id = "wch-ch32v20x"
url = "https://github.com/openwch/ch32v20x.git"
commit = "<full commit id>"
archive_sha256 = "<hash of the exact downloaded archive, when applicable>"
verified_date = "YYYY-MM-DD"
license_notice = "vendor/notices/wch-ch32v20x.txt"

files = [
  { source = "EVT/EXAM/SRC/Peripheral/inc/ch32v20x.h", sha256 = "<file hash>" },
  { source = "EVT/EXAM/SRC/Peripheral/inc/core_riscv.h", sha256 = "<file hash>" },
]

patches = []
```

実際のformatはgenerator実装前にschemaとして決定します。

Git commit、GitHub生成archiveのbyte hash、allowlist各ファイルのhashは同じ意味ではありません。何を取得物の正本とし、再packされたarchiveをどう扱うかをADRで決めます。少なくとも最終的に取り込んだ各ファイルはhashで追跡します。

## vendor-syncの処理案

1. 新しい一時ディレクトリを作成
2. 固定commit/archiveを取得
3. archive hashとcommitを検証
4. allowlist外のファイルを除外
5. 改行等の許可された正規化だけを実行
6. patchをfail-fastで適用
7. reject、想定外ファイル、通知文欠落があれば失敗
8. inventory、diff summary、provenanceを生成
9. unit、compile、HIL対象を実行
10. 成果物をatomicに更新

## Patch方針

patchは最後の手段とします。各patchに次を要求します。

- 対象source IDとrevision
- 必要な理由
- upstream issue/PRの有無
- 影響するdevice
- patchが必要であることを示すtest
- patchが不要になったことを検出できる条件

旧コアの33 patchはそのまま移植せず、まずfailure modeを分類します。

- constructor/runtime
- weak/default ISR
- C/C++ linkage
- unused parameter/warning
- vendor implementation bug

runtimeとISRは原則としてowned implementationで解決し、headerやvendor bugは回帰testを作成します。

## EVT Compatibility Pack

EVT互換用sourceも同じlock対象にします。exampleごとに必要なsourceをmanifest化し、取得したEVTの全exampleを公開しません。

互換性の表示には少なくとも以下を含めます。

- EVT family/revision
- target SKU/package
- compile-tested toolchain
- HILの有無
- 変更したsourceと理由
- 未対応依存関係

## ライセンスとprovenance

- ルートのMIT Licenseはvendorファイルへ適用しない
- vendorのcopyright、license、attention noticeを保持する
- ArduinoCore-APIのLGPL-2.1条件を配布物で満たす
- releaseに第三者ファイル一覧と対応するsource/revisionを含める
- SBOMまたは同等のmachine-readable inventoryを生成する
- 利用条件が不明なファイルは、確認できるまでrelease archiveへ入れない

WCH EVT全体の再配布・改変条件は未確定です。必要に応じてWCHへ書面で確認します。本書は法的判断を行うものではありません。
