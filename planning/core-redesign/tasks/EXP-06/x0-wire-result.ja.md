# N01 X0用途と型付きwire adapterの接続

2026-10-21。実装commit `472a958`。固定条件は [x0-wire-inputs.json](../../x0-wire-inputs.json)。
R/A/B/C-static-batchの同じ用途ロジックへ、SSD1306のcommand／dataとINA219のpointer／RXを接続した。
以前の[表示交換実験](../EXP-05/display-result.ja.md)と4 golden、共通headerは変更していない。
製品コア・USB・Networkは編集していない。実験内部の境界であり、公開APIの採用ではない。

## 接続した処理

共通の `WireX0Meaning` がPPS操作、入力容量4、表示、100 tick周期の計測、20 tick周期の仮想touchを持つ。
Rは専用inbox、AはQueue、BはWorkExecutor、Cは従来と同じ有限batchの固定宛先。
全群とも同じ受理、最新Apply、session、文脈、1回の処理単位、round-robinを使う。
Cを動的宛先の汎用EventExecutorと同機能の候補にはしない。

`X0WireFake` は1件を受理し、TXを最大33 Bコピーする。RXはbus内の8 B枠へ書き、
明示takeまで保持する。constの送信入力を受信先へ流用しない。
take前はbusyで、間違ったowner・容量不足・未終端の回収は拒否。
失敗時は呼出し側の受信先を更新せず、終端の二重回収も拒否する。
これは今回のコピー方式の費用であり、全I/Oにコピー方式を採用したわけではない。

### 表示

128×64のpage順1024 B・既存glyph・payload32 Bを維持した。
各chunkは横方向addressモードとcolumn／page範囲を設定する9 B commandを送り、
別transactionでcontrol byte `0x40`＋32 B dataを送る。
commandとdataの間にもbusを別ownerへ返す。frame全体をbus所有期間にはしない。
同じdisplayを別のwriterが設定しない条件で、他ownerはINAと仮想touchのみ。

command受理時からchunkのScreen状態を保持する。commandが失敗すればdataを送らずframeを中断。
data失敗でも部分frameを完成扱いせず、100 tick後の再試行へ進む。
成功したframeは独立したwhole-image rendererと全byte、表示hash、revisionを比較した。
1 frameはcommand32＋data32の64 transaction、address込み1408 B／12672 SCL clockの模型に対応する。
これは[device資料照合](x0-device-result.ja.md)のchunk-window条件で、実時間ではない。

full initは含まない。panel電源、COM／SEG、charge pump、orientation、STOP後の実pointer保持は未確認。
frame-window方式との再順位付けやcontroller設定の採用も行っていない。

### 計測

7 bit address `0x40`へ、pointer `0x02`＋2 B RX、pointer `0x04`＋2 B RXの2 transactionを使う。
両方のRXはMSB first。bus wordの上位13 bitを4 mV単位、current wordをsigned 16 bitとして解釈する。
currentの1 mA/LSBは、前回固定した上流calibration 4096と10 mΩの宣言を仮定したもの。
このadapterはcalibration／configurationを書かず、既に設定された機器の模型として扱う。

一方だけの成功では新しい値を公開しない。失敗したpairは旧値を保ちstaleにし、
両方成功時に計測payload8 Bと用途の値を更新する。
画面・計測結果と、途中まで受信したraw wordは別の状態。

値はsynthetic completion時に取得する。submit時、take時、2回のread間でsourceを変える追加検査により、
bus word 14000とcurrent word 65506から7000 mV／−30 mAとなる例を保存した。
この例は2つのregisterが異なる時点の値になり得ることを示す。
transport成功によるstale解除を、新しいADC変換・同期snapshot・校正済みの実測値という保証にはしない。
CNVR clear解釈、OVFを含むanalog品質、変換世代は未決定。
公開する測定品質の契約が必要かはP5の判断材料として残す。

touchは8 B・50 Hz相当というP1の論理負荷を維持し、`VirtualTouch` と明示した。
実controllerのaddress／command／受信処理や、物理50 Hzを実装した扱いにはしない。

## 検証

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x0_wire.py tests/unit/test_workspace.py -q
python3 tests/platform/core_redesign/measure_x0_wire.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/<new-x0-wire-abi>
```

最初のrunはnative42＋workspace3＝45件成功。追加の異時点RX検査4件も成功。
合計native46・workspace3を2 runで確認し、追加runのnative実行物hashも初回と同一。
初回のtest source snapshotは追加4件前の版として保存している。
既存pytest plugin由来warningは初回90、追加8件。remote CI成功を示すものではない。

- 84 trace・743,116操作・43,976受理packetを保存。21入力群すべてでR/A/B/Cの全観測行が一致。
- 独立oracleは入力、whole image、transaction台帳から期待packetとRXを生成し、実装の観測結果を正解へコピーしない。
- command／data／bus word／current wordの各失敗位置、再試行、前値＋stale、取消の二重要求、古いPD終端、再接続を検査。
- 満杯では4入力だけ受理。M/S/U、system／user参加、setup／ISR／callback禁止、1000 tickの通常進行停止を検査。
- seed 0〜9それぞれ10,000生成入力＋2000 tick drainを4群へ与えた。全interleavingや実IRQの証明ではない。
- transport単体では不正・未対応入力、TXコピー後の元buffer変更、早すぎるtake、owner／容量不一致、sticky終端、失敗RX不変を検査。

fakeは全transactionを2 tickで完了する。旧DisplayBusの長さによる模型とは時間条件が異なる。
tickをmsやwire周波数へ換算しない。cancelはfakeを即時終端にするだけで、実hardware停止は保証しない。
故障注入はtransaction単位で、途中byteの有効prefixや実NACK／BERRの区別は含まない。

## ABI費用

[全68構成](x0-wire-size-matrix.csv)は2 ABI×LTO有無で、empty・新4群・旧mock4群・各未使用を測定。
比較入口は共通のoperation／argument／observedを持ち、RXから更新した用途の値を観測する。
単独entryでありstartupは含まない。

LTOあり、単位B。RAMは初期値付き.dataも含む静的使用量。

| 対象 | 群 | Flash image | 静的RAM | 用途object | 最大単一frame |
|---|---|---:|---:|---:|---:|
| V003 | R | 3768 | 468 | 396 | 104 |
| V003 | A | 3776 | 468 | 396 | 104 |
| V003 | B | 4024 | 500 | 428 | 128 |
| V003 | C-static-batch | 4096 | 484 | 412 | 156 |
| X035 | R | 3248 | 468 | 396 | 112 |
| X035 | A | 3264 | 468 | 396 | 112 |
| X035 | B | 3760 | 500 | 428 | 112 |
| X035 | C-static-batch | 3784 | 484 | 412 | 176 |

全構成にfakeと検査counterを含み、harnessのoperation＋argument＋observedは68 B。
R／Aは.data 400 B＋.bss 68 Bで468 B。用途object396 Bとの差4 Bは.data整列。
最大単一frameは.suであり、call chain・IRQ込みstack peakではない。
Flashには.dataの初期値imageも含め、RAMには別に数える。
全体startup／IRQ／実backend・stack／保存予約を含むB1成立とは扱わない。

新方式の追加RAMはLTOありで旧mock対比R/A＋40、B/C＋48 B。
旧mockはreceive slotや2回のregister read、controller headerを持たず、同機能対照ではない。
この差は不足機能を加えた費用であり、実行方式の優劣や物理処理速度の比較には使わない。
未使用32構成は追加image／静的RAM／用途objectが0 Bで、emptyのbinaryと同一。
allocator・constructor arrayへの依存は検出されなかった。登録後のRAM回収を示すものではない。

初回ABI runは入力記録が旧display-inputsを参照していたため正本に使わず保存。
正しいx0-wire-inputsへ直した68構成を再生成し、case名のC表記を正規化後、
全68 ELF・load image・静的RAMが初回と同一であることを確認した。
数値を旧結果に合わせる書換えは行っていない。

## 再現と残作業

[測定記録](x0-wire-measurements.json)に入力hash、tool、JUnit、全trace、ABIとarchiveの参照を記録。
移送archiveは `work/exp06-x0-wire-20261021-evidence.tar.gz`、8,488,223 B、
SHA-256 `8ed2bcc95094143d20fcb462aacff742ddd413b00406cda5cd602814d0b3fc33`。
`EXP06_X0_WIRE_EVIDENCE` に未作成directoryを指定するとnative実行物・入力・gzip traceを保存する。
コンパイラ用TMPDIRは短い専用directoryを使い、共有/tmpの容量やpytest socket長の影響を避ける。

[完了監査](../P4-AUDIT-01/result.ja.md)のN01を、このhost／ABI範囲で実施した。
次はN02：このtyped境界を既存I2C register engine・資源台帳へ接続し、全群で同じwireを維持する。
実IRQ、停止と部分RX、結果保持、実pin／clock、PD相手、実機時間と電流は引き続き未検証。
[変更課題](x0-wire-exercise.ja.md)も利用者レビュー前。P4全体や最終仕様は未完了。
