# N02 X0用途の完了通知・満杯・宛先解除

2026-10-21。実装commit `cfaa99f`。固定入力は [x0-event-wire-inputs.json](../../x0-event-wire-inputs.json)。
[直接回収の用途接続](x0-register-wire-result.ja.md)に、既存EventExecutorとpendingを保持する配信glueを接続した。
Master・Manager・Managed・Domain、直接回収adapter、用途の意味処理、rendererは変更していない。
製品コア・USB／Networkは編集していない。方式や公開APIの採用、N02／P4全体の完了ではない。

## 今回の比較条件

R/A/B/C-static-batchすべてに同じsource固定の完了通知を追加した。
用途は従来どおり1 packetを保持し、表示command／dataと計測2回のread、仮想touchを進める。
C-static-batchを汎用EventExecutorと同機能にした実験ではない。
用途の実行方式と、I/O完了通知の提供を別の条件として扱う。

`X0EventRegisterWire` は直接回収adapterを継承し、1宛先・FIFO容量1のEventExecutorを持つ。
sourceと内部callback contextは実験中ずっと生存する。同じManagerを指す固定contextであり、
Manager-local tokenがシステム全体で一意だとは仮定しない。

1. 既存の明示pollでI2Cを停止・reap・closeする。周辺leaseは返すが外側storageは保持する。
2. 明示pumpでhintをqueueへ送る。sourceはackしない。Full／Staleでもpendingと結果を保持する。
3. 明示deliverで内部handlerを呼ぶ。現在のrequest・source pendingを照合し、回収許可の印だけを設定する。
4. 用途の明示stepがtakeを試みる。許可済みであれば直接adapterのtakeで結果とstorageを一度だけ回収する。

内部handlerは利用者処理を呼ばず、Masterを進めず、結果のtakeやbuffer返却も行わない。
callbackへ通知を渡したことと、用途が受信値を処理したことを分ける候補である。
virtual touchの終端は従来の仮想clockによる直接回収を維持し、物理touch通知を実装した扱いにしない。

## 故障・解除・進行不足

| 状態 | 周辺lease | 外側buffer | 用途がtakeできるか |
|---|---|---|---|
| I2C実行中 | 保持 | 貸出中 | できない |
| 停止・reap後、queue満杯 | 返却済み | 貸出中 | できない |
| hint送信後、配達前 | 返却済み | 貸出中 | できない |
| 有効なhintを配達済み | 返却済み | 貸出中 | 正しいowner・容量なら可能 |
| 用途take後 | 他ownerが使用してよい | 返却済み | 同じ結果の二重回収はできない |

宛先解除はqueueを捨て、回収許可の印も消す。source pendingは残すので、再bind→pump→deliverで回復できる。
この解除時の規則は比較用候補であり、全event APIに採用したわけではない。
enqueue時にackして通知を失う方式への変更は行っていない。

二重pumpはFIFO容量1によりFullになる場合がある。配達後に再pumpした古いhintも保存し、
次requestの終端後に配達してもgeneration／index／clientの照合で回収許可に使わないことを検査した。
一方、同じ現requestを指す重複hintは印を再設定できる。二重takeはできない。

満杯や解除で用途がtakeできなくても、別Managerは返された周辺を使える。
内部hint handlerと旧takeのどちらも、peer通信中にMMIOを発生させずpeerのleaseを変更しなかった。

用途側には波及する保持がある。表示commandの結果を保留すると、この用途の次のI2C要求と仮想touchは待つ。
追加例ではPPSの入力処理・仮想PD完了は進み、confirmed=5020となったが、
同じ時点のmeasure_done／touch_doneは0のままだった。
「通知で独立に進む処理」と「1件の受理枠の保持で止まる処理」を分けてP5へ渡す。
frame全体・計測・制御の無期限進行や外部protocolの期限を保証する結果ではない。

## 検査結果

最初のrunはnative49＋workspace3＝52件成功。PPS進行の追加4件も成功し、native計53件＋workspace3件を確認。
初回のtest source snapshotは追加4件より前の版として保存した。
既存pytest plugin由来warningは初回104、追加8件。remote CI成功ではない。

- 160 trace・1,045,096入力操作・1,994,500 MMIO／critical eventを保存した。
  mode・入力・domainごとの40群すべてでR/A/B/Cの全観測行が一致。
- 独立whole-image／wire台帳に、sourceごとのrequest世代、宛先世代、1件queue、配達印の模型を追加。
  実装の観測値を期待値へコピーせず、各入力後に用途状態と通知状態を照合した。
- 通常の同一入力では、直接回収と通知方式の両方へpoll／pump／deliver／stepを明示した。
  全観測行で用途・loan・lease・wire byte・MMIOが一致。通知のstatusと内部状態だけは別条件として比較した。
- Full、Stale、解除でqueueを失う場合、再bind後のretry、無効hint、古い重複hintを検査した。
- 通知handlerのsystem／user・参加、M/S/U、setup／ISR／callback禁止を検査した。
  文脈は引数で宣言する模型で、実ISR検出や別実行器を跨ぐ再入禁止の証明ではない。
- 5 error flag×表示command／data・bus word・current wordで、途中失敗の結果をhint配達まで保持し、
  用途take後にだけ失敗・staleへ反映することを照合した。
- seed 0〜9各10,000生成入力＋2400 tick分のdrainを4群へ与え、解除・満杯・再試行を混在させた。

peer通信中の検査はliteralの追加検査で、独立oracleの外部owner模型ではない。
nativeのwireはhostだけの刺激macro。実IRQ・物理wire・時間の成立には読み替えない。
元のcorruptions counterの0だけをmemory保護の証明には使わない。

## ABI費用

[全68構成](x0-event-wire-size-matrix.csv)は、2 ABI×LTO有無×（empty＋直接4群＋通知4群＋各未使用）。
共通入口に通知操作を追加したため、今回のdirect imageは前回N02の入口と同一ではない。
比較は今回の同じ入口内のpairを使い、前回のimageへ数値を寄せない。
R/A/B/C間は同じ通知機能で比較する。

LTOあり、単位B。

| 対象 | 群 | 通知Flash | 直接Flash | 通知静的RAM | 直接静的RAM | 通知の最大単一frame |
|---|---|---:|---:|---:|---:|---:|
| V003 | R | 7728 | 6640 | 788 | 716 | 108 |
| V003 | A | 7736 | 6648 | 788 | 716 | 108 |
| V003 | B | 8192 | 7104 | 820 | 748 | 128 |
| V003 | C-static-batch | 8264 | 7224 | 804 | 732 | 156 |
| X035 | R | 7368 | 6312 | 788 | 716 | 128 |
| X035 | A | 7384 | 6320 | 788 | 716 | 128 |
| X035 | B | 7896 | 6816 | 820 | 748 | 112 |
| X035 | C-static-batch | 7888 | 6896 | 804 | 732 | 192 |

追加objectは68 B。RV32Eのsizeof補助objectでEventExecutor 44、target token 8、
検査counter 12、配達印とpadding 4を照合した。port自体は360→428 B。
最終静的RAM差は72 Bで、.data整列の追加4 Bを含む。全群に68 Bの共通harnessもある。
初期値を持つobjectの.data imageはFlashにも計上している。
検査counterや保持packetを含む試作であり、最適化済み製品APIの最小費用ではない。

未使用32構成の追加image／静的RAM／用途objectは0 Bで、empty binaryと一致。
allocator・constructor arrayは検出されなかった。removeで常設objectのRAMが返ることは示さない。
独立stackは使っていないが、.suの最大単一frameが同じでも、配達経路のstack peakが無費用とはいえない。
startup、IRQ、clock／pin準備、実PD、stack peakと保存予約を含むB1は未検証。

## 再現と残り

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x0_event_wire.py tests/unit/test_workspace.py -q
python3 tests/platform/core_redesign/measure_x0_event_wire.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/<new-event-wire-abi>
```

`EXP06_X0_EVENT_EVIDENCE` は未作成directoryを指定し、入力・生成fake・実行物・gzip traceを保存する。
詳細とarchiveは [測定記録](x0-event-wire-measurements.json)。conftestは追加していない。
移送archiveは `work/exp06-x0-event-20261021-evidence.tar.gz`、11,010,754 B、
SHA-256 `587f63a8596d91e1737ae112d308d5a10feea02e2a9f712c698d5b616fefcd09`。

source／callback contextの動的破棄、end後の公開callback契約は未決定。
この検査でIO-04全体が成立したとは扱わない。
受信tailでの故障とX035準備adapterを用途へ接続する作業はN02で引き続き行う。
handler呼出し回数・実CPU時間・IRQ禁止区間・物理停止・変換品質・実機・利用者評価も未検証。
[教材案](x0-event-wire-exercise.ja.md)を人の理解しやすさの結果とはしない。
