# N04 固定kernelを共通の仮想I/O境界で比較

2026-10-22。実装commit `f9537c5`。
[入力](../../kernel-trace-inputs.json)、[共通列](../../kernel-trace-traces.json)を固定した。
WCHの[固定FreeRTOS source](https://github.com/openwch/ch32x035/tree/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/FreeRTOS/FreeRTOS_Core)の
tasks／queue／listを変更せず、決定的なhost test portで実行した。
製品コア・USB・既存実験sourceは変更していない。RTOSや公開blocking APIの採用ではない。

## 比較境界

これまでの[2 stack比較](two-stack-result.ja.md)は明示turn、[WCH port比較](stack-port-result.ja.md)は
kernel taskの記述とlinkまでだった。入力1件でtaskをwakeすると、次の入力前に消費する可能性があるため、
同じ値を同じ順で与えるだけでは満杯の意味が揃わない。

今回はworker優先度1より高い優先度2のcontrollerをfixtureに追加した。
入力・完了・観測のコマンド間はcontrollerが動き続け、workerを進めない。
`drain`だけがcontrollerをsuspendし、全workerがblockしてidleへ達した後、idle hookからcontrollerをresumeする。
これにより、burstと観測の境界を明示した。

| 共通コマンド | 意味 |
|---|---|
| input／complete | 容量1 queueへuint32を1件offer。満杯は拒否し、workerを途中で進めない |
| observe | 結果とqueue状態を読む。処理は進めない |
| drain | このfixtureのworkerをblockするまで進めて観測する。未到着の外部完了は作らない |

継続型／host 2 stack型の参照は同じboundaryで32回の有限turnを与え、同じ公開値へ正規化する。
空slotの古いdata値は観測対象にせず0とし、fullと有効payloadを照合。
backendの保留要求・保留完了・phase、表示・完了数・拒否数・不一致数も照合した。
32回はこの固定fixtureの観測窓で、一般schedulerの公平性や時間保証ではない。

controllerは測定用で、coreに同梱する実行方式ではない。
FromISR関数を呼ぶが本物のIRQではなく、常に高優先度controllerから与える。
下位workerのwakeでcontrollerが交代しないこともassertする。実IRQ安全性を検査した意味ではない。

## kernelを動かした範囲

FreeRTOS V10.4.6のkernel Cを固定し、TCBの先頭stack pointerを使うtest portとLinux `ucontext`を接続した。
critical中のyieldはpendingにして、mask解除後にkernelの`vTaskSwitchContext`を呼ぶ。
queueのwait list、block／unblock、task優先度の処理は固定kernelを実行する。
native portはLP64・64 KiB/task stackで、WCHのport.c／portASM.Sは実行しない。
hostのcontext・stack量・切替時間をMCU値へ転用しない。ASanではなくUBSanで検査した。

実tick・timer IRQは進めず、待機は無期限queue待ち。協調／preemption設定のAPI進行を比較するが、
tick preemptionや時間待機・real-time性を検証した結果ではない。
一drainのcontext選択は256回までassertし、processにも観測timeoutを置いた。
戻らないtaskを製品で強制中断する仕組みにはしていない。
process終了はkernel shutdownやtask破棄の証拠ではない。

## Host結果

最終native20件＋workspace3件、計23件成功。既存plugin由来warning46件。
76 vendor fileのhash・Git blobを照合し、kernel4実行物と参照1実行物を作成した。
78 trace、共通コマンド120,244件、展開後native driverコマンド864,430件を保存した。
異なる数え方を合算せず、重複traceも増やさない。

- 8固定列：空、正常、入力burst、早い完了、完了burst、不一致、controller停止、段分割の容量。
- seed 0〜9、各3,000共通コマンド。input／complete／drain／observeを組み合わせる。
- workflow＋backendの2 taskは、協調・preemptionの両方で、継続型・2 stack型・独立oracleと一致。
  全4方式を揃えた18組で全公開状態が一致した。
- workerを動かさない入力batchで、表示が勝手に進まないことも2／3 taskで検査。

固定sourceがない通常CIではnative kernel検査をskipし、networkやboardへ自動接続しない。
sourceまたはevidenceを明示した実験では、source不在を失敗とする。
今回の最終23件にskipはない。CI全体の成功や実機成功ではない。

## 3 taskへ分けると容量も変わる

完了を与えず、入力の間にdrainを入れた場合：

| 保持場所 | workflow＋backendの2 task | 入力・backend・表示の3 task |
|---|---|---|
| backendが待つ要求 | 1件目 | 1件目 |
| Request queue | 空 | 2件目 |
| 入力taskがsend待ちで保持 | 該当なし | 3件目 |
| Input queue | 2件目 | 4件目 |

同じ5入力の受理は2 taskで`1,1,0,0,0`、3 taskで`1,1,1,1,0`となった。
後から完了1〜4を与えると、表示完了数はそれぞれ2／4。
3 taskは既存queueとtask localを先読み保持へ使うため、追加stack／TCBだけでなく機能量が異なる。
3 taskを同じ受理容量の方式として順位付けしない。snapshotやキュー総数だけから有効容量を推定しない。

## X035の同じfixture sourceをlink

worker・controllerの共通fixture Cを、WCH portとX035型でも組んだ。
target側はvolatile command／reply sequenceを持つcontroller。実vector・起動・外部controllerは接続していない。
入力を書いてからcommand sequenceを進め、replyを待つ一要求ずつのfixture契約。
未対応operationはfaultに止める。これを実機で通した結果ではない。

[18構成](kernel-trace-size-matrix.csv)：LTO有無×協調／preemption×アプリ2／3 taskの使用／未使用＋empty。
各task stack128 word＝512 B、共通起動／IRQ予約1,024 B。
port固有のIRQ上端−512 Bと、その下の512 Bを維持する。

| LTOあり | Flash image | 静的RAM | 全task stack配列 | TCB合計 | 起動／IRQ予約 | RAM総予約 |
|---|---:|---:|---:|---:|---:|---:|
| 協調・アプリ2＋controller＋idle | 5112 | 2936 | 2048 | 240 | 1024 | 3960 |
| 協調・アプリ3＋controller＋idle | 5112 | 3512 | 2560 | 300 | 1024 | 4536 |
| preemption・アプリ2＋controller＋idle | 5368 | 2936 | 2048 | 240 | 1024 | 3960 |
| preemption・アプリ3＋controller＋idle | 5368 | 3512 | 2560 | 300 | 1024 | 4536 |

controllerだけのstack512 BとTCB60 Bを別項目で記録した。queueは4個、計320 B。
fixtureの観測・制御とidle hookも含むimageで、以前のcontrollerなし数値と直接順位付けしない。
未使用8行はemptyと同じimage、静的RAM0 B。共通起動／IRQ予約はemptyにも残る。
allocator・constructor arrayは検出されなかった。
stack量は候補予算で、安全性や実peakは未確認。`.su`も観測peakではない。

初回ABIは10行で、LTOが小さいfixture wrapperをinlineしたためsymbol名検査に失敗。
実port入口4個の保持検査を維持し、inline可能なwrapper名への依存を外した。
最終18行を再生成し、先の完成10 imageと一致した。kernel・fixtureの動作は変更していない。
nativeも、source不在を明示実験でskipしない検査条件へ修正し、全23件を再実行した。
初回成功数を最終結果へ加算しない。

## 再現・次の作業

```sh
EXP03_KERNEL_SOURCE=work/<locked-vendor-tree> \
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_kernel_trace.py tests/unit/test_workspace.py -q
python3 tests/platform/core_redesign/measure_kernel_trace.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-gcc \
  --source-dir work/<locked-vendor-tree> --out work/<new-kernel-trace-abi>
```

`EXP03_KERNEL_TRACE_EVIDENCE`に未作成directoryを指定するとnative source・実行物・traceを保存する。
[記録](kernel-trace-measurements.json)に入力・tool・JUnit・trace・ELF・予備run・archiveを保存した。
[教材案](kernel-trace-exercise.ja.md)は人による評価前。
N04の通常モデルで進めるsource／host／整数模型／link範囲は実施した。
WCHの実切替・待機・tick／IRQ・stack peak・外部controllerでの同一列再現はN09へ残す。
公開境界・寿命・系列・採否はN10／P5。次の独立作業はN05の保存と用途共存。
