# P2 X0共有busと周期処理の結合結果

2026-10-20。P2-PREP-02完了。RのPPS、画面、計測、タッチを、一つのfake busと明示stepへ結合した。
P2全体は進行中で、最小起動・実IRQ・実I2C／PDはまだ含まない。

## 実装と確認した意味

- busは受理済み1件のみ。終端も回収までは同じslotを占有し、busy要求がownerを置き換えない。
- 表示32 byte、計測8 byte、タッチ8 byteのbufferを回収まで保持し、hashの封印で途中変更を検出する。
- 各transactionの終了後、readyな要求元をround-robinで選ぶ。待機queueはbusへ追加していない。
- 計測は100 tick、タッチは50 tick。遅れた周期は最新1回へ集約し、捨てた周期数を記録する。
- 描画はviewが変わったときだけdirtyにする。最初のchunkの受理を基準に100 tick以上の開始間隔を保つ。
- アプリ停止中もfake hardwareは終端を保持し、再開時に回収して他の要求へ進む。
- 切断・旧完了・queued Apply・入力満杯・計測stale・失敗からの復帰を結合状態で検査した。

## 結果

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign -o junit_family=xunit1 \
  --junitxml=work/p2-prep02-host-20261020/junit.xml
```

72件成功、20.55秒。既存51件と結合21件を含む。AddressSanitizer／UndefinedBehaviorSanitizerを有効化。
結合側は10 seed×3,000操作と固定故障条件を、全frameと取引ledgerを持つ独立oracleへ照合した。
各観測で `受理数−回収数＝active`、`終端数−回収数＝terminal` を確認する。
buffer破壊を意図的に注入する検査も通り、診断値が常に0を返すだけではないことを確認した。
pytest pluginのexperimental warningが72件あるが、失敗はない。

通常負荷1000 tickの基準では計測10件、タッチ追加条件ではタッチ20件を完了した。
安定した画面は2 frameで落ち着き、毎stepで再描画しない。
1000 tick停止後も終端は一度だけ確定し、回収までbufferを保持。再開後に1000、1100 tick分の計測2件へ進んだ。

```sh
python3 tests/platform/core_redesign/build_logic_controls.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --application --out work/p2-prep02-link-20261020
```

X035の2構成がlink成功。

| 条件 | logic Flash image byte | 静的RAM byte |
|---|---:|---:|
| LTOなし | 2866 | 364 |
| LTOあり | 2786 | 364 |

検査counter・mailbox・fake stateと、runtimeで切り替えるタッチの状態も含む。
これは触っていない機能の除去や、基準／B2の個別build費用を測った表ではない。
startup、controller header、実protocol、実stackを含まないので完成アプリのfitには使わない。

## 証拠と再実行

入力HEADは `6171a05`。ソースと入力hash、JUnit hash、linkの全コマンド・入力・library hashは
[measurements.json](measurements.json)に記録した。
`work/p2-prep02-evidence-20261020/`に入力コピー、`work/p2-prep02-host-20261020/`にJUnit、
`work/p2-prep02-link-20261020/`にELF・map・逆アセンブル・symbol・hash一覧がある。
移送用は `work/p2-prep02-20261020.tar.gz`。新しいcheckoutで上記2コマンドを実行して再生成できる。
archiveは135,471 byte、SHA-256 `059aabb57f9dc38a9c1c488fed2aea9a93d7e570dee447406265f8939696a800`。
出力先は毎回未作成のものを指定する。

## 残る条件

P1の基準Mを保ち、自動delay・loop外・再入・別stackの仕様は追加していない。
nativeの時間をMCU時間に換算せず、周期は仮想tickでの意味だけを確認した。
実機操作は行っていない。実IRQ同期、wireのACK/NACK、PDの外部条件、clock・時間精度・stack peakは未検証。
採用判断はP5へ残す。続いて[実行状況](../../execution-status.ja.md)に従い、独立したデータ部品・実行方式の比較を進める。

## 2026-10-20の条件照合による補足

この試作のタッチは50 tick周期であり、1 tick＝1 msなら20回／秒。
P1のB2（50回／秒）とは異なる。上の旧数値をP1のB2の結果としては使用しない。
[EXP-06のX0同等比較](../EXP-06/x0-result.ja.md)では全群を20 tick周期へ揃えた。
旧ソースと旧測定は再現のため保持し、oracleの既定値も50のままにした。
