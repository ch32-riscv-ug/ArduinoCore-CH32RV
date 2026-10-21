# 表示交換と独立rendererの接続

2026-10-21。[X0固定feature比較](../EXP-06/x0-features-result.ja.md)から、PPS・計測・タッチの意味を保って表示側を交換する課題。
新しい実験用headerで比較し、旧X0・製品コア・USBを変更していない。公開APIや実行方式の採用はP5へ渡す。
[入力](../../display-inputs.json)はcontroller製品ではなく、情報量・byte形式・転送分割の異なるmock profileを定義する。

## 交換する範囲

`display_parts.hpp`は`stdint.h`だけを使い、View・glyph・byte生成・frame状態・1 chunkのstorageを持つ独立部品。
bus・queue・実行器・Arduino・実時計のAPIへ依存しない。
`x0_display.hpp`がPPS側の値をViewへ集め、同じbusで計測・タッチと交互に転送する接続を持つ。
R/A/B/C-static-batchは従来の各入力・実行器へ同じMeaningを接続し、Panelの型だけを交換する。

| profile | frame byte数 | chunk上限 B | 最後のchunk B | 描画の変更 |
|---|---:|---:|---:|---|
| mono64-32 | 1,024 | 32 | 32 | 128×64 page mono。従来と同じpixel |
| compact32-31 | 512 | 31 | 16 | 128×32。状態表示を第1行の横へ移す |
| mono64-16 | 1,024 | 16 | 16 | mono64-32と同じpixel、分割だけ変更 |
| rgb96-64 | 12,288 | 64 | 64 | 96×64、green RGB565のmock little-endian stream |

すべてdesired・confirmed・測定mv／ma・confirmed有効・測定stale・接続状態を同じ意味で描く。
小さい表示でも状態情報を削らず、配置を変える。RGBはmonoのglyphをpixelへ展開した比較用形式。
controllerのcommand、address offset、wire byte順、I2C／SPI特性、電気条件を実装したものではない。
型選択はbuild時であり、転送中のpanel差替えやhot swapは行わない。

## 所有と明示的な処理

Viewの最新値と、進行中frameの凍結値を別に持つ。転送中の更新は最新値だけに反映し、次のframeへ集約する。
全frame bufferは持たず、各chunkを凍結Viewから再計算する。
prepareは最大chunk byteだけ生成し、active中のprepare／二重開始を拒否する。
transportが受理したchunkを借用し、hardware終端だけでは解放しない。用途側のstepでtakeした後にfinishを渡し、次へ進める。
appとbufferのaddressは借用中に変えないことが前提。ISR共有・raw pointerへの書込みを安全にする仕組みではない。

失敗したchunkの後はframeを破棄し、間隔を置いて最新Viewから全体をやり直す。
displayed_revision／hashは「最後に全chunkを成功回収したframe」の論理観測。
実panelの一部へ既に書いたpixelを巻き戻したり、物理的なframe切替をatomicにした証拠ではない。
同じmono画像はchunk16／32で最終hashが一致する。異なる配置・byte形式同士ではhash一致を要求しない。

## 共通のwire模型

今回の全profileは`1 + ceil(payload_bytes/16)`抽象tickで1 transactionが終端する。
Owner1が表示、2が8 byte計測、3が8 byteタッチ。毎回明示回収してbusを解放し、readyなownerをround-robinで選ぶ。
待ち行列へ全部積む方式ではなく、各用途がpending状態と借用bufferを保持する。
計測は100 tick、タッチは20 tick、表示はdirty時に最小100 tickの間隔でframe開始を試みる。
100 tick以内のframe完了を保証する設定ではない。

旧X0は長さによらず2 tickのtransactionだった。今回の共通bus費用も変わっているため、旧image／処理時刻との差を表示交換だけの効果としない。
tickはphysical msやI2C速度ではなく、実render CPU時間・controller command・IRQ・DMA・実wire時間を含めない。
時刻はこの非周回・単調なuint32入力範囲。clock変更・sleep・wrapを跨ぐ接続は今回の比較に含めない。

## Host検査

新規72件とworkspace規則3件、合計75件が通過。ASan／UBSan・警告errorを使用し、conftest.pyは追加しない。
独立Python模型は全frame画像とtransaction台帳を持ち、C++のchunk再計算とは違う表現で各観測を照合する。

- 4 profile×R/A/B/Cの16構成で同じ3,000 tickの用途を照合。
- 表示・計測・タッチの失敗、通常処理停止、入力とsession、切断・再接続・古いPD終端、借用中のView更新を照合。
- 5入力のburstで容量4を満たし、受理4・拒否1、最後に受理したApplyの5040確定と表示追従を全16構成で照合。
- M/S/U、system／未参加user／参加user、禁止文脈を同じ意味で照合。
- 4 seed×300生成操作と各3,500 tickの後続進行を、全16構成で全観測に照合。
- 借用中prepareでbufferが変わらないことと、直接破壊した場合にbusのhash観測が検出することを確認。
- 独立部品とC標準headerだけのdriverを別directoryへコピーし、`-nostdinc++`でbuild。4 profileで凍結・二重終端拒否・失敗後の再frame・全画像hashを確認。

通常用途を保存したnative binaryで再生し、各profileで4候補の全snapshotがbyte単位で一致した。
PPS確定5020、計測30件、タッチ150件、frame2個の成功回収、最後のViewへの追従を全構成で確認した。
通常条件では入力・計測・タッチの取りこぼしcounterと借用buffer破壊は0だった。

| profile | 表示transaction数（2 frame） | frame完了tick | 計測最大遅れtick | タッチ最大遅れtick |
|---|---:|---|---:|---:|
| mono64-32 | 64 | 112、220 | 5 | 7 |
| compact32-31 | 34 | 58、158 | 5 | 7 |
| mono64-16 | 128 | 148、292 | 4 | 6 |
| rgb96-64 | 384 | 1,092、2,182 | 7 | 9 |

遅れは正常時の各周期予定から通常処理で回収するまで。明示stepを毎tick与えたこの有限入力だけの数値で、最悪時間の上限ではない。
PD確定は全profileでtick5だった。実PD応答・deadlineを満たすことの証拠ではない。
分割を小さくすれば今回の占有は短くなるが、transaction回数が増えframeの完了は遅くなった。
最終revisionはprofileにより3／4となった。完了順によって複数の値変更を一度にViewへ反映する場合があり、内部revision数の一致と最終の描画内容の一致は別である。

## ABIと逆流しない構成

GCC 14.3.0、`-Os`、V003 `rv32ec_zicsr/ilp32e`とX035 `rv32imac_zicsr/ilp32`、LTO off／on。
empty、app16構成、library単独4構成、それぞれの未使用20構成の計164 ELFをlinkした。
既存profileの純粋な構造の測定で、実startup・PD・I2C／SPI・display driverを実行できるfirmwareではない。

| profile | library Screen B | app R／A B | app B B | app C-static-batch B |
|---|---:|---:|---:|---:|
| mono64-32 | 116 | 356 | 384 | 368 |
| compact32-31 | 116 | 356 | 384 | 368 |
| mono64-16 | 100 | 340 | 368 | 352 |
| rgb96-64 | 148 | 388 | 416 | 400 |

object値は両profile・両LTOで同じ。ScreenはView2個・状態・chunk bufferを含み、frame全体のstorageを持たない。
chunk31は32よりpayloadが1 B少なくても、alignment込みのScreenは同じ116 Bだった。
streamingでRAMを抑えた分、再計算するrenderのCPU費用は別測定が必要になる。

X035・LTO onのimage B。入口と必要なsupportを含む。

| profile | R | A | B | C-static-batch | library単独 |
|---|---:|---:|---:|---:|---:|
| mono64-32 | 2,784 | 2,800 | 3,232 | 3,232 | 1,168 |
| compact32-31 | 2,832 | 2,856 | 3,288 | 3,248 | 1,256 |
| mono64-16 | 2,760 | 2,776 | 3,200 | 3,200 | 1,144 |
| rgb96-64 | 2,872 | 2,888 | 3,320 | 3,312 | 1,248 |

harness objectは68 B。静的RAMにはこれとpaddingを含むため、object表と混同しない。
library単独とappでは能力・入口が違い、image差をそのままframework単体の費用としない。
Cは従来どおり固定1宛先・有限batchで、動的購読配信まで同等の能力ではない。

未使用80行はemptyとimage hashが一致し、追加Flash／RAM 0 Bだった。
library単独16行はWorkExecutor／Mailbox／PPS／Queueのsymbolを持たず、constructor配列とallocator symbolは全164行でなかった。
appで出力された最大単一frameはV003 48–120 B、X035 80–144 B。library単独はV003 28–108 B、X035 48–96 B。
呼出し連鎖・IRQ込みのstack peakや実機速度、電流の値ではない。
[全164行](display-size-matrix.csv)と[再現記録](display-measurements.json)を参照する。

## 変更課題として残ること

独立rendererにはCoreの実行器を持ち込まず、必要なbus所有・進行機会をapp側で接続できた。
この配置をcore／独立library／用途例のどこへ公開するか、View変換やdirty単位をどこまで共通化するかはP5へ渡す。
今回は限定glyphと4 mock形式の比較であり、実controllerの初期化・座標設定・protocol変換・borrowed buffer・停止／回収へ接続する作業は残る。
実wire・render時間・IRQ共有・cache／DMA・省電力・稼働中切替、利用者が実際に交換するレビューは未実施。
[表示交換の課題](display-exercise.ja.md)に、変更する層と予測する資源を記録する。

後続の[X0上流再現と実device関数の観測](../EXP-06/x0-upstream-result.ja.md)では、上流のSSD1306 128×32 vertical初期化・I2C control・長い同期取引を記録した。ここで比較したmock profileや固定128×64 rendererの実controller protocolを成立済みにしたものではない。
