# ADR-0011: 再配布が必要なtoolは1ツール1repositoryでミラーする

- Status: Proposed(方針は2026-08-20にmaintainer承認済み。残る未決は末尾)
- Date: 2026-08-20
- Related questions: Q-054、[ADR-0002](0002-toolchain-distribution.ja.md)、[ADR-0008](0008-upload-strategy.ja.md)

## Context

arduino-cliはBoard Managerのtool archiveに**単一のroot directory**を要求します。
probe-rsのLinux/macOS `.tar.xz`はそれを満たしますが、Windows `.zip`は7ファイルが
root直下に並ぶ平坦な構造で、installが次で失敗します。

```text
Cannot install tool ch32-riscv-ug:probe-rs@0.32.0:
  searching package root dir: files in archive must be placed in a subdirectory
```

確認済みの事実(詳細は[調査](../research/probe-rs-archive-layout.ja.md)):

- この非対称は[cargo-dist](https://github.com/axodotdev/cargo-dist)の意図的な規約
  ("for compatibility/legacy reasons")であり、probe-rs固有ではない
- arduino-cli側の緩和は[#325](https://github.com/arduino/arduino-cli/issues/325)が
  `conclusion: declined`で終了しており望めない
- **indexからWindows entryを削っても回避できない**。entryの無いhostでは
  `no versions available for the current OS`でinstallごと失敗する(実験で確認)

つまりupstreamのアーカイブをそのまま指す限りWindowsでは通りません。

この判断はprobe-rsに限りません。[ADR-0008](0008-upload-strategy.ja.md)は今後
wchisp(GPL-2.0)やWCH OpenOCDの併用を計画しており、後者はWCHのサーバー配布です。
org内には既に同種の前例があり(`WCH-common`のデータシート、
`MounRiver_Studio_Community_miror`)、**再配布が必要になる場面は繰り返し発生します**。
そのたびに個別判断するのではなく、判断基準と置き場所の規約を先に決めます。

## Decision drivers

- **理由がないなら再配布しない。** ライセンスの種類によらず、増やしたくない
- **版ずれを起こさない。** 手動publishは他のtoolと版がずれる。これが最初の再ホスト案
  ([承認状態](../approval-status.ja.md) A-2)の却下理由
- **コア本体のreleaseに混ぜない。** platformのrelease streamは利用者が追う対象
- **ライセンス義務を局所化する。** GPLのものを配るなら対応ソースの提供義務が付く
- upstreamの資産削除・retagに対する耐性(ADR-0002がConsequencesに挙げたリスク)
- 再現可能であること。第三者がupstreamから同じ成果物を作り直して照合できる

## Decision(提案)

### 1. 再配布するかどうかの基準

**既定は「しない」。** 次のどちらかに当てはまるときだけ再配布します。

| 理由 | 内容 | 例 |
|---|---|---|
| **R-1 構造** | upstreamのアーカイブ構造がconsumerの要求を満たさない | probe-rsのWindows zip(root directoryが無い) |
| **R-2 可用性** | upstreamがGitHub外、またはサーバーが不安定 | WCH配布物(既に`WCH-common`でデータシートを自己ホスト)、MounRiver Studio |

R-2は**先方サーバーへの負荷を避ける意味**もあります。GitHub上で安定して配布されて
いるものは、これに当たりません。

xPack riscv-none-elf-gccは**どちらにも当たらないので再配布しません**
(GitHub、構造は適合、安定)。ADR-0002の直リンク方針を維持します。

**ライセンスは可否の基準ではなく、実施時のコスト**です。必要ならGPLのものも
再配布します。ただしGPLは対応ソースの提供義務を伴うため、後述の1ツール1repositoryが
効きます。

### 2. 1ツール1repository

複数のtoolを1つのrepositoryに混ぜません。

- **ライセンス義務の局所化。** 義務が紐づくのはrepositoryではなく個々の資産だが、
  混在させると外から区別できない。分ければREADMEに一度
  「このrepositoryはGPL-2.0の◯◯を配布しており、対応ソースはここ」と書けば済む。
  wchisp(GPL-2.0)は[ADR-0008](0008-upload-strategy.ja.md)の段階2に入っており、
  仮定の話ではない
- **tagがupstream versionと1:1になる。** 混ぜるとprefixが必要になり、release一覧が
  交互に並ぶ。「upstreamの最新を見て、無ければ作る」自動化もtag = versionが単純
- **保持方針とサイズを分けられる。** probe-rsは1 version約80MB、gccは約2.4GB
- **障害と廃止の隔離。** upstreamが構造を直せばそのミラーはrepositoryごと畳める

CIの重複は、2つ目が出た時点でreusable workflowへ抽出します(先に共通化しない)。

### 3. 命名は `mirror-<upstream名>`

最初のものは **`mirror-probe-rs`**([作成済み](https://github.com/ch32-riscv-ug/mirror-probe-rs))。

- **接頭辞**にするのは、org一覧でミラーが固まり、先頭の1語で正体がわかるため
- 素のupstream名(EVTミラーの流儀)はフォークに見える。`arduino-`を冠すると
  Arduino由来物に見えるうえ、実際にはArduino専用ではない(単一rootを要求する
  consumerなら何でも使える)
- 名前だけに頼らず、descriptionとREADMEで「upstreamの成果物であり我々の著作物ではない」
  ことを明示する。rootに自前のLICENSEを置かない

### 4. 取り込みは自動、採用は手動

| | 場所 | 頻度 |
|---|---|---|
| **取り込み** | ミラー側。upstreamに新versionが出たらreleaseを作る | 自動(定期ポーリング) |
| **採用** | このrepositoryの`tools_probe_rs.json`のversionを上げる | 手動。認定matrix通過が条件([ADR-0002](0002-toolchain-distribution.ja.md)) |

ミラーが新versionを公開しても、こちらがversionを上げるまで**利用者への影響はゼロ**です。
だから取り込みは自動でよく、確認は採用時に一度で済みます。

自動化の条件:

- **append-only。既存tagは絶対に再発行しない。** upstreamが同じversionを差し替えたら、
  上書きせず**失敗させて通知**する。黙って中身が変わるのが最悪
- **古いversionを消さない。** package indexもappend-onlyであり、古いplatform versionが
  pinしているtoolを消すと**過去のplatformがinstallできなくなる**
- `workflow_dispatch`でversion指定の取り込みも用意する(定期ポーリングは「今後」しか
  拾わないため、既存versionの遡り取り込みに要る)

### 5. 成果物の作り方

1. upstreamのreleaseを定期確認する
2. 各hostのアーカイブを取得し、**upstreamが公開しているchecksumと照合**する
3. 構造を整える
   - 要修正のもの(probe-rsのWindows zip): root directoryを付けて詰め直す。
     **決定的**(entry順・timestamp・圧縮方式を固定)にし、同じ入力から必ず同じchecksumが出る
   - それ以外: **バイト単位でそのまま**再配布する(checksumがupstream公開値と一致し、
     照合が容易)
4. upstream versionに対応するtagでreleaseを作り、全hostの成果物を添付する
5. Arduino tool定義fragmentも成果物として出す。upstreamのURLとchecksumを
   `upstream*`フィールドに残し、来歴を追えるようにする

構成は既存の`WCH-common`(`update.sh` + `documents.json` + CI)に寄せます。

## Consequences

- **Windowsでinstallできるようになる。** CIの`install-test`にwindows-latestを戻せる
- upstreamの資産削除・retagから利用者が切り離される(ADR-0002が挙げたリスクの緩和)
- 第三者がupstreamから成果物を再現して照合できる
- **こちらがprobe-rsバイナリの再配布者になる。** Apache-2.0/MITの表示義務は
  アーカイブ内のLICENSEファイルで満たすが、releaseの説明にも出所を明記する
- **検証を上乗せするわけではない。** upstreamのchecksumを記録して転送するだけで、
  upstreamが汚染されればミラーも汚染される。ただし採用が手動なので、利用者に届くまでに
  人のレビューが1回挟まる
- repositoryが増える。CIとsecretの管理対象が増える
- upstreamが構造を直せば(cargo-distへのopt-in設定依頼など)、Windowsの詰め直しは
  不要になる。R-2の価値は残るので、ミラー自体を畳むかは別途判断する

## Validation

- ミラーが出したアーカイブでBoard Manager installが3 OSで通ること(`install-test`)
- 詰め直しが決定的であること(2回実行してchecksum一致)
- 詰め直し後の中身がupstreamとバイト一致すること(ファイル単位のSHA-256比較)
- upstreamの新versionが出たときに自動で追従すること
- 既存tagの再発行が拒否されること

## 実装状況(2026-08-20)

`mirror-probe-rs`の中身は用意済みで、ローカルで次を確認しました。

- 詰め直しが必要なのはWindows zipのみで、**判定は実物の検査による**(tarballは素通し)
- 詰め直したアーカイブがarduino-cliでinstallでき、`probe-rs.exe`が
  `{runtime.tools.probe-rs.path}`直下に来る
- 詰め直しが決定的(2回実行してchecksum一致)
- 中身がupstreamとバイト一致(7ファイルすべてSHA-256一致)、LICENSE 2種も同梱
- 素通し分のchecksumがupstream公開値と一致
- 取り込み済みversionの差し替えを検知してジョブが落ちる
- ミラーの成果物でLinuxのclean install → 上書きなしcompile → upgrade/rollbackが通る

**未実施**: ミラーのreleaseの公開。これができるまで`tools_probe_rs.json`が指すURLは
404で、CIの`install-test`にwindows-latestを戻せません。

## 未決

- 定期確認の間隔と、失敗時の通知先
- このADRを`Accepted`にするか(方針自体は2026-08-20に承認済み)

## References

- [Windowsでinstallできない件の調査](../research/probe-rs-archive-layout.ja.md)
- [tools/index/README.ja.md](../../tools/index/README.ja.md)
- [承認状態](../approval-status.ja.md)
