# I2C backendの回収と利用者の結果回収を分ける

2026-10-21。実装commit `6aa3b1e`。
[入力](../../i2c-handoff-inputs.json)、[全20 ELF](i2c-handoff-size-matrix.csv)、
[host・tool・hash・比較台帳](i2c-handoff-measurements.json)を保存した。
[register I2C Master](i2c-master-result.ja.md)を変更せず、完了枠・buffer借用・明示dispatchへ接続した。
製品コア・USB・既存のMaster／Fake／resource adapterには変更していない。
これはhostのregister模型と独立linkの実験で、実wire・IRQ・pin／clock共有の成立を示すものではない。

## 二つの借用を分ける

従来のMasterはtakeで自身のrecordとbuffer借用を同時に返す。
別要求へ進むためにそのtakeを呼ぶと、利用者がまだ結果を処理していなくてもbufferが編集可能になる。
新しい試作では、外側のBufferと、Masterだけが使うprivateなBackendViewを分けた。
MasterがViewをreleaseしても外側のownerは消えず、利用者のtakeだけが外側の借用を返す。
byte列を別bufferへコピーせず、元の配列をView経由でMasterへ貸す。

| 段階 | 外側のrecord・buffer | backend | 次のdispatch |
|---|---|---|---|
| submit成功 | 予約・借用済み、Queued | まだ使わない | 可能 |
| dispatch成功 | Running | Viewを借用してSTART発行 | 拒否 |
| MasterのTerminal判定 | Runningのまま保持 | 停止・drain判定済み、まだ未回収 | 拒否 |
| 明示reap | 結果をrecordへcopy、Terminal。bufferは借用継続 | Master.takeでbackend借用だけ返却 | 別recordへ可能 |
| noticeのack | 保持したまま | 変化なし | 変化なし |
| 利用者のtake | 結果をcopyし、外側record・bufferを返却 | 変化なし | 自動実行しない |

submit・clientのend／cancel・notice／ack・利用者take・reapはMMIOを行わない。
open_backend／close_backend、dispatch、明示stepだけが既存Masterのregister経路へ入る。
reapをstep内へ隠したり、takeで次要求を自動dispatchしたりはしない。
呼出しが来なければ進まず、時間上限も設けない。

MasterのTerminal判定を止まった根拠にできるのは、既存のIRQ-off・DMAなし・exclusive registerアクセスという条件下だけ。
実siliconでの停止・drainの十分性、IRQ／DMAがbufferへ触らなくなったことは、このFakeだけでは確認できない。
別backendへこのViewのreleaseを移す場合も、停止契約が必要になる。

## 受理・取消・終了

利用者3、専用3 record／共有2 record、保護付きbuffer64 B×3を比較した。
各clientは未回収要求1件まで。submit時点で結果枠とbufferを確保し、dispatchまでのQueued中も利用者はread／writeできない。
write／read／repeated STARTのtx＋rxを同じ配列内で扱い、受信の保存位置は従来どおりtx長の直後。
Result・issued・confirmed・received・error flagsを保持し、受信byte列をmetadataへ置換しない。

Queuedなcancel／endはMasterへ一度も渡していないので、MMIOなしでCancelled・prefix 0へ移れる。
Runningでは既存Masterへ取消要求を出すだけで、reap・利用者takeまで借用は続く。
ADDR後のreadでは、取消やclient endの後でもSuccessが勝つ既存の意味を保つ。
client endは新規受理とnoticeを閉じるが、Terminal結果の明示takeは可能。
再openはそのclientの未回収recordがなくなるまで拒否する。

physical backendを閉じる操作はclient endと分けた。
Queued／Runningがあればclose_backendを拒否し、Terminalだけならbackendを閉じても外側のresult・bufferを保持する。
その後にbackendを別の許容PCLK／速度で再openし、旧結果をtakeできることを検査した。
これでGPIOやclockの資源台帳を自動解放するわけではない。今回のManagerはその台帳へ未接続。

dispatchは受理済みclientのround-robin。利用者がtake直後に再submitしても、待つ別clientを先に選ぶ。
共有2枠は受理容量が違い、未受理clientの公平性までは保証しない。
wire BUSYや残存START等でMaster.startがBusyなら、Queuedとbufferを保持して明示retryを待つ。
その失敗で外側token・cursor・backend generationを進めない。

外側recordとbackendのgenerationは別々に非周回で管理する。
backendがExhaustedならdispatchはその状態を返し、受理済みQueuedを勝手に捨てない。
callerはQueuedをcancelしてtakeできる。自動的なFailure完了や復旧方法を採用したものではない。
tokenは同じManager instanceの生存期間内の識別子で、bootやobject再生成を跨ぐものではない。
Buffer・Managerはreapと利用者takeまで生存させ、copy／moveやdestructorによる自動停止は提供しない。

## Hostの証拠

ASan／UBSan、`-Wall -Wextra -Werror`でnative70件＋workspace3件、計73件が通過した。
新規70 trace・13,302操作、基準17種類のtrace・1,175操作を保存した。
write、read長1／2／3／4／8／32／64、tx＋readを両record方式で比較し、34条件で全MMIO列・register・送信列・ADDR時のcontrol・回収byteが元driverと一致した。
同じ基準入力を専用／共有の両比較で使うため、基準の実行は34回だが保存するunique入力は17種類。

- 受理時の外側借用、Terminal判定前とreap前のtake拒否、reap後もbuffer編集を拒否。
- client0の結果を保持したままclient1へ実register engineを渡し、後の結果を先に回収。
- Queued cancel／endとRunningの停止待ち、read中endのnotice抑止とSuccess回収。
- 5 error flagのprefixと結果保持、他masterのBUSYによる次dispatchの保留、ARLO時にSTOPを出さないこと。
- 不正長・null・予約address・buffer二重借用・古い要求と別client tokenを拒否し、失敗時の出力を保持。
- 専用は3 client、共有は受理した2 clientで、直後の再submitに対する巡回順を確認。
- 8 bitの外側recordは専用255件／共有2枠計510件で退役。Queued cancelだけならbackend世代を使わない。
- 8 bit backendは255実transaction後に退役し、close／reopenでも巻き戻らない。次の受理済み要求を明示cancel／takeで回収。
- backendだけのclose・再設定中にも旧結果を保持する境界。

各traceの最後には全外側bufferを回収した。
基準Master header・Fake元driver・protocol helperのhashは以前の資源実験と一致し、同じFakeを抽出して使った。
所有の全面的な独立状態機械との照合ではなく、wire経路の差分比較と境界assertによる証拠である。
fixtureはtest module内に置き、conftest.pyは追加していない。

## ABIの値

V003 rv32ec／ilp32e、X035 rv32imac／ilp32、LTO off／onで20 ELFをlinkした。
LTOありの値は次のとおり。image・全RAMは共通観測harnessを含む。

| ABI | record | image B | 全静的RAM B | Manager B | 外部buffer3個 B |
|---|---|---:|---:|---:|---:|
| V003 | 専用3 | 3,336 | 456 | 144 | 204 |
| V003 | 共有2 | 3,360 | 420 | 108 | 204 |
| X035 | 専用3 | 3,072 | 456 | 144 | 204 |
| X035 | 共有2 | 3,112 | 420 | 108 | 204 |

Manager本体にはMasterと全record・Viewを含み、backend用byte配列を別に複製していない。
観測harnessは103 Bで、objectの和とsection合計の差はalignmentによる。
全20行の.dataは0 B。allocator・constructor arrayの混入を検査した。
未使用8構成はManager・buffer・追加RAMが0 B、空の8 B probeとimage hash一致。
最大単一frameはLTOありでV003 32 B、X035 80 Bだったが、呼出し鎖やIRQを含むstack peakではない。

専用3と共有2は同等の受理保証ではない。
以前のfake IoSlotによる完了枠実験はbuffer32 B・異なるresultであり、その総RAMとの差分をこの接続だけの費用にしない。
実行器・資源台帳・pin準備・起動を含む用途全体の費用はまだ測っていない。

## 再現と残るもの

```sh
env EXP01_I2C_HANDOFF_EVIDENCE=work/exp01-i2c-handoff-host-NEW \
  uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_i2c_handoff.py tests/unit/test_workspace.py -q

python3 tests/platform/core_redesign/measure_i2c_handoff.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp01-i2c-handoff-abi-NEW
```

出力先は新しいdirectoryを使う。
local archiveは `work/exp01-i2c-handoff-20261021-evidence.tar.gz`、1,211,195 byte、
SHA-256 `ec2bd38558b1fac689e5a396a39206cddb269e8fcea6a7f0ad29bcc58e1f7ed5`。
native2 binary・入力と生成Fake・全87 unique trace、34条件の再照合、ELF／binary／map／symbols／disassembly／frame、
compiler／binutils／libgcc hash、全command、集計scriptを保存した。
target probeのcritical関数はbarrierだけで、IRQ-offはcaller前提。実行・upload用firmwareではない。

共有resource DomainとX035準備adapterへの接続、EventExecutorへの配達、ISR同期、実wireとMCU時間、
backend停止後にclock／pinをどこまで返すか、独立stackを含む用途との結合は残る。
新Managerを公開bus classとして採用したわけではない。
[回収を分ける教材案](i2c-handoff-exercise.ja.md)は、人による評価前。
