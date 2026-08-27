# ADR-0014: upstreamがバイナリを公開しないtoolは`build-<upstream名>`で自前buildして配る

- Status: Proposed
- Date: 2026-08-27
- Related questions: Q-040、Q-044、Q-054、[ADR-0011](0011-tool-mirror-repository.ja.md)、[ADR-0008](0008-upload-strategy.ja.md)

## Context

[ADR-0011](0011-tool-mirror-repository.ja.md)は、再配布の既定を「しない」とし、
**R-1 構造**(upstreamのアーカイブ構造がconsumerの要求を満たさない)と
**R-2 可用性**(upstreamがGitHub外、またはサーバーが不安定)のときだけ
`mirror-<upstream名>`でミラーする、と決めました。

そこでのミラーは**upstreamの成果物をバイト単位でそのまま転送する**ものです。
詰め直したprobe-rsのWindows zipでさえ「中身がupstreamとバイト一致」を検証していますし、
Consequencesには「**検証を上乗せするわけではない**」と明記されています。

ここへminichlink(ch32fun)を配布物に加える必要が出ました。用途は3つです。

1. probe-rs未対応family(V205 / V407 / X315 / M030)のfallback候補
2. 互換probe 6種とrv003usb BL([ADR-0008](0008-upload-strategy.ja.md)のTier2)
3. `gdbserver-shim`経由のGDB server backend(採否は別途判断)

確認済みの事実(2026-08-27):

- **ch32funはreleaseアセットを一切公開していない。** releaseは2件、添付は
  いずれも0件、最新の`v1.0rc2`が2025-02-01。一方`master`は活発
  (`6c4dd53`、2026-08-24)
- **upstreamにCIはある。** `.github/workflows/minichlink.yml`がLinux / Windows /
  macOSをビルドしてartifactに上げているが、releaseへは添付していない
- ライセンスはMIT
- 生態系の前例: UIAPduinoコアが`minichlink-2982dfd/1.0.0`を配っているが、
  `-l`を`Error: Unknown command l`で拒否する古い世代で固まっている
  ([調査](../upload-and-fixture.ja.md))

つまり**R-1にもR-2にも当たりません。転送すべき成果物がそもそも存在しない**ため、
ADR-0011の枠組みでは扱えません。

## Decision drivers

- ADR-0011の「**理由がないなら再配布しない**」を維持する。理由を増やすのであって、
  既定を緩めるのではない
- **信頼モデルを名前で区別できること。** ミラーと自前buildは照合の対象が違う。
  外から見て取り違えられるのが最悪
- **再現可能であること**(ADR-0011から引き継ぐ)。ただし照合対象は
  upstreamのchecksumではなく**ビルド手順**になる
- **自己完結。** 利用者に追加のruntime installを要求しない
- ライセンス義務を局所化する(1ツール1repository)
- upstreamへpatchを当てない。fork運用を始めない

## Options considered

### A. 配布しない

用途2と3が落ち、probe-rs未対応familyのfallback候補も消えます。ADR-0011の
既定に最も忠実ですが、**既定は「理由がないなら」であって、理由はあります**。

### B. upstreamにrelease assetの添付を依頼し、ミラーへ戻す

筋は最も良い案です。CIは既にあるので、tag時にartifactをreleaseへ上げる差分だけで、
PR 1本の規模です。通ればADR-0011の素直なミラーになり、我々は製造者になりません。

ただし**releaseが1年半止まっており、時期をこちらで握れません**。単独では採れませんが、
**Cと並行して提案し**、実現したらミラーへ移行できる余地を残します。

### C. 自前buildして配る(採用)

公開CIで、pinしたupstream commitからhostごとにビルドして配ります。時期を握れ、
`-l`のような世代差も検査で潰せます。代償として**我々が製造者になります**。

### D. install時にビルドさせる

Arduino Board Managerにその仕組みがありません。

### E. コア本体にvendorする

ADR-0011の「**コア本体のreleaseに混ぜない**」に反します。platformのrelease streamは
利用者が追う対象であり、バイナリをrepositoryへ入れることにもなります。

## Decision

### 1. 再配布理由に`R-3`を追加する

ADR-0011の表に1行足します。既定「しない」は維持します。

| 理由 | 内容 | 例 |
|---|---|---|
| **R-3 不在** | upstreamがバイナリを公開しておらず、転送すべき成果物が存在しない | minichlink(ch32fun。release 2件・添付0) |

R-3を使うのは、**そのtoolが必要だと別途決まっている場合だけ**です。R-3は
「配ってよい理由」であって「配る理由」ではありません。

### 2. 命名は`build-<upstream名>`

`mirror-`と分けます。接頭辞が信頼モデルを表します。

| 接頭辞 | 意味 | 照合の対象 |
|---|---|---|
| `mirror-` | upstreamの成果物を転送する。検証を上乗せしない | upstreamが公開しているchecksum |
| `build-` | **我々がコンパイルする** | **ビルド手順の記録**(再現ビルド) |

`mirror-`を冠すると来歴の表示が逆になります。ADR-0011 §3は
「upstreamの成果物であり我々の著作物ではないことを明示する」「rootに自前のLICENSEを
置かない」としていますが、自前buildでは**ビルド環境はこちらの著作物**なので、
rootのLICENSEは自repositoryのファイルに適用され、releaseの中身には適用されない、と
書き分ける必要があります。

1ツール1repositoryはADR-0011のままです。最初のものは
[`build-minichlink`](https://github.com/ch32-riscv-ug/build-minichlink)。

### 3. 取り込みは手動、採用も手動

ADR-0011 §4は「取り込みは自動(定期ポーリング)、採用は手動」ですが、
**R-3のrepositoryでは取り込みも手動**にします。upstreamにreleaseが無く
pollする対象が存在せず、commitごとにreleaseを切れば雑音にしかならないためです。

代わりに週次で`master`が最新ビルドより進んだことを**issueで報せるだけ**にします。
採用が手動なのはADR-0011と同じです。

### 4. 照合の根拠はビルド手順の記録

upstreamのchecksumが存在しないため、`versions/<version>.json`に
**第三者が再現できるだけの情報**を残します。upstream commit、依存ライブラリの
versionとchecksum、hostごとのコンパイラとフラグ、結果の動的依存。

### 5. sourceは無改造

patchを当てたくなったらupstreamへPRを出します。fork運用は始めません。
これを崩すと、ADR-0011が避けたかった「維持コストの発生源」になります。

## Consequences

- **我々が製造者になります。** ADR-0011の「検証を上乗せするわけではない」が
  R-3のrepositoryでは成立せず、ビルドの正しさに責任を持つことになります
- **upstream checksumとの照合ができません。** 代わりに再現ビルドで照合します。
  第三者による検証のハードルは上がります
- **version採番を自前でやることになります。** minichlinkには自前のversionが無く、
  `VERSION`文字列はMakefileが計算する自身のsourceのSHA-1です
- **依存ライブラリのライセンスがこちらに来ます。** minichlinkはMITですが、
  staticリンクするlibusbはLGPL-2.1-or-laterで、§6の再リンク可能性を満たす説明が
  要ります(ビルド手順の記録がそのまま根拠になります)
- 接頭辞が2つになり、org一覧の読み方を1行説明する必要があります
- **upstreamがreleaseを出せばミラーへ戻せます**(選択肢B)。そのとき`build-`を畳んで
  `mirror-`へ移すかは別途判断します
- upstreamのCIが既にあるため、ビルドレシピをゼロから起こす必要はありません

## Validation

- 6 hostすべてでBoard Manager installが通ること(`install-test`)
- **`-l`を持つことをCIで検査すること。** UIAPduinoコアの前例があり、これが無いと
  serial指定ができない世代を配ってしまう
- 動的依存に配布対象の共有ライブラリが出てこないこと(Linux `ldd`、macOS `otool -L`)
- アーカイブが単一rootディレクトリを持つこと
- 同じupstream commitからの再ビルドで同じ成果物が得られること
- 既存tagの再発行と、既存`versions/<version>.json`の上書きが拒否されること
- 初releaseの前に**実機で1回**、書き込み・`-T`・`-G`が通ることを記録すること

## References

- [ADR-0011: 再配布が必要なtoolは1ツール1repositoryでミラーする](0011-tool-mirror-repository.ja.md)
- [ADR-0008: 書き込みdefaultはWCH-LinkE](0008-upload-strategy.ja.md)
- [build-minichlink 初版仕様書](https://github.com/ch32-riscv-ug/build-minichlink/blob/main/docs/design.ja.md) — R-3の具体化。実測値の出どころもここ
- [upload-and-fixture](../upload-and-fixture.ja.md) — UIAPduino同梱minichlinkの世代差
- [ch32fun](https://github.com/cnlohr/ch32fun) / [upstreamのCI](https://github.com/cnlohr/ch32fun/blob/master/.github/workflows/minichlink.yml)
