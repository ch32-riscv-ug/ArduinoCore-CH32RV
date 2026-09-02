# ch32rv への依頼事項(組み込み側)

文書状態: 提案(依頼一覧は組み込み側の要件整理。個々の実施可否・順序の判断は ch32rv 側)
文書基準日: 2026-09-01

## 前提(決定済み、2026-09-01)

- コアも ch32rv も品質を高めてからリリースする。**probe-rs を同梱した状態ではリリースしない**。コア同梱のアップローダは ch32rv に一本化する。
- ch32rv の Linux 版をコアの実機ベンチで先行ドッグフーディングする(開始はもう少し安定してから)。
- 現行の [platform.txt](../platform.txt) は probe-rs recipe([ADR-0008](adr/0008-upload-strategy.ja.md))のまま。置換時に ADR の追記(または新 ADR)を行う。この文書は置換までに ch32rv 側へ求めるものの整理である。
- [upload-and-fixture.ja.md](upload-and-fixture.ja.md) の「ch32-upload(仮称)」構想は ch32rv が実体化した。recipe 形は ch32rv 側 `docs/cli.ja.md` §5 に定義済みで、現行 platform.txt の制約(probe selector を空にできる 1 変数、`--non-interactive`、進捗抑止)と整合している。
- ch32rv 側のレビュー記録: `../../ch32rv/docs/direction-review-2026-09-01.ja.md`
- **検証開始(2026-09-02 決定)**: v0.2.0 を baseline に検証(ドッグフーディング)を開始。0.x は検証版として使いながら直し、安定したら 1.0 にする。A-2(lock)・A-3(--capture)は未充足のまま開始し、同日付で正式にプッシュ(下表に受け入れ基準)。
- **ドッグフーディング中の運用方針(2026-09-02 決定)**: 変な挙動・足りない機能は**利用者側で回避せず、ch32rv への修正・追加依頼にする**。回避策を恒久化しない(一時的な直列化などは依頼の納品までの暫定に留め、この文書に依頼として残す)。

## 依頼 A: ドッグフーディング開始まで(Linux x64 のみで可)

| # | 依頼 | 根拠(組み込み側の制約) |
|---|---|---|
| A-1 | exit code(cli §3.6)と `--json` envelope を add-only として凍結した版タグを 1 つ切る | ベンチ(tests/manual/smoke)が exit code と JSON に依存する。番号・field の変更が起きない保証がないと切替できない |
| A-2 | **プッシュ済み(2026-09-02)** per-device advisory lock の実装(cli §3.7 仕様どおり: USB serial 単位〔無ければ topology〕の advisory lock を OS runtime dir に置く、`--lock-timeout` 既定 10s、取得失敗は exit 13、異常終了者の stale lock は起動時回収)。受け入れ基準: (1) 同一 probe への並行 `flash` ×2 で一方が待機または exit 13 になり protocol 破損が起きない、(2) monitor 保持中の flash が決定的に振る舞う(黙って混線しない)、(3) kill -9 後の stale lock が次回起動で回収される | ベンチでは monitor(CDC/DMI)と upload が同一 probe へ並行アクセスするのが常態。lock 納品までの直列化は暫定運用であり、IDE 相当フロー(discovery + monitor + upload)の検証開始までに必要 |
| A-3 | **プッシュ済み(2026-09-02)** `--capture <path>`(cli §3.1、USB transaction 記録)の実装。受け入れ基準: (1) 全 probe 系 command で記録できる、(2) **失敗時(非ゼロ exit)でも記録が flush される**(主用途はバグ報告)、(3) 記録形式が versioned で ch32rv 側の replay fixture にそのまま取り込める、(4) 記録に書込データ(firmware 内容)が含まれる旨を doc に明記。**着手順は A-3 → A-2 を推奨**(以後の全報告の質が上がるため) | 「回避せず報告」ループの土台。無いと報告が現象+再現手順ベースになり、実機なし CI での regression 固定(ch32rv architecture §4 の replay harness)にも繋がらない |
| A-4 | ~~README / Status の実態同期~~ **済み(2026-09-02 確認)**: README 刷新済み(β位置づけ・verified scope・インストール手順・udev 手順) | 対外表示が「pre-implementation」のままではフィードバックの受け口として誤解を生む → 解消 |

## 依頼 B: コア同梱リリースまで

| # | 依頼 | 根拠 |
|---|---|---|
| B-1 | cargo-dist による 6 platform binary、artifact 命名とチェックサムの固定 | コア側の package_index.json 作成と ADR-0011 `mirror-` 枠での再配布に、安定した artifact 名・URL 構造が必要 |
| B-2 | Windows 実機検証(WinUSB binding、`doctor` の driver 診断) | Board Manager 利用者の主流は Windows。現時点の実機検証はすべて Linux |
| B-3 | `--chip` 語彙の machine-readable 公開(`db list --json` 等) | boards.txt の `build.ch32rv_chip` 値の源泉。現行 `tools/index/probe_rs_targets.csv` の置換元。compile-only の 7 family(V205/V407/V467/X305/X315/M030/M103)が「DB に無い」exit 20 の detail で区別され、利用者へ fail-closed の文言を出せること |
| B-4 | `arduino discovery` / `arduino monitor`(Pluggable Discovery/Monitor、P1) | IDE の port 列挙と monitor 体験。特に SerialSDI(uart と同一 CDC に混在)を IDE の monitor で正しく扱う唯一の解。upload だけなら不要なので B 扱い |
| B-5 | `flash --sdi on`(および `--monitor` への移行)を recipe から使える形で | コアの SerialSDI ライブラリ利用スケッチの「書込→即 monitor」。現行構成では SDI 有効化を upload に織り込む手段が無い(ch32rv requirements §5(5)) |
| B-6 | ~~udev rules ファイルの Linux tar への同梱~~ **納品済み(v0.2.0、2026-09-02 実物確認)**: Linux tar(x64/arm64)に `60-ch32rv.rules` を同梱。`doctor --emit-udev` 出力とバイト一致(単一ソース `cli/60-ch32rv.rules` を include_str!)。`uaccess` 本線 + `plugdev`/0660 fallback、対象 `1a86:8010/8011/8012`(8011 = RISC-V alt PID は依頼時想定に無かった分も収録)。ISP `55e0` は isp 実装時に追加予定 | **ツールアーカイブ内の post_install.sh は Arduino から実行されない**(post-install はプラットフォーム側の仕組み — arduino-cli platform specification で確認済み 2026-09-02)ため、スクリプト同梱は無し(依頼どおり)。macOS/Windows アーカイブには非同梱(udev は Linux 固有) |

## 依頼 C: コアのリリース後でよいもの

| # | 依頼 | 備考 |
|---|---|---|
| C-1 | V103 buffered quirk の解消(部分消去・flash SW breakpoint) | 日常 flash は stub 経路で動作済み。capability で正直に弾ければリリース非ブロッカー |
| C-2 | `probe firmware check --min` の維持(契約への固定) | 実装済み。ベンチ CI の probe firmware ゲートとして使い続ける |
| C-3 | isp / boot 経路(P2) | コアの書込経路が LinkE 系で足りる間はリリース要件に含めない |

## コア側で行う受け入れ作業(ch32rv への依頼ではない)

- ベンチ(smoke.py)に ch32rv 経路の opt-in 切替を実装し、probe-rs 経路とのクロスチェック期間を設ける(probe-rs はローカル利用のみ。同梱はしない)
- **udev rule の自動インストールはコアの `post_install.sh`(プラットフォームルート、Linux)で行う**(B-6 の対、2026-09-02 決定)。実行条件は IDE 2.x = 常に実行、arduino-cli = interactive 時のみ、IDE 1.x = 実行しない(Arduino 署名限定)。IDE 2.x は通常ユーザー権限で走るため /etc/udev への書込失敗時は sudo 手順を案内して**正常終了**する(非ゼロで落とさない)。rule 実体はコア側にも置き、CI で `ch32rv doctor --emit-udev` の出力と一致することを照合してドリフトを防ぐ。最後の砦は doctor(未インストール検出+次の一手提示、実装済み)
- platform.txt の recipe 置換、ADR-0008 の追記(または新 ADR)、boards.txt への `build.ch32rv_chip` 追加
- `mirror-ch32rv`(ADR-0011 枠)の作成と package_index への組み込み
- gap 7 family の実機調達(ch32rv 側 DB の verified 化は実機が前提。device_id の evidence 依頼は ch32rv → ch32-device-data で提出済み)

## 参照

- ch32rv: `../../ch32rv/docs/cli.ja.md`(§3 共通契約、§4.11 arduino、§5 呼び出し例)、`../../ch32rv/docs/architecture.ja.md`(§5 配布)、`../../ch32rv/CHANGELOG.md`(実機検証の記録)
- コア側: [ADR-0008](adr/0008-upload-strategy.ja.md)、[ADR-0011](adr/0011-tool-mirror-repository.ja.md)、[upload-and-fixture.ja.md](upload-and-fixture.ja.md)、[platform.txt](../platform.txt)
