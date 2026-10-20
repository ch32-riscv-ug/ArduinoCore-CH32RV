# EXP-01 I2C masterの有限stepとbuffer貸出

2026-10-20。試作commit `5070b6f`。製品coreとUSBは変更していない。
V003／X035向けregister状態機械のhost検査と、独立runtimeへのlinkを追加した。
実通信・起動・clock精度・pin設定・IRQ共有の成立は未検証。実行方式や公開APIの採用判断はP5に残す。

## 資料と比較範囲

[固定入力](../../i2c-inputs.json)はWCH SDKのI2C実装・7-bit例とRMを参照する。
URL・版・bytes・SHA-256を保存し、既存の[part／startup入力](../../runtime-inputs.json)も使う。

- V003：[SDK I2C source](https://raw.githubusercontent.com/ch32-riscv-ug/CH32V003/12d9aed0eae0276a133db00eb16bd492e8189f2d/EVT/EXAM/SRC/Peripheral/src/ch32v00x_i2c.c)、[RM](https://raw.githubusercontent.com/ch32-riscv-ug/CH32V003/12d9aed0eae0276a133db00eb16bd492e8189f2d/datasheet_en/CH32V003RM.PDF) §13。
- X035：[SDK I2C source](https://raw.githubusercontent.com/ch32-riscv-ug/CH32X035/327874484eda7e50cb4cf40271718e68fafbc850/EVT/EXAM/SRC/Peripheral/src/ch32x035_i2c.c)、[RM](https://raw.githubusercontent.com/ch32-riscv-ug/CH32X035/327874484eda7e50cb4cf40271718e68fafbc850/datasheet_en/CH32X035RM.PDF) のI2C章。

両系列のI2C registerは共通のoffsetを使い、16 bit register間にpaddingがある。TRISEは使わない。
100kHz／400kHz要求に対し、整数MHzの宣言PCLKからCCRを切上げ計算する。
PCLKは2〜48MHz、400kHz時は4MHz以上の入力だけを受理する実験条件とした。
実測したbus速度や全系列での動作保証ではない。

対象は7-bit unicastのwrite、read、同じaddressへのwrite→repeated START→read。
1件ずつ受理し、1つのbufferのtx領域の後ろへrx領域を置く。tx＋rxが容量を超えれば拒否する。
10-bit、slave、DMA、IRQ、SMBus／PEC、完全なmulti-master動作、bus recoveryは未実装で、製品の対象外と決めたものではない。

初期化済みclock・pin、周辺の排他的所有、直列呼出しが前提。
`owned`は利用者の宣言であり、他libraryの所有を検出するmutexではない。
target入口のcritical policyは、独立resetがmaskable IRQを止めたままであることに依存する。
IRQが動く製品coreへそのまま移植できる保護policyとは扱わない。
pin・RCC・remapの設定を自動実行せず、以前のclock試作ともまだ結合していない。

## 明示的な進行と終端

`open`→`start`→繰返し`step`→終端通知→`take`の経路を実装した。
1回のstep内で相手のreadyを待つwhile、sleep、callback、taskの実行はしない。
1回の呼出しは有限回のregisterアクセスで戻るが、MMIOの実時間や要求全体の完了時間は保証しない。
要求が進まなければpendingが続く。時間timeout・WDT・強制resetをこのclassは追加しない。

- writeはDATARへ1 byte渡した後BTFを待つ。次のbyteを渡す前に、観測済みprefixを進める。
- readの1 byteはACK／POSを落としてADDRを解除し、STOPを要求してRXNEを待つ。
- 2 byteはADDR解除前にPOS／ACKを設定し、解除直後にACKを落とす。BTF後にSTOPと2 byteの読出しを行う。
- 3 byte以上は末尾3 byteを別段階に分け、BTFでACK解除と1 byte読出し、その後のBTFでSTOPと残り2 byteを読む。

ADDR解除や受信末尾の短い順序をcritical policyで囲む。hostではアクセス順とguardの均衡を検査した。
ST製品など別MCUの受信例を、そのままこのregister模型へ置き換えたものではない。

取消は要求だけを記録し、回収は行わない。writeの送出中はBTF観測まで待つ。
readのADDR処理後は受信を最後まで進め、取消と競合して成功する場合がある。
STOP経路ではSTART／STOP commandとMSLの解除、残る受信場所のdrainを確認してから終端化する。
BUSYが別masterに属する可能性があるため、BUSY=0を終端の必要条件にはしていない。
ARLOではSTOPを要求しない。これはlocal register模型の停止判定であり、線の解放や実機での停止成立の証明ではない。
進まないcontrollerの回復と、回復後に安全にbufferを返す手順は別に検討する。

## 貸出・通知・回収

保護付きbufferはowner pointerを持ち、受理から`take`まで利用者のread／writeを拒否する。
raw側は同じengineを使うが、貸出中も利用者からアクセスでき、寿命と書換え禁止を呼出し側が守る。
両者の容量差は保護の差であり、同じ安全性を提供する実装同士の比較ではない。

完了通知はstickyで、`peek`／`ack`と`take`を分けた。通知を消してもbufferは返らない。
`end`は受理と通知を閉じ、稼働中なら取消要求を残す。閉じた後もstepと回収が必要で、回収前の再openは拒否する。
世代は受理ごとに進み、openで巻き戻さない。上限では再利用を拒否する。
8 bit版で255件の反復を検査し、通常ABI入口は32 bitを使う。
objectはcopy／moveせず、要求・回収までbufferとdriverの寿命を保つ。destructorによる自動停止はない。

結果は`issued`、`confirmed`、`received`、error flagsを分けた。
`issued`はDATARへ渡したbyte数、`confirmed`はerrorを先に観測せずBTFで確認できたprefix。
失敗時の`confirmed`は既知の観測であり、相手機器が実際に受け取った数や副作用の正確な数ではない。
受信失敗後のdrainはbufferへ公開せず、受信済みprefixだけを`received`へ数える。
以前のfake IoSlotのfill処理やexact-byte terminal判定とはまだ接続していない。

## 検証

- ASan／UBSan付きhost：I2C **59件**、workspace **3件**が成功。conftestは追加していない。
- read 1／2／3／4／8／32／64 byte、write、combined、取消、end、5種類のerror、通知保持、stale世代、残るFIFOのdrainを検査。
- 固定8 seed×12要求をraw／保護付きへ同じ順で与え、payload・結果・address列を照合した。正しいpeer進行の生成で、電気的なfault注入ではない。
- V003／X035×LTO off／on×9 caseで**36 ELF**。命令harnessは`experiment_main`の直前までを検査し、mainと実MMIOは実行しない。
- 既存runtime **40構成のbinaryが以前と一致**。constructor無効・RAM／Flash超過の想定リンク拒否**12件**も維持。

[CSV](i2c-size-matrix.csv)と[JSON](i2c-measurements.json)に全行、tool／入力hash、証拠を保存した。
空構成との差には全APIを呼ぶ測定入口も含まれ、I2C関数だけの増分とは呼ばない。

| LTOあり | raw 32 byte Flash／静的RAM | 保護付き32 byte Flash／静的RAM | 保護追加差 |
|---|---:|---:|---:|
| V003 | 2,356／100 B | 2,400／104 B | 44／4 B |
| X035 | 2,292／100 B | 2,320／104 B | 28／4 B |

全使用行でdriver objectは28 B、raw bufferはpayloadと同じ容量、保護付きbufferはpayload＋4 B。
測定入口のvolatile変数40 Bも静的RAMの表に含む。8→32 byteでは静的RAMが24 B増えた。
未使用raw／保護付きの**8条件は追加Flash／RAM 0 B**で、object symbolも残らなかった。
この条件でallocator symbolはない。V003で必要になった整数演算のlibgccはlink・hash・容量に含めた。
初回の補助library不足のlink失敗も診断記録として残し、成功記録はr2を使う。
`.su`の最大単独frameはV003で20 B、X035で48 B。libgccを含むcall-chainやIRQのstack peakではない。
予約stack 256 Bが十分であるとの結論は出していない。

raw比較も同じC++状態機械を使う。CH32FUN、vendorのblocking例、用途専用の直接register実装との比較は未実施。
この結果からArduino Core全体の重さや、採用すべきAPIを決めない。

## 再実行と残る作業

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_i2c_master.py tests/unit/test_workspace.py \
  -o junit_family=xunit1

python3 tests/platform/core_redesign/build_minimal_runtime.py --suite i2c-master \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp01-i2c-recheck
```

出力先は未作成の場所を使う。`--sources 保存先`で取得済み固定参照を指定できる。
証拠archiveの場所・bytes・SHA-256はJSONの`archive`に記録している。

通常作業で続ける対象は、clock／pin／bus所有の結合、ISRと完了recordの共有、旧IoSlotとの結果意味の調整、
busyを返す基準と固定arbiter／完了枠の比較、timeoutと回復、実際の表示用途への接続である。
[clock・pin利用権との接続](i2c-resource-result.ja.md)は直列台帳で追加検査した。実PCLK・pinmuxとIRQ共有は未接続。
実機ではSCL／SDA、受信末尾、NACK／STOP、線を保持する相手、IRQ介入と占有時間を確認する。
slave、DMA等も別の比較として残し、今回未実装という理由で対象外に決めない。
[利用者向けの貸出・小分け処理課題](i2c-loan-exercise.ja.md)を追加したが、人による教育効果の評価は残る。
