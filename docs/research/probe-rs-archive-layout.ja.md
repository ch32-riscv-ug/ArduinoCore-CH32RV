# R-18: probe-rsのアーカイブ構造とarduino-cliの要求

状態: **調査済み。[ADR-0011](../adr/0011-tool-mirror-repository.ja.md)で方針決定。**
関連: [tools/index/README.ja.md](../../tools/index/README.ja.md)、
[TODO](../todo.ja.md)、[承認状態](../approval-status.ja.md)

`arduino-cli`のtool archive要求とprobe-rsの配布物が噛み合わず、Windowsだけ
Board Manager installが失敗する。原因の特定と、取りうる方針の比較。

## 確定している事実

| # | 事実 | 確認方法 |
|---|---|---|
| F-1 | arduino-cliはtool archiveに**単一のroot directory**を要求する | 平坦なzipとroot付きzipを作って両方installし、前者だけが`files in archive must be placed in a subdirectory`で落ちることを確認 |
| F-2 | probe-rsのLinux/macOS `.tar.xz`はroot directoryを持ち、Windows `.zip`は持たない | `tar tJf` / `unzip -l`、および`dist-manifest.json` |
| F-3 | この非対称は[cargo-dist](https://github.com/axodotdev/cargo-dist)の**意図的な規約** | cargo-dist book「Archive Contents」 |
| F-4 | arduino-cli側の緩和は望めない | [arduino-cli#325](https://github.com/arduino/arduino-cli/issues/325)が`conclusion: declined`で終了 |
| F-5 | **indexからWindows entryを削っても回避できない** | entryの無いhostでinstallすると`no versions available for the current OS`で失敗する。実験で確認 |
| F-6 | cargo-distは現役 | 最終pushが2026-08-19、open issue 332 |
| F-7 | probe-rsのPowerShell installerは既に`.tar.*`を`tar xf --strip-components 1`で処理する | `probe-rs-tools-installer.ps1` v0.32.0 |
| F-8 | 同installerのコメント曰く、Windows同梱のtarは実質`.tar.gz`のみ対応 | 同上 line 302 |

**F-5が効きます。** probe-rsをtool依存として宣言している限り、
**upstreamのアーカイブをそのまま指したままWindowsでinstallを成功させる方法はありません**。
残るのは「upstreamに構造を直してもらう」(案X / 案Y)か、
「構造を整えたものをこちらが配る」(案Z)かの二択です。

## 却下済みの案

| 案 | 却下理由 |
|---|---|
| 再パッケージして本repositoryで再ホスト | 手動publishになり他のtoolとversionがずれる。運用が持たない(2026-08-20) |
| `windows-archive = ".tar.gz"`へ**変更**を依頼 | `.zip`が無くなるのはupstreamにとって破壊的すぎる。既存利用者の参照が壊れる(2026-08-20) |

## 採用: tool配布専用repositoryで自動ミラー(案Z)

`ch32-riscv-ug`配下にtool配布専用のrepositoryを置き、upstreamのreleaseを
**自動で**取り込んで構造を整えて再配布する。
→ [ADR-0011](../adr/0011-tool-mirror-repository.ja.md)、実体は
[`mirror-probe-rs`](https://github.com/ch32-riscv-ug/mirror-probe-rs)。

## 検討したが出さないことにしたupstream依頼

**2026-08-20、upstreamへの依頼は出さないと決定。** 以下は記録として残す。

### 案X: zipの中にroot directoryを作る(cargo-distへ)

cargo-dist側に**opt-inの設定**を追加してもらい、zipにもtarball同様のroot directoryを
付けられるようにする。probe-rsはその1行を設定するだけになる。

- 宛先: **cargo-dist**(probe-rsではない。規約はcargo-dist側のもの)
- 既定を変えずopt-inにすれば**既存利用者への破壊がゼロ**
- cargo-dist自身がこの非対称を "for compatibility/legacy reasons" と書いており、
  直す動機の説明がしやすい
- 恩恵はcargo-distを使う全プロジェクトに及ぶ。Arduinoに限らず「単一root必須」の
  consumerは他にもある
- upstream側のコスト: PowerShell installerが`Expand-Archive`後にroot directoryを
  剥がす処理を要する(tar側の`--strip-components 1`に相当)
- 弱点: **2段階**(cargo-distが入れる → probe-rsが採用する)。時間がかかる
- 既存issueは見つからず(2026-08-20検索)

### 案Y: root付きアーカイブを追加で出す(probe-rsへ)

`.zip`はそのままに、root付きのアーカイブを**追加**で出してもらう。
cargo-distには[`[[dist.extra-artifacts]]`](https://github.com/axodotdev/cargo-dist/blob/main/book/src/reference/config.md)
(0.6.0〜)があり、任意のコマンドで作ったファイルをreleaseへ追加upload できる。

- 宛先: **probe-rs**(1プロジェクトで完結。速い)
- `.zip`は残るので既存利用者への影響なし
- 弱点1: **実現性が未確認。** `extra-artifacts`の`build`はglobal artifactsのjobで
  走るため、その時点でWindowsのzipが手元にあるか不明。要確認
- 弱点2: そのartifactの利用者が事実上こちらだけになる。**probe-rsに我々のための
  bespokeなスクリプトを維持してもらう**形になり、依頼として弱い
- 弱点3: checksumやdist-manifestへの載り方が通常のarchiveと異なる

## 比較

| | 案X (cargo-dist) | 案Y (probe-rs) |
|---|---|---|
| 破壊 | 無し(opt-in) | 無し(追加) |
| 段数 | 2段階 | 1段階 |
| 速さ | 遅い | 速い |
| 実現性 | 設定追加として素直 | **未確認**(CIのjob順序) |
| 依頼の筋 | 全ユーザーの利益。上流自身が legacy と認めている | 我々専用の成果物 |
| 維持 | cargo-distの正式機能 | probe-rsのbespokeスクリプト |

## 結論

ミラー(案Z)で解決する。upstream依頼は出さない。

upstreamの非対称は残るが、ミラー側は**詰め直すかを実物の検査で決める**ため、
将来upstreamがroot付きのWindowsアーカイブを出せば、こちらを変更しなくても
自動的に素通しへ戻る。
