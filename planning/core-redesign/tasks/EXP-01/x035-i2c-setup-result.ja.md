# X035のI2C準備：PCLK確認・clock gate・default pin route

2026-10-21。[資源利用権の接続](i2c-resource-result.ja.md)に続き、準備済みというboolだけだったpin adapterを、registerの設定・再確認へ置き換えた。
実装commitは`f0e3f74`。[入力と一次資料hash](../../x035-i2c-setup-inputs.json)、[測定記録](x035-i2c-setup-measurements.json)、[全18 ELF](x035-i2c-setup-size-matrix.csv)を保存した。
製品コア・USB・Flash実装は変更せず、実機のregister操作・配線・通信も行っていない。公開APIと方式の採用はP5へ渡す。

## 一次資料で確認した系列固有の条件

[WCH RM英語V1.9](https://www.wch-ic.com/download/file?id=382)、
[同中国語V1.9](https://file.wch.cn/download/file?id=445)、
[DS英語V2.3](https://www.wch-ic.com/download/file?id=381)を取得してhashを固定した。
英語RMは従来の固定I2C資料と同じSHA256だった。
SDKは[公式openwch/ch32x035 commit c625394](https://github.com/openwch/ch32x035/tree/c6253942b29ff2a3f5e2829182a874afffa45881)に固定し、8 fileのSHA256とGit blob SHA1をroot treeへ照合した。

| 条件 | 根拠と今回の扱い |
|---|---|
| peripheral clock | [SDK RCC_GetClocksFreq](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/SRC/Peripheral/src/ch32x035_rcc.c)はPCLK1／2をHCLKとする。HSI公称48 MHzとHPREから求め、Domainの宣言と照合する |
| HPRE | RM 3.4.2、印刷p15–16：code0～7は÷1～8、code8～15は÷2／4／8／16／32／64／128／256。別系列の「0xxxなら等倍」を流用しない |
| gate | RM 3.3.3／3.4、印刷p14以降：registerアクセス前に周辺clockを有効にする。APB2のAFIO＋GPIOA、APB1のI2C1をenableし、readbackを確認する |
| GPIO | RM 8章、印刷p62–63とCNF表、SDK GPIO_Init：AF push-pullでI2C用途なら自動open-drain。英語・中国語の両方に同じ説明がある。X035に存在しない汎用GPIO open-drain enumを追加しない |
| route | RMのAFIO PCFR1、印刷p73：I2C remapの000がSCL/PA10、SDA/PA11。今回このdefault routeだけを扱う |
| package | DS印刷p11（PDF13）の図を画像でも確認。C8T6のLQFP48ではPA10＝42番、PA11＝43番。小さいpackageへ一律適用せず、board配線は未確認 |

[SDKの7-bit I2C例](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/I2C/I2C_7bit_Mode/User/main.c)もPA10／PA11をAF_PPとする。
GPIO helperのmode低4 bitとspeed値1から、各pinの設定nibbleは9。
CFGHRのPA10／11に対応するmask `0x0000ff00`だけを`0x00009900`へ変更する。
I2C engineのACK・timing・受信末尾の方式は従来の試作をそのまま使い、SDK例全体を移植したものではない。

## 準備・失敗・open時の再確認

SetupはDomainの周辺／SCL／SDAとclock利用権を一時claimし、次を有限回の操作で進める。

1. cold／quiescentという外部adapterの宣言を要求し、HSION／HSIRDYとHPREから公称PCLKを求める。宣言PCLKと違えばwrite前に拒否して利用権を返す。
2. clock gateをORで有効にし、readbackを確認する。GPIO／AFIO／I2Cのclockが無効なまま、そのregisterへアクセスしない。
3. PE・START／STOP・software reset、IRQ／DMA設定、MSL／BUSYの残りがないことを確認する。PEが立っていればSTAR2を読まない。
4. I2C remap fieldだけを000へ、CFGHRの対象2 pinだけをnibble9へ変更する。ほかのfieldは保持する。
5. clock・gate・idle・route・pin設定を再確認し、成功したら一時利用権を返す。

成功後に利用権が空くため、その間に別利用者が設定を変える可能性がある。
Port.openは改めて利用権をclaimし、Setup.readyで実registerを再確認してから既存Masterをopenする。
古いprepared flagだけでは受理しない。readyは「新しくopenできる状態」のqueryであり、PEが有効な転送中のhealth判定として使うものではない。

gate有効化後にidleや設定を確認できなければFaultHeldとなり、一時利用権を保持する。
STOP要求・reset・clock無効化を勝手に実行せず、再prepareや別Portのopen、clock変更も止める。
abandon(true)は外部adapterが静止を確認したという宣言であり、この関数がhardwareを停止・検証するわけではない。
検査では不明な状態を明示的に解消してから宣言を与える。falseの宣言では返却しない。

## 成立させていない条件

公称PCLKの確認は、HSIの周波数・精度・trim・温度変化を測定した証拠ではない。
RMにはHPREの分周時にprefetch bufferを有効にする条件もある。このadapterはCFGR0を変更せず、Flash wait／prefetch設定を提供しない。実clock設定側で確認する必要がある。
従来DomainとI2C engineは整数MHz等の制限を持つため、HPREで生成できる公称値をすべて受理する構成でもない。

GPIO／AFIOのRMWは直列呼出しが前提。ISRや別coreからの同時変更を保護していない。
PA10／11以外のroute、pinと別機能の競合台帳、実pull-up・立上り・自動open-drainの電気的動作は未検証。
特に小packageのpin選択は別のrouteが必要であり、今回のC8 default routeを全X035共通の必須設定とはしない。

準備後のGPIOA／AFIO／I2C clockは有効のまま残す。Port.endでPEと利用権を返しても、sharedなGPIO clockを元の値へ戻す処理は行わない。
他pinの利用者を巻き込まずにclockを無効化するrefcount・停止手順と、静止電流の評価は残る。

## Host検査

ASan／UBSan、警告errorでnative100件＋workspace規則3件、計103件が通過した。conftest.pyは追加していない。
100 trace、611操作を保存した。register模型は、gateが無効なAFIO／GPIO／I2Cへのアクセスをassertで拒否する。
既存resource driverとI2C Fakeを固定入力として再利用し、派生source全文とhashも保存した。

- 16 HPRE×4 HSI状態を、一次資料に基づくdivisor表と照合。
- 10公称clock条件で他fieldの保持、準備、Port.open／endを確認。
- cold宣言なし・未確定Domain・PCLK不一致で、構成writeをせず拒否。
- PE／command／reset・5種類のIRQ／DMA関連bit・MSL／BUSYの計12条件で、route／pinを書かずFaultHeld。
- APB2／APB1 gate・remap・pin設定のwrite無視を注入し、readbackで失敗を検出。clockが無効な先にはアクセスしない。
- 準備後にclock・gate・route・pinを壊し、prepared flagが残っていてもopenを拒否。
- write、read1／2／3／8 byte中や終端未回収の再prepareを、MMIOをせず拒否。回収後は再準備・openできる。

保存traceで観測したprepare1回の最大はread20回・write4回だった。
検査した入力での値であり、bus waitを含む実時間や完了時間の保証ではない。delay・callback・taskの呼出しは追加していない。

## 構造費用

X035 `rv32imac_zicsr/ilp32`、GCC14.3.0、`-Os`、LTO off／onで18 ELFをlinkした。
V003はこの系列固有adapterの測定対象にせず、除外を記録した。実startup・IRQ backendを持たない構造probe。
engine・Domain・宣言だけのmanaged等の以前の14基準imageは、byte hashとstatic RAMが一致した。

| X035・LTO on | image B | static RAM B | pin adapter object B |
|---|---:|---:|---:|
| 宣言boolだけのmanaged | 2,736 | 248 | 1 |
| register設定・再確認あり | 3,280 | 260 | 16 |

Domain40 B、Port48 B（Master内包）、保護付きbuffer68 Bは不変。
adapterは＋15 B、imageは入口の追加操作も含めて＋544 B。全static RAM差はalignment等により＋12 Bだった。
異なる保護と操作を提供する比較であり、544 Bを一般的なpin初期化だけの費用とは呼ばない。
未使用8条件は追加RAMとobject symbolが0 Bで、image hashは各emptyと一致した。
全18 ELFにallocator symbol・constructor arrayはなく、単一frame値はCSVに保存した。IRQ・callee込みのstack peakや実時間は未測定。

## 再現と次の接続

```sh
EXP01_X035_SETUP_EVIDENCE=work/x035-i2c-setup-host-new \
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_x035_i2c_setup.py tests/unit/test_workspace.py -q

python3 tests/platform/core_redesign/measure_x035_i2c_setup.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/x035-i2c-setup-abi-new
```

保存archiveは`work/exp01-x035-i2c-setup-20261021-evidence.tar.gz`。
固定した3 PDF・SDK8 file・Git commit／tree・pin図、sourceと派生driver・compiler／binary hash、全trace、JUnit、ELF／map／disasm／stack情報とanalysisを含む。
hash・byte数は測定JSONを参照する。同名出力先は再利用しない。

残るP4作業はrouteとpackage能力差、clock喪失・停止／復帰、GPIO／AFIOのIRQ共有、完了枠・arbiter・用途への接続。
[routeとC8／F7能力差](x035-i2c-route-result.ja.md)は追加のhost／ABI検査を行った。物理pin競合や実配線の成立は未検証。
実起動・wire・周波数・電流は未確認。教材では「flagとregister再確認」「開く準備と転送中の状態」「idle不明時に資源を返してよいか」を予想する。
人による教材実施・理解度レビューは未確認。
