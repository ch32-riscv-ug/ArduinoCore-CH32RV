# P2 の対象と対照ロジックの固定

版P2-host-1、2026-10-20。[比較契約P1-1](experiment-spec.ja.md)に従う準備単位。
型番・資料・入力・描画データと、hostで動くR対照の構成を固定する。
K0とX0の状態ロジックは用意したが、実周辺backendとX0全体の進行を結合した対照アプリはこれから。
この準備単位の完了を、P2全体の完了とはしない。

## 対象と一次資料

型番選定は実験の構成であり、製品の対応範囲を決めるものではない。
JSONの固定値と資料hashは [p2-inputs.json](p2-inputs.json)にある。
資料はWCH発行のPDFとSDK headerで、固定commitのmirrorから取得した。

| 項目 | K0 | X0 |
|---|---|---|
| partとpackage | CH32V003F4P6、TSSOP20 | CH32X035C8T6、LQFP48 |
| ISAと実験ABI | RV32EC、`rv32ec_zicsr`／`ilp32e` | RV32IMAC、`rv32imac_zicsr`／`ilp32` |
| CodeFlash／SRAM | 16,384／2,048 byte | 63,488／20,480 byte |
| compile用配置 | code origin 0、SRAM 0x20000000 | 同左 |
| 選んだclock条件 | HSI＋PLLで48 MHz | HSI 48 MHz |
| I2C1 route | SDA PC1、SCL PC2、default | SDA PA11、SCL PA10、I2C1_RM=000 |
| 入力候補 | PD2、PD3、PD4 | PA2、PA3、PA4 |
| debugを残すpad | PD1 | PC18、PC19 |
| PDのpad | 使用しない | CC1 PC14、CC2 PC15 |
| I2C IRQ番号 | event 30、error 31 | event 30、error 31 |
| PD IRQ番号 | 使用しない | USBPD 49、USBPDWakeUp 50 |

clockは後続の実機profileで要求する条件で、今回初期化・実測した値ではない。
入力padは論理配線案。board・fixtureの物理接続はまだ宣言していない。
CodeFlashの物理開始0x08000000と実験のlink origin 0を区別する。
保存予約とCodeFlash内boot予約はいずれも0のbare profile。
既存boardのbootloaderやupload設定は適用しておらず、実機に書く前に別profileで条件を照合する。

| 一次資料と固定URL | 使った箇所 |
|---|---|
| [V003 datasheet v1.8](https://raw.githubusercontent.com/ch32-riscv-ug/CH32V003/12d9aed0eae0276a133db00eb16bd492e8189f2d/datasheet_en/CH32V003DS0.PDF) | PDF page 1〜2：ISA・容量・part。14〜15：PC1／PC2 route |
| [V003 reference manual v1.9](https://raw.githubusercontent.com/ch32-riscv-ug/CH32V003/12d9aed0eae0276a133db00eb16bd492e8189f2d/datasheet_en/CH32V003RM.PDF) | PDF page 4〜5：memory map。154〜155：ACK／POS・clock stretch。176、181〜182：Flash方式 |
| [X035 datasheet v2.3](https://raw.githubusercontent.com/ch32-riscv-ug/CH32X035/327874484eda7e50cb4cf40271718e68fafbc850/datasheet_en/CH32X035DS0.PDF) | PDF page 1〜2：ISA・容量・part。13、19、22〜23：packageとI2C／CCのpad |
| [X035 reference manual v1.9](https://raw.githubusercontent.com/ch32-riscv-ug/CH32X035/327874484eda7e50cb4cf40271718e68fafbc850/datasheet_en/CH32X035RM.PDF) | PDF page 5〜6：memory map。74：I2C1 remap。238〜248：Flash方式。252以降：PD controller |
| [V003 SDK header](https://raw.githubusercontent.com/ch32-riscv-ug/CH32V003/12d9aed0eae0276a133db00eb16bd492e8189f2d/EVT/EXAM/SRC/Peripheral/inc/ch32v00x.h) | IRQ enum。headerは参照資料として保存し、対照へ取り込んでいない |
| [X035 SDK header](https://raw.githubusercontent.com/ch32-riscv-ug/CH32X035/327874484eda7e50cb4cf40271718e68fafbc850/EVT/EXAM/SRC/Peripheral/inc/ch32x035.h) | IRQ enum。同上 |

PDF pageは表紙を含む1始まり。本文に印字されたpage番号とは異なる。
既存device-dataの固定表は資料を探す入口に使い、上の項目は資料本文でも照合した。

### Flash の消去方式を分ける

V003は通常消去1,024 byte、高速消去64 byte。X035は通常消去1,024 byte、高速消去256 byte。
同じ系列でも方式で単位が変わるため、EXP-04のEは選択した操作・modeと組にする。
これは永続化backendの採用決定ではない。読み出し、programの粒度・再program制限、実行停止、電断時の影響は別項目として残す。
今回Flashを書き換えて検証していない。

### K0 の wire 受理は未確認

V003のACK／POS／clock stretchの記述は確認したが、未反映設定で満杯のときにどのbyteをNACKにするかは、
実装の割り込み順序とpeerからの観測を含む契約として未確認。
hostの`begin_write`受理と実I2Cのaddress ACKが一致するという主張はしない。
条件が成立しない場合はwire契約の問題として記録し、hostの成功で代用しない。

## 固定入力とR対照

ソースは [tests/unit/core_redesign](../../tests/unit/core_redesign/README.ja.md)に隔離した。
共通実行器を持たない、明示操作とstepで進む用途専用のC++実装。
通常処理とfrontendをhostから直列に呼び、実際のIRQ排他はまだ実装しない。
Python参照モデルはC++の出力を使わず、整数演算・取引ledger・全frame画像から期待状態を作る。

| 部分 | 固定したもの | 今回の検査範囲 |
|---|---|---|
| Knob | 8 byte設定、8 byte snapshot、未反映1件、飽和入力、status | 通常入力、snapshot、満杯、短い／長い／不正frame、restart、飽和、sequence周回 |
| PPS用途状態 | 入力payload4 byte×4件、desired／送信値／confirmed、session、終端回収 | 交渉中再操作、切断と完了の両順序、失敗・再適用、満杯、stale、古い／二重完了 |
| 入力のsession | payloadとは別のsession metadata | 切断前のqueued Applyを再接続後に電力要求へ使わない |
| 表示 | 128×64、page順、独自3×5 glyph、32 byte chunk、100 tick以上のframe開始間隔 | frame中の変更集約、buffer寿命、失敗再試行、終端の一度だけ確定、全byte照合 |

glyph配置はpage0にD＋希望、page1にC＋成立、page2にV＋電圧、page3にA＋電流、page5にlink／stale状態。
数値は5桁ゼロ埋め、負値と未成立値はdash。残りは0。
[goldenデータ](../../tests/unit/core_redesign/frame-golden.json)は4条件の各1024 byteとhashを保持する。
controller初期化、address指定、I2C headerはまだ含まない。

traceは [traces.json](../../tests/unit/core_redesign/traces.json)の25ケース。
指定の順序で操作し、各操作後の状態を照合する。
seed 0〜9、各10,000生成ステップをKnobとPPSへ追加し、範囲・取引・世代・回収を比較する。
byte frameの展開を含むため、低水準コマンド数は生成ステップ数より多い。
この追加検査は全割り込み順序の証明ではない。

表示bufferの保持はchunkごとのhashでも確認するが、描画内容そのものはgoldenとの全byte一致を別に検査する。
仮想時刻はこの入力範囲の非減少値。時計の周回・sleep・実時間保証はEXP-03以降の比較。

## 上流との機能差

上流コードのコピーや移植は行っていない。参照したsourceは[用途調査](todo-carried.ja.md#既存プロジェクトからの用途比較実験候補)の固定commit。

| 項目 | 上流Uの観測 | 今回のK0／X0 |
|---|---|---|
| Knobのレジスタ | 位置・最小・最大・step等を共有し、通常処理がbusy／changedを確認 | 新しい8 byte設定とsnapshot。互換レジスタ仕様ではない |
| Knob入力 | 通常loopでエンコーダ位相を読む | frontendから論理差分を注入。物理decode／debounceは未結合 |
| Knobの制約 | loopや符号付きstep等の元の用途機能 | 比較では飽和とstep1〜127に限定。機能差として保持 |
| PPSの入力 | 増減と積算reset。元sourceは待機・表示更新と電圧要求を組み合わせる | 増減とApplyという新しい比較用操作。意味の違いを明示 |
| PPS表示 | 電力、経過時間、電力量・電荷等を含む | 希望・成立・計測・状態に限定し、積算は含まない |
| I2C | PD Adapterはsoftware I2Cを同梱 | hardware I2C routeを後続条件に選定。今回のhostには実波形なし |
| PD | 実controller／IRQとprotocol処理 | sessionと要求・終端のfake。PDの実装費用・適合性を測った扱いにはしない |

この差があるため、上流binaryと今回のlogic ELFを並べてコアの優劣を判断しない。
同等機能のR対照とA〜Cを比較する前に、bus・周期・backendを揃える。

## 設備と残る作業

今回、local fixtureを使う接続・書き込み・電源操作は行っていない。
既存fixtureファイルの存在から次の配線・観測能力を推測しない。

| 契約 | 必要な設備と未確定事項 |
|---|---|
| K-HW | 対象part、I2C master peer、入力刺激、logic capture、pull-upと100／400 kHzの条件、共通lock |
| X-HW | 対象part、PPS source、負荷・許容範囲、電圧計、表示・計測・入力、CCとVBUS配線 |
| protocol観測 | CC traceまたは独立に解釈できる観測。電圧変化だけでは全条件を確認した扱いにしない |
| 時間 | 独立したmarker capture、計測分解能、clockとIRQ禁止時間の校正 |
| 保存・休止 | 実電源断・電圧・消費電流の観測、V006の保持と復帰 |
| H417 | 両コアを制御できるfixture、共有memory・cache・resetの資料と観測 |

次の準備単位は、X0の表示・計測・入力を一つのfake busへ結合し、100 ms周期・公平なbusy再試行・X-BUSを固定すること。
続いてK0のslave frontendと最小起動、X0のI2C／PD backendを実際の所有・IRQ契約へ結び付ける。
K0／X0のhost対照が個別部品から完成用途へ進んだ後も、実機未検証は明示する。
V006・H417・Flashの追加物理調査は該当する比較の開始条件として残る。

後続の[X0上流再現と機能差](tasks/EXP-06/x0-upstream-result.ja.md)は、UC-01の元source・flagsによるbuildと、元OLED／INA関数のI2C呼出し記録を固定した。上流128×32 verticalと今回の128×64 rendererは同一ではなく、controller header・setupは未確定のまま。

後続の[K0上流再現と契約差](tasks/EXP-06/k0-upstream-result.ja.md)も、UC-03の元source・flagsから同一imageを再生成した。元のlive register10 Bは固定K0のsnapshot／完成設定保持と異なり、そのまま同等機能対照へ置き換えない。実wire・入力・IRQ共有は未検証。
