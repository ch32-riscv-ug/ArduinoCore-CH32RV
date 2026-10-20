# 取得buffer 2 blockの所有と回収

2026-10-20。P4の境界確認用のhost／ABI実験。方式・公開APIの採用はP5へ渡す。
コア本体、USB stack、実ADC／DMAは変更していない。
[入力](../../acquisition-inputs.json)は16 bit×64 sample×2 blockを必須とし、8／128 sampleを感度比較に使う。

## 検査した契約

`Free → Producer → Ready → Consumer → Free`の4状態で、生成完了順のFIFOと明示的な返却を扱った。
予約順と完了順が異なっても、完了順に取り出す。Consumerが2 blockを保持でき、逆順の返却も許す。
満杯の予約はBusyで失敗し、既存の内容と出力tokenを変更しない。

| 操作 | 状態の意味と許可範囲 |
|---|---|
| reserve | 空き1 blockをProducerへ貸す。受理時だけticketを進める |
| write | fake生成元が既知のprefixを1 sampleずつ書く。Producer以外へ書けない |
| cancel | 取消要求を記録する。所有はProducerのまま。停止するまで追加書込みがあり得る |
| hardware_terminal | 停止済みという宣言と既知prefix長を受けてReadyへ移す。宣言なしでは拒否 |
| peek／ack notice | 再通知用の状態を確認／解除する。Readyの結果やblock自体は回収しない |
| take | ReadyをConsumerへ渡す。通知が届かなかった場合も結果は残る |
| sample | Consumerだけが既知prefixを読み出せる。失敗時は出力値を保持 |
| release | 正しいticketのConsumerだけをFreeへ戻す |
| end | 新規予約・通知を止め、Producerへ取消要求。貸出中・完了済み・利用中を解放しない |
| open | 全blockの返却後のみ再開。ticketはリセットしない |

成功は全sampleの生成を要求する。失敗と取消では既知prefixだけを公開し、以前の取得の末尾を公開しない。
取消結果には取消要求が必要だが、要求と成功が競合した場合に成功が先に確定することも許す。
終端は1回だけ。end後も停止通知・take・読出し・返却は許すため、閉じたまま回収できる。

ticketはpool内で単調増加する32 bit値で、周回させない。上限では空きがあってもExhaustedとなる。
8 bitの試験型で255回の寿命と再開を検査した。古い返却／通知確認が、新しい利用へ作用しないことも検査した。
tokenは**同じpool instance・同じobject寿命でのみ使う前提**であり、別instanceのtoken取り違えを防ぐ所有者識別は未実装。
poolはcopy／move不可で、破棄前にハードウェア停止と回収を完了することが前提。destructorが停止を代行する方式ではない。

## Hostと生成負荷

ASan／UBSan、警告をerrorとするnative検査50件と、workspace規則3件が通過した。
独立したPythonのlist模型に操作結果・両blockの状態・読出し可能な全sampleを照合した。
3容量×8 seed×500操作の生成trace、完了順変更、逆順返却、出力保持、部分生成、取消、end、通知不達、ticket上限を含む。
tests配下へconftest.pyは追加していない。

40回の抽象的な取得機会を与え、利用側を一定間隔でのみ回収させた。
stall 0／3／12は各々1／4／13機会ごとの回収を意味し、最初の機会は回収する。
各取得は64 sampleで、通知の外部配信先は使わず、後からtakeで回収する。
最後のend後の残りは読出しせず回収する。各条件で受理数＝take数＝release数だった。

| 回収間の停止機会 | 受理block／40 | 開始を見送る機会 | 停められない入力での既知欠落sample |
|---:|---:|---:|---:|
| 0 | 40 | 0 | 0 |
| 3 | 21 | 19 | 1,216 |
| 12 | 7 | 33 | 2,112 |

表の後半2列は**別の入力契約**である。software-rearmでは空きがない間は生成を始めない。
結果JSONの`deferred_starts`は開始を見送った機会数であり、後で実行するrequestを保持した数ではない。
この模式入力で明示的に失ったsampleは0と数えるが、外部信号を観測しなかった時間の情報まで保持できた意味ではない。
unavoidable-arrivalでは毎機会64 sampleが届いたと宣言し、受理できなかった量を欠落として数えた。
Busy回数から実機の物理欠落量を自動推定してはいけない。両者のpool操作traceは同じでも、入力側の意味が違う。
これらは時刻・sample rate・ADC throughputの測定値ではない。

## ABIの費用

GCC 14.3.0、`-Os`、V003 `rv32ec_zicsr/ilp32e`とX035 `rv32imac_zicsr/ilp32`、LTO off／on。
empty・payloadだけ・pool 3容量・未使用pool 3容量の計32 ELFをlinkした。
ELF／bin・map・disasm・nm・section・出力された単一frameを記録した。
constructor配列とallocator symbolがないことを検査した。supportのmemcpy／memsetはLTOなしでbuildし、必要分をimageへ含める。

| 容量（sample／block） | sample領域 B | pool全体 B | 所有管理分 B |
|---:|---:|---:|---:|
| 8 | 32 | 76 | 44 |
| 64 | 256 | 300 | 44 |
| 128 | 512 | 556 | 44 |

| 部品・容量 | V003 image B LTO off／on | X035 image B LTO off／on |
|---|---:|---:|
| empty | 8／8 | 8／8 |
| payloadだけ64 | 88／88 | 80／80 |
| pool8 | 1,056／1,024 | 992／1,000 |
| pool64 | 1,064／1,032 | 1,000／1,000 |
| pool128 | 1,064／1,040 | 1,000／1,000 |

pool側は全操作をvolatile引数から使う入口で、追加の観測・引数・結果storageは86 B。
link配置のpadding込みで静的RAMは164／388／644 Bになる。実際のlibrary利用時のRAMをこの値で固定しない。
payload側は所有・取消・通知の機能を持たず、同等能力の実装として速度やimageを順位付けしない。
未使用poolの12条件はemptyとimage hashが一致し、追加Flash／RAM 0 Bだった。
出力された最大の単一frameはV003 28 B、X035 80 B。呼出し全体・割込み・startupを含むstack peakではない。

## 実機と次の準備で確かめること

- `quiescent=true`はfake adapterの宣言にすぎない。実DMA停止、将来のdescriptor参照なし、書込み可視性、完了IRQとの競合を根拠にする必要がある。
- prefix writeの禁止で循環DMAは止まらない。DMAがConsumerの領域へ戻る前に停止／別領域へ設定する契約を検査する。
- 4 byte alignmentは今回のlayout仮説。系列ごとのDMA alignment、cache lineと共有memoryは別確認。
- 通常文脈で直列操作する模型であり、ISR／core間のmetadata更新、memory barrier、排他時間を測っていない。
- ADC連続取得・外部trigger停止・overrun・時刻付与・欠落counter・buffer所有者識別・完了枠予約との結合を比較する。
- 複数block管理44 Bの一部は縮約できる可能性があるが、世代や結果保持を除いたものは能力差を明記する。常設診断counterは今回のpoolに含めず、負荷集計は外側で行った。

汎用固定poolの貸出と、生成元が停止するまで返せないI/O bufferの寿命は別の機能である。
今回の数値だけで汎用pool・取得専用class・共通request層の採否を決めない。
[教材](acquisition-exercise.ja.md)、[全ABI行](acquisition-size-matrix.csv)、[再現記録](acquisition-measurements.json)をP5の判断材料へ渡す。
