# 直接回収とイベント配信の結合費用

2026-10-21。実装commit `695d06d`。
[入力](../../completion-delivery-inputs.json)、[全28 ELFの表](completion-delivery-size-matrix.csv)、
[hash・host・差分の台帳](completion-delivery-measurements.json)を保存した。
[前回の配信結合](completion-event-result.ja.md)の接続処理を実験用headerに分け、hostとABI probeで共有した。
製品コア・USB・Arbiter／IoSlot／EventExecutor本体は変更していない。

## 何を比較したか

両構成で利用者3、buffer32 byte×3、専用3 record／共有2 record、既存のfake I/Oを使う。
共通のprobe操作はopen／end／submit／dispatch／finish／cancel／notice／ack／take、buffer read／write、
record観測、通知が有効なときだけtakeする回収処理と、その呼出し・成功・棄却counter。
直接構成ではcallerがtokenを指定して回収処理を呼ぶ。
配信構成には1宛先・FIFO容量1のEventExecutorと宛先tokenを加え、bind／remove／initialize／pump／turnを呼べるようにした。
イベントhandlerは同じ回収処理を呼ぶ。

共通header `completion_delivery.hpp` のpumpは、元の要求のnoticeを確認し、
NotPending／Accepted／Full／Staleを返す。enqueue成功時にも元の通知をackしない。
collectはnoticeを再検証してtakeし、回収できない場合は出力を変更しない。
これらは直列文脈の実験用関数で、公開APIや普遍的なack policyの採用ではない。

配信構成は登録・解除・pump・turnという追加経路を持つため、Flash差分にはそれらとprobeのswitch分岐も含む。
「イベント1回分の命令数」や、同じ用途を完成させたアプリ同士の比較値ではない。
直接構成で複数sourceをどう探索するか、その探索費用もこのprobeには含めていない。

## 測定値

LTOあり。imageはprobe全体のbinary長、RAMは.data＋.bss。どちらにも共通harnessを含む。

| ABI | record | 直接image B | 配信image B | 増分 B | 直接RAM B | 配信RAM B |
|---|---|---:|---:|---:|---:|---:|
| V003 rv32ec／ilp32e | 専用3 | 2,008 | 2,728 | 720 | 344 | 396 |
| V003 rv32ec／ilp32e | 共有2 | 2,016 | 2,728 | 712 | 308 | 360 |
| X035 rv32imac／ilp32 | 専用3 | 1,792 | 2,472 | 680 | 344 | 396 |
| X035 rv32imac／ilp32 | 共有2 | 1,808 | 2,472 | 664 | 308 | 360 |

全使用構成で、Arbiter本体は専用116 B／共有80 B、保護付きbuffer3個は108 B、共通harnessは117 B。
配信構成だけにEventExecutor44 Bとtarget token8 Bが加わり、両ABI・LTO off／onでRAM増分は52 Bだった。
object sizeの和とsection合計の差はalignmentによる。全28構成で.dataは0 B。
専用3と共有2は未回収容量・受理条件が違うので、36 Bの削減だけを見て同等と扱わない。

LTOなしのimage増分はV003で専用696 B／共有688 B、X035で専用736 B／共有728 B。
LTOの有無による最適化は構成ごとに違い、差分が必ず縮むとは限らない。
`-fstack-usage`の最大単一frameは、V003の直接36〜40 B・配信56〜60 B、X035の直接80 B・配信112 Bだった。
呼出し鎖・割り込み・アプリのlocal変数を合算したstack peakではない。

未使用の専用／共有×2 ABI×LTO off／on、計8構成では、
Arbiter・buffer・EventExecutor・targetのobjectも追加RAMも0 B、image hashは空の8 B probeと一致した。
これは今回のsection GCとlink条件下で、参照しない静的構成を除去できた証拠。
同じinstance内の機能をruntime boolでoffにした場合や、起動時に自動登録する場合の0 B保証ではない。
全28 ELFでconstructor array・allocatorの混入も検査した。

## Host側の再検査

共通接続処理を使う形で、ASan／UBSan・警告errorのnative76件＋workspace3件、計79件が通過した。
前回74シナリオに、終端前のNotPending、解除済みtargetへのStaleとpending保持、
回収拒否時の出力保持を両record方式で追加した。
前回binaryでも元の74入力を再実行し、新binaryの74 stdoutとbyte単位で一致した。
Arbiter／IoSlot／EventExecutor／WorkExecutor／データ部品の入力hashは前回と同じ。
fixtureはtest module内に置き、conftest.pyは追加していない。

```sh
env EXP03_COMPLETION_EVENT_EVIDENCE=work/exp03-completion-delivery-host-NEW \
  uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_completion_event.py tests/unit/test_workspace.py -q

python3 tests/platform/core_redesign/measure_completion_delivery.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp03-completion-delivery-abi-NEW
```

出力先は新しいdirectoryを使う。
ABIはentryと非LTOのmemcpy／memset supportをlinkし、使用したlibgccもhashで固定する。
ELF・binary・map・symbol・section・disassembly・単一frame・全command、compiler／binutilsのhash、入力snapshotを残した。
独立link用のentryで、reset／CRT・vector・実peripheralを持つ起動可能firmwareの検証ではない。

local証拠archiveは `work/exp03-completion-delivery-20261021-evidence.tar.gz`、1,148,894 byte、
SHA-256 `3189b1d0b9c2df0cf171038eb4f40a1d4981f85b78ba0c5a96e327a8a4db8402`。
新host76 trace・binary、旧hostのsnapshot／binary、旧binaryの74再実行、ABI28構成、集計scriptと検証結果を含む。
追跡する台帳にはarchive hashと全28行、host入力hash、76 traceのhashを保存した。

## 次に残るもの

今回の固定1宛先は、小型用途で共通配信を入れる際の費用を検討する材料。
直接poll・集約bit・payload FIFOの採否、system専用かuser公開かは、この数値だけでは決めない。
後続の[metadata fan-out](completion-fanout-result.ja.md)では1／2／4宛先・容量1／2のnative356件・ABI108を追加した。
受信byteの共有や同じtokenからの競合回収とは区別する。用途全体の探索・状態管理を含む比較、実I2C backendの回収分離、
ISR共有、MCU実行時間とstack観測は残る。時間保証は追加していない。
[前回の教材案](completion-event-exercise.ja.md)も、この費用表を使った予測課題へ接続できるが、人による評価は未実施。

後続の[資源管理付きI2Cとの接続](i2c-event-result.ja.md)では、この配信glueを変更せず、既存register engine・Domain・固定callback2発生元へ接続した。実験register模型での停止／reap／close後の配達と別owner通信中の旧結果回収、同値tokenを別sourceへ誤配信する反例を記録した。実IRQ／wireや発生元識別方式の採用判断は含まない。
