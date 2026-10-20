# P2 対象固定とhost対照ロジックの結果

2026-10-20。P2-PREP-01を完了。型番・資料・入力・描画データを固定し、Knob／PPS／表示のR対照と独立oracleを用意した。
host検査51件と、対象ABIでのlogic link 4構成が成功した。
P2全体は進行中。共通bus・周期処理の結合、起動・実周辺backendはまだない。

## 入力と変更

入力HEADは `b6639ed40a2f3f8d2664b710b37238d8eec134dc`、着手時はclean。
[P1-1の比較契約](../../experiment-spec.ja.md)と[用途](../../workloads.ja.md)を維持した。
製品core・API・Accepted ADR・既存EXP-00のコードは変更していない。

- [固定対象・差分・設備表](../../p2-preparation.ja.md)と[p2-inputs.json](../../p2-inputs.json)を作成。
- [host対照](../../../../tests/unit/core_redesign/README.ja.md)にC++、Python oracle、trace、golden、意味の検査を追加。
- [ABI確認入口](../../../../tests/platform/core_redesign/build_logic_controls.py)に入力・コマンド・libraryのhashと成果物を記録するbuildを追加。
- 全体計画、TODO、platform READMEの現在地を更新し、[次単位の依頼](../P2-PREP-02/brief.ja.md)を作成。

資料6件はWCH発行の4 PDFと2 SDK headerを固定commitから取得し、hash付きで保存した。
V003F4P6／X035C8T6の容量・ISA・route等を照合した。boot予約0のbare compile profileを選んだもので、
既存boardやfixtureを選定・使用したことは意味しない。
入力とtoolの共有記録は [measurements.json](measurements.json)にある。

## Host検査

最終実行コマンドは次のとおり。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_r_workloads.py -o junit_family=xunit1 \
  --junitxml=work/core-redesign-p2-host-20261020-final/junit.xml
```

結果は51 passed、8.88秒。AddressSanitizer／UndefinedBehaviorSanitizerを有効にしたnative C++を使った。
pytest pluginの`record_xml_attribute` experimental warningが51件あり、失敗はない。

| 検査 | 観測と照合 |
|---|---|
| 固定25 trace | 各操作後のC++状態と独立Python oracleが一致 |
| 必須結果の直接確認 | position、status、sequence、成立値、受理・回収数等をliteralな期待値と照合 |
| 4 golden frame | 各1024 byteの全一致。hashの一致だけで描画内容を判定しない |
| seed付き検査 | Knob／PPS、各seed 0〜9・10,000生成ステップ。合計200,000生成ステップからの状態遷移を一致確認 |
| 切断前のqueued Apply | 再接続後に電力要求を開始しないことを直接確認 |

Knobは設定の有限容量、読み出し中snapshot、過長frame、途中restart、飽和、設定sequenceの周回を含む。
PPSは再操作・切断・失敗・再適用、古い／二重完了、受理と回収を含む。
表示は実行中buffer、revisionの集約、失敗再試行と終端を一度だけ確定する条件を含む。
物理割り込みの同時実行はなく、hostからの直列刺激の結果である。

## 境界検査で修正したこと

表示の終端が既に成功しているとき、後から来た失敗を上書きしないようにした。
また、`attach → Applyをqueueへ受理 → detach → attach → step`で旧操作が新しい接続の要求になる経路を発見した。
直接確認の回帰検査が1件失敗することを保存し、入力の接続世代を別metadataに保持して修正した。
入力payloadは1操作4 byteのままで、session metadataは別の16 byteとしてRAMへ計上される。
これは「切断後は再適用を待つ」という既存の比較条件を満たす修正で、公開実行規則は変えていない。

最初のcross linkでは、compilerが生成したmemcpyの未解決で失敗した。
coreを追加する代わりにtoolchainのlibc memory関数とlibgccをlinkし、選んだarchiveのhashを記録した。
以後、入力ロジックの修正に合わせて再buildし、最終の4構成は現在の入力hashと一致している。

## 対象ABIでのlink

```sh
python3 tests/platform/core_redesign/build_logic_controls.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/core-redesign-p2-logic-20261020-final
```

`-Os`、LTOなし／ありを分けた。値は検査用mailbox・counter・fake stateを含むlogic-onlyのbyte数。
startup・IRQ vector・実I2C／PD・実clock・controller header・完成用途の進行を含まないので、
完成firmwareのfitやコアの費用には使わない。

| 用途・ABI | LTO | Flash image | 静的RAM |
|---|---|---:|---:|
| K0・ILP32E | なし | 1,052 | 80 |
| K0・ILP32E | あり | 1,036 | 80 |
| X0・ILP32 | なし | 1,974 | 244 |
| X0・ILP32 | あり | 1,758 | 244 |

stack peak、heap peak、cycle、遅延は未測定。`.su`は解析用の関数情報として保存し、実行時最大量とはしていない。
今回は対象ABIでロジックをlinkできることの確認で、A〜Cの比較を始めたものではない。

## 証拠と再生成

共有する数値・入力hashは [measurements.json](measurements.json)、一次資料と構成は [固定入力](../../p2-inputs.json)。
生の記録は次のディレクトリにある。

- `work/core-redesign-p2-sources-20261020/`：6資料、抽出text、取得URLとhash。
- `work/core-redesign-p2-host-20261020-final/`：最終51件のJUnit。
- `work/core-redesign-p2-logic-20261020-final/`：4構成のELF・map・symbol・逆アセンブル・入力コピーとコマンド。
- `work/core-redesign-p2-prep-20261020/`：13入力のコピー・hash、host compiler識別、commit・差分。

移送用archiveは `work/core-redesign-p2-prep-20261020-evidence.tar.gz`、6,633,390 byte、
SHA-256は `994f45c412de7ac32c17468295df0090dc01a5890c8e80c8400b7eef39976480`。
logic成果物43ファイルと一次資料6件をarchive内でもhash照合した。
回帰検査の修正前失敗と最初のlink失敗も含めた。

新しいcheckoutではhostにg++を、cross buildには固定xPack toolを用意し、上のコマンドで再生成する。
未コミットの成果物を渡す場合は、archiveの`preparation/inputs/`を入力HEADの相対pathへ重ねる。
このコピーに検査・oracle・trace・golden・build入口が含まれる。
実行にはrepository側の固定Python依存を使い、旧log内の絶対pathは新しい作業領域へ読み替える。
PDF・SDK headerはJSONの固定URLとhashから再取得できる。コードへ取り込む必要はない。

## P2で残るもの

X-BUS、100 ms周期の計測、描画と入力の全体結合、delay／loop外のprofile、最小起動、実IRQ排他、
K0のACK/NACK、実PD、実clock、実機設備は未検証。
接続世代番号の周回、通知解除後のobject寿命、Flashのprogram・停止・電断、V006／H417の追加条件も残る。
各未検証を、今回の51件の成功へ含めない。

次は通常モデルで [P2-PREP-02](../P2-PREP-02/brief.ja.md)を実行できる。
共通busと周期処理を結合したhost対照を作り、起動と実backendへ進む前に全体の所有と進行を確認する。
今回、高位判断が必要な比較契約の変更は発生していない。
