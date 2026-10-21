# N04 X035用固定FreeRTOS portと静的配置

2026-10-22。実装commit `a2793ab`。
[固定入力](../../stack-port-inputs.json)はWCHの[CH32X035 repository](https://github.com/openwch/ch32x035/tree/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/FreeRTOS/FreeRTOS_Core)。
76 file・1,726,158 Bを取得し、Git blob SHA-1とSHA-256・byte数を全件照合した。
今回の目的はportのABIと配置の確認。製品コア、USB、既存実行器を変更していない。
kernelの起動・切替・待機を実行した結果ではない。

## 固定portと今回の構成

固定sourceのkernelはV10.4.6。WCHのPFIC extension、Software IRQ、SysTick、`mscratch`を使う。
一般的なRISC-V portをそのままX035へ使えると仮定せず、WCH版C／assemblyを変更せずに組んだ。
compilerはxPack GCC 14.3.0-1、RV32IMAC／ILP32、LTO有無。
vendor compilerの実行結果や、全silicon revisionへの適合を証明するものではない。

今回の設定はvendor demoの設定と異なる。
静的確保のみ、優先度3段階、software timer・mutex・task notification・heapは含めない。
協調設定／preemption設定を別条件にし、各task stackは64／128／256 wordの候補値。
これらは最小動作量でも、実測から選んだ安全な予算でもない。

仮想I/Oの記述は、入力待ち→要求→完了待ち→表示の流れ。
4個の容量1 queue（uint32 payload）と次の2構成を記述した。

- アプリ2 task：workflowが入力・要求・完了・表示、backendが要求と仮想完了を扱う。
- アプリ3 task：入力・backend・表示を別taskにする。

どちらもidle taskが追加される。協調設定のFreeRTOSでも「全体で2 stack」とは呼ばない。
fixtureの外部入力入口は保持するがvectorを与えず、物理割込みとして実行していない。
同じ入力列での実行順・満杯・timeout・切断の挙動一致は、このlink比較では未検証。
2 stackだけで切り替える協調方式との対照も別途必要。

## 起動／IRQ用stackの予約修正

固定[`portASM.S`](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/FreeRTOS/FreeRTOS_Core/FreeRTOS/portable/GCC/RISC-V/portASM.S)
の最初のtask開始は、`xISRStackTop - 512`を`mscratch`へ設定する。
SysTickのC handlerは`sp`と`mscratch`を交換し、software IRQのassembly入口は別経路でIRQ上端へ切り替える。
このため、上端から512 Bだけを予約してもSysTick用spの下のstackは確保できない。

最終版ではRAM末尾へ1,024 Bを予約し、上端−512 Bの下にも512 Bを置いた。
起動前stackとIRQ用領域は同じ予約を再利用し、両方として二重計上しない。
整数contextの120 Bは各task stackの中で使い、別の静的領域として加算しない。
portの512 Bオフセットと、その下の512 Bは別項目。後者を観測peakとは呼ばない。

使用24行で、ELF内`xISRStackTop`がRAM上端を指すこと、予約の開始／終了、
最初のtask開始での512 B減算、SysTickの2回の`mscratch`交換と`mret`を確認した。
静的領域と予約が重ならないlink assertionも置いた。
実割込みの優先度、nested IRQ、compilerのC割込み入口、必要なstack量は実機で別検証する。

## 構造測定

[最終50構成](stack-port-size-matrix.csv)はLTO有無×2 scheduling設定×2 task構成×3 stack容量×使用／未使用＋empty。
heap／allocator・constructor arrayは検出されなかった。
未使用24行はemptyと同じ8 B image、静的RAM0 B。
共通の起動／IRQ予約1,024 Bはemptyにもあり、未使用の総RAMが0 Bという意味ではない。

X035、LTOあり、各stack128 word＝512 B。単位B。

| scheduling設定／アプリtask数 | Flash image | 静的RAM | task stack配列 | TCB合計 | queue合計 | 起動／IRQ予約 | RAM総予約 |
|---|---:|---:|---:|---:|---:|---:|---:|
| 協調／2 | 4088 | 2268 | 1536 | 180 | 320 | 1024 | 3292 |
| 協調／3 | 4088 | 2844 | 2048 | 240 | 320 | 1024 | 3868 |
| preemption／2 | 4344 | 2268 | 1536 | 180 | 320 | 1024 | 3292 |
| preemption／3 | 4344 | 2844 | 2048 | 240 | 320 | 1024 | 3868 |

TCBはこの設定で1個60 B、queue controlは1個72 B。
queue合計320 Bはcontrol288 B＋payload16 B＋handle16 B。
idleのstack／TCBも表に含む。残りはscheduler状態・fixture観測値・整列等。
アプリ2 taskの総予約は64／128／256 wordで2,524／3,292／4,828 B。
アプリ3 taskでは2,844／3,868／5,916 B。

今回のFlash・静的配置はX035の62 KiB／20 KiB mapに収まった。
clock／pin準備、vector、実I/O driver、永続化領域、全用途を加えたfitや、stackの安全性は未証明。
先の継続型toyとは機能量・queue・entry・予約が異なるため、Flash差をRTOSだけの費用としない。
`.su`は保持されなかった関数を含むcompile時の情報もあり、最大報告frameを実stack peakにしない。
切替cycle・待機latency・task／IRQ stack peakはnull。

## 故障検査と途中結果の扱い

- 固定port assemblyをRV32E／ILP32Eへ渡すと、x16以降の命令でAssemblerが拒否。
  このportをそのままV003へ適用できない証拠であり、他のRV32E portやFreeRTOS一般の不可能性ではない。
- RAMを人工的に1,024 Bへ縮めたlinkは、静的領域と起動／IRQ予約の重なりで拒否。
  別の物理partを検証した結果ではない。
- vendor `port.c`の同長1 byte改変はcompile前のhash照合で拒否。
- workspace3件成功、既存warning6件。用途のnative実行件数は0。

初回はdebug include path不足で0行、2回目はLTOがfixture入口配列を除去し26行で停止した。
入口に`used`を付け、link rootの保持を明示した3回目は50行を生成したが、
起動／IRQ予約が512 BでSysTick用spより下を含まなかったため、最終配置の根拠に使わない。
最終4回目の50行は予約を1,024 Bへ修正したもの。
3回目との全50 imageは一致し、違いはNOLOAD予約とその検証。予約がbinaryに出ないことも確認した。
予備runを成功build数へ足さない。

## 再現と残り

固定URLの76 fileを同じ相対配置へ置く。lockはfileごとのURL・hashを持つ。

```sh
python3 tests/platform/core_redesign/measure_stack_port.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-gcc \
  --source-dir work/<locked-vendor-tree> \
  --out work/<new-stack-port-abi>
```

測定scriptは通信せず、固定sourceを照合・copyして使う。
[記録](stack-port-measurements.json)にsource lock、tool、全command、ELF、予備run、負例、archiveを保存した。
[教材案](stack-port-exercise.ja.md)は人による評価前。
N04は未完了。次は本当に2 stackで動く協調portの適合・配置と、全方式で同じ仮想I/O列を扱う対照。
実切替・待機・stack観測はN09の設備・fixture条件へ残す。RTOSを提供する判断はP5で行う。
