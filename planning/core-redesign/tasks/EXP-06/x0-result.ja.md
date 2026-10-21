# EXP-06 X0の同等ロジック比較

2026-10-20。入力作成時のHEAD `209408b`、検査済みhost試作は `3373e55`。
当時の未commit試作も含めた入力をhashとコピーで保存した。HEADだけを入力ソースの同一性の根拠にしない。
PPS設定、表示、周期計測、任意のタッチを、同じ仮想I/O・入力・進行機会でR/A/B/Cへ接続した。
製品コアとUSB実装は変更していない。実行方式や公開APIの採用、P4全体の完了を意味しない。

## 揃えた処理単位

Rは用途専用の入力array、Aは共通Queue、Bは共通Queueと1宛先のWorkExecutorを使う。
Cは追加比較条件の **C-static-batch**。固定1宛先のFIFOそのものに入力を保持し、二重の入力Queueは作らない。
全群とも入力容量4件、各8 byte（command 4＋接続session 4）。満杯の次入力は拒否し、既存入力を保持する。

1機会では、開始時点の入力を最大4件消費し、PDの回収・要求、表示の32 byte分の準備、
busの終端回収と最大1要求の選択まで進める。busは表示／計測／タッチの3者を巡回する。
同一機会内の複数Applyは参照対照と同じく最後の選択へ集約する。

既存Cの「1宛先1payload／巡」をそのまま使うと、途中のApplyでPD要求を開始して挙動が変わる。
そのため既存EventExecutorの仕様と測定は変更せず、固定宛先で有限batchを配送する条件を別に追加した。
この条件に動的登録・解除・複数宛先の機能はなく、汎用Cと能力を揃えた容量比較ではない。
Cのhandlerはbatchを実行中だけ借りる。配送中の再投稿・通知は次巡へ残り、再入配送を拒否する。
batch用の通常stackが必要で、静的RAMだけを比べて小さいとは判断できない。

frontend開始前のbeginは1回。刺激の後にadapterがchangedを通知し、frontend中にhandlerをinline実行しない。
Mは明示だけ、Sはsystem自動機会も、Uは参加user自動機会も許可する。
setup／ISR／callbackからの配送は拒否する。Delay／AfterLoopは入力された機会であり、実runtimeへの組込みではない。
ドメイン検査では結合applicationを1宛先として扱う。PD等を個別のsystem／userへ分類する設計は未検証。

## タッチの条件訂正

P1のB2は8 byteを50回／秒。[旧PREP-02](../P2-PREP-02/result.ja.md)の試作は50 tick周期、
1 tick＝1 msとして20回／秒だった。旧ソース・数値を今回の50回／秒条件へ読み替えない。
今回の全群は20 tick周期へ揃えた。独立oracleは周期を引数化し、既定50で旧対照を再現し、今回だけ20を渡す。
基準の計測は100 tick周期、表示は1024 byteを32 byteずつ送る。

## Host検査

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x0_candidates.py \
  tests/unit/core_redesign/test_r_application.py tests/unit/test_workspace.py \
  -o junit_family=xunit1
```

ASan／UBSan付きの新規X0検査55件、旧application対照21件、配置規則3件、計79件が成功。
既存plugin由来のexperimental warning79件。conftest.pyの再導入はなく、remote CI成功を意味しない。

独立oracleは全frameと取引台帳を保持し、各入力後に29個の観測値を照合する。
共有する仮想backendだけを正解にしていない。主な条件は次の通り。

- 1000 tickの基準／タッチ追加。PPS確定5020、計測10件、タッチ有効時50件、表示2frameと最新revisionの回収。
- 表示／計測／タッチ各ownerの1回故障と回復。回収前bufferの書換え検出とbusy時の所有保持も別に検査。
- 入力満杯、切断・再接続、古いPD完了、長い通常処理停止、計測値変更と失敗。
- 同じ4入力のApply集約、同じM/S/Uと禁止文脈、system／user・参加の条件。
- 10 seedの各3000生成入力と後続drainを全4群へ与え、各行の出力・受理・拒否・回収を照合。
- Cの有限snapshot、配送中の次の4件と集約通知、満杯、非再入、通知だけの配送、未参加userの保持。

## ABIの容量

```sh
python3 tests/platform/core_redesign/measure_data_parts.py --matrix x0 \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp06-x0-link
```

29ケース×2 ABI×LTO有無の116構成がlink成功。
未使用4群×4条件の16行は追加Flash／静的RAM／init_arrayが0 byteで、baselineとbinaryも一致し、関連symbolは残らなかった。
全行は [x0-size-matrix.csv](x0-size-matrix.csv)、toolと入力・ELF等は [x0-measurements.json](x0-measurements.json)。

RV32E・LTOなし・明示M・タッチ初期無効の参考値。単位byte。

| 条件 | Flash image | 静的RAM | entryのstack frame |
|---|---:|---:|---:|
| R | 3286 | 372 | 28 |
| A | 3286 | 372 | 28 |
| B | 3774 | 400 | 44 |
| C-static-batch | 3902 | 384 | 72 |

stack列は `.su` の単一関数frameで、呼出し先・IRQ等を含むpeakではない。
CのFIFOを二重化しなくても、配送時のbatchと呼出し経路には費用がある。
R/A同値はこの条件の観測。全24対ではA−Rの静的RAM差は0、Flash差は0〜28 byteだった。
LTOありRV32Eの同じ条件ではAがRよりFlash 16 byte大きく、全条件で無費用とはいえない。
入力保持はR/Aともpayload16＋session16＋head/countとpaddingで36 byte。
Bは実行器とtoken、Cは通知・domain・実行中flagと一時借用pointer/cursor等も持つ。
共通状態にはPD／表示／bus、表示32 byte、計測8 byte、タッチ8 byte、周期と検査counterが含まれる。


測定入口は同じ操作分岐と観測値を保持する。検査counter、hash、仮想I/O状態、bufferとmetadataも含む。
タッチの有無は実行時flagの初期値であり、基準でもその状態・bufferを保持する。
タッチ未使用の0 costを証明する比較ではなく、compile時の機能除去は独立した次の条件として残す。

## 再現と限界

host `work/exp06-x0-host-20261020/`、link `work/exp06-x0-link-20261020/` に入力・生記録を保存。
コマンドと終了値、JUnit、stdout／stderr、ELF・map・symbol・逆アセンブル・section・.su、入力hashを照合した。
移送archive `work/exp06-x0-20261020-evidence.tar.gz`、5,858,536 byte、
SHA-256 `847f2a8daf34ca2ff7e21b32780c44bfd58891c40df4273711929a766dc2125a`。再実行の出力先は未作成の場所を使う。

仮想PDは5 tick、busは2 tickで終端となり、通常処理停止中も時刻入力で進む。
この仮想時間を実PD／I2Cの所要時間や許される停止時間に換算しない。
全群で同じp2::SharedBus・Screenを使い、入力保持と通常処理の起動を比較した。
共通の製品I/O API、実取消・停止・buffer返却、IRQ共有、実protocol・起動・stack peakは未検証。
時刻は非周回の単調入力で検査しており、結合X0の32 bit周回・sleep・clock変更は別条件に残す。
ABIは命令と呼出し規約の比較で、V003にPDがあることや用途が載ることを意味しない。

続く通常作業はタッチのcompile時除去、用途の変更課題、資源保有と待機依存、実I/Oへの接続。
人が状態・失敗・容量を理解して変更できるかの教材評価は人による確認を残す。

後続の[上流再現・device呼出し・差分](x0-upstream-result.ja.md)では、UC-01の元source／flagsで同一imageを再生成した。上流の128×32 vertical表示・software I2C・入力／失敗処理と、ここで固定したX0は異なる。上流サイズをこの候補の同等対照として直接順位付けせず、controller setup／headerと計測の物理取引を別途接続する。
