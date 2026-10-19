# EXP-00 再現性と結果記録の作業依頼

P2、通常モデル向け。依頼版 P1-1、2026-10-19。
目的は、既存のリンク費用実験を再現し、後続の比較を追跡できる記録方法を用意すること。
実行モデルや公開APIの採否は、この作業に含めない。

## 入力

1. [全体計画](../../../../CORE_REDESIGN_PLAN.ja.md)のフェーズ・引き継ぎ規則。
2. [比較仕様 P1-1](../../experiment-spec.ja.md)のbuild・測定・記録。
3. [実行順](../../experiment-tasks.ja.md)のEXP-00とP2入力固定。
4. [既存probeの説明](../../../../tests/platform/README.ja.md)と
   [probe本体](../../../../tests/platform/probe_execution_link_cost.py)。
5. [testsの配置・実行規則](../../../../tests/README.ja.md)。

P0入力の基準commitは `55b6e90ce27872ae17b6a0fb9dc4c1f7c068ea7b`。
P1文書はその後の成果物なので、基準commitだけにcheckoutして開始しない。
開始時にP1-1文書と作業ツリーの版・差分・未追跡入力を確認し、hash付きmanifestへ記録する。

既存probeの入力SHA-256は
`2971233136e1459e0a15ae65090ed6a5e5118d8854c5a4a2177afb48f200178b`。
異なる場合は理由と差分を確認する。無条件に旧ファイルへ戻さない。
過去の `work/execution-link-cost-20261019-v2/result.json` は任意の参考資料。
存在しなくても実験を再生成できることを完了条件にする。

## 固定条件と操作範囲

- 既存probeの2 ISA/ABI、9ケース、`-Os`、LTOなし、入力C++とlinkerを維持する。
- compilerは `.tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++` を明示。
  なければリポジトリの固定tool取得手順を使い、PATHの別compilerへ無言で置き換えない。
- 実行はhost上のcross compile/linkとファイル検査のみ。DUT・probe・電源・serialを開かない。
- 修正先は記録・集計のための `tests/platform/`、必要なボード不要検査、platform README、
  `planning/core-redesign/tasks/EXP-00/` と全体計画の現在地に限定する。
- 新しい実行方式、LTO条件、実機startup、製品core・library・ADRは追加しない。

probeの既存結果だけで調べられる範囲にとどめ、後続全実験用の巨大なrunnerは作らない。
一般化が必要になったら、その時点で最小の補助関数として共用する。

## 実行手順

1. 入力とtoolの版・hashを記録する。compiler driverのほか、実際に使うcc1plus、assembler、linker、ar、
   size、objcopy、nm、objdump、配布archive／lockの識別子を追えるようにする。
   存在しないarchiveは取得不能と記し、hashを推測しない。
2. 元のprobeを独立した未作成出力先で2回実行する。ソースツリーや過去のwork出力を上書きしない。
3. 各runの18行について、ELF・map・section・symbol・逆アセンブル・binとJSONを照合する。
4. 2回の結果を比較する。数値と対象symbolの到達性は一致を要求する。
   ELFのbyte差が出たらpath・build ID等を調べ、理由を残す。単にhash不一致だけでコード差としない。
5. 未使用除去・weak参照・archive抽出・constructor保持を、下表の証拠で説明する。
6. 記録不足があれば最小限修正し、旧入力を残して修正後の同条件を再実行する。
7. 新しい作業領域で再現できるコマンドと記録一式を作り、resultに次のP2作業を示す。

実行コマンドの雛形。出力先が既にある場合は別の一意な名前に置き換える。

```sh
python3 tests/platform/probe_execution_link_cost.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/core-redesign-exp00-run1

python3 tests/platform/probe_execution_link_cost.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/core-redesign-exp00-run2
```

## 検査する意味

| 対象 | 必要な観測 |
|---|---|
| 全構成 | 9ケース×2 ISA＝18結果、対応する成果物、成功・失敗とコマンドが追える |
| 無効＋unused object/archive | baselineと追加Flash・静的RAMが同じか。serviceの到達性と除去をsymbolsで確認 |
| 未定義weak・weak archive | 未解決weakの扱い、archive内のserviceが抽出されたか、呼び出し側の費用 |
| 空のhook | 実体のないhook呼び出しでも残る命令・配置費用 |
| active object/archive | service_buffer等の保持と呼び出し命令。weakとstrongの差を説明 |
| constructor | `.init_array`と登録先の到達性。起動時にconstructorが実行されたという主張はしない |
| サイズ集計 | このlinkerでのbin長・section和・`.data`＋`.bss`とJSONの一致。配置間隙を説明 |
| 再現 | 2runの同一条件・数値・到達性。違いがあれば原因を記録 |
| 失敗記録 | compiler不在や不正出力先などを成功扱いしない。部分成果物と失敗コマンドを残す |

collectだけでbuild・実機操作を始めない。必要な追加検査は記録の欠落・誤集計・失敗の隠蔽を防ぐものに絞る。
検査コードを変更した場合は該当するボード不要検査を実行する。既存の全実機suiteは実行しない。

## 成果物と完了条件

`result.ja.md` はこの依頼と同じディレクトリへ作る。
18行の結果表、baselineと差分、tool／入力、再実行コマンド、各契約の証拠、未検証を含める。
生ログ・ELF等はrunごとのwork配下とし、manifestとhash一覧を持つarchiveまたは再生成手順を記す。
個人の絶対pathだけを引き継ぎ情報にしない。共有する小さな結果表・手順はGit管理対象にする。

完了には、36 build結果の確認、サイズとsymbolの照合、同条件の再現、入力とtoolの追跡、
失敗が成功へ混ざらない記録、別作業領域での再生成経路が必要。
本probeの16K／2Kのlinker値やRV32IMAC buildを、実コアやH417の適合性の結果と呼ばない。
stack・heap peak・cycle・遅延は未測定としてnull／未検証にし、ゼロにしない。

通常の不具合と記録漏れは担当が修正する。
比較契約を変える必要、未使用ゼロ目標の構造的な不成立、tool変更で同条件比較ができない場合は、
事実と選択肢をresultへ残して高位判断に戻す。無関係な証拠整理は続けてよい。

## 次セッションへの指示例

```text
planning/core-redesign/tasks/EXP-00/brief.ja.md を読み、P2のEXP-00を実行してください。
P1-1の条件を維持し、入力の版と未コミット差分を確認してください。
通常の修正と再現確認まで進め、実測値・未検証・再実行手順をresult.ja.mdに記録してください。
この依頼は実機操作を含みません。EXP-00の完了時点でフェーズ内の結果を報告してください。
```
