# EXP-01 reset・CRTを含むELFとGPIO入口

2026-10-20。入力作成時のHEAD `9fa1d65`、検査済み試作 `004ff53`。
logic-only linkから進め、独立したreset／vector／data・bss初期化を含むELFをV003F4P6とX035C8T6向けに作った。
製品コア・並行作業中のUSB実装は変更していない。実機への書込み・起動確認は行っていない。

## 対象と一次資料

[固定入力](../../runtime-inputs.json)にpart、ISA／ABI、容量、GPIO probe、参照URLとhashを記録した。
WCH配布SDKの固定mirrorにあるstartup、linker、register headerを参照した。
reset／linkerのソースは試作独自で、SDK startupを製品へ取り込んでいない。

- V003のvector構造は[固定startup](https://raw.githubusercontent.com/ch32-riscv-ug/CH32V003/12d9aed0eae0276a133db00eb16bd492e8189f2d/EVT/EXAM/SRC/Startup/startup_ch32v00x.S)、配置は[固定linker](https://raw.githubusercontent.com/ch32-riscv-ug/CH32V003/12d9aed0eae0276a133db00eb16bd492e8189f2d/EVT/EXAM/SRC/Ld/Link.ld)を参照。
- X035も[固定startup](https://raw.githubusercontent.com/ch32-riscv-ug/CH32X035/327874484eda7e50cb4cf40271718e68fafbc850/EVT/EXAM/SRC/Startup/startup_ch32x035.S)と[固定linker](https://raw.githubusercontent.com/ch32-riscv-ug/CH32X035/327874484eda7e50cb4cf40271718e68fafbc850/EVT/EXAM/SRC/Ld/Link.ld)を参照。
- GPIOのregister配置は[固定V003 header](https://raw.githubusercontent.com/ch32-riscv-ug/CH32V003/12d9aed0eae0276a133db00eb16bd492e8189f2d/EVT/EXAM/SRC/Peripheral/inc/ch32v00x.h)と[固定X035 header](https://raw.githubusercontent.com/ch32-riscv-ug/CH32X035/327874484eda7e50cb4cf40271718e68fafbc850/EVT/EXAM/SRC/Peripheral/inc/ch32x035.h)を照合。

maskable IRQはMIEを落としたままmainへ進む。clock選択・PLL・SysTick・tick・scheduler・heapを追加していない。
これはP2の48MHz要求を実装したprofileではなく、clock未設定の独立runtime比較。
boot予約は0、Flash alias先頭0でlinkする。既存bootloaderやupload経路への適用を確認した意味ではない。

| 項目 | V003F4P6 | X035C8T6 |
|---|---|---|
| ISA／ABI | rv32ec_zicsr／ilp32e | rv32imac_zicsr／ilp32 |
| Flash／RAM | 16384／2048 byte | 63488／20480 byte |
| reset／vector base | 0／0 | 0／4 |
| vector枠数 | 39 | 55 |
| GPIO probe | PD0、clock mask 32 | PA0、clock mask 4 |

GPIOはportの下位8 pinのMODE=01／CNF=00だけを扱う比較用入口。
clock gate、初期low、当該config nibble、BSHRのset/resetを操作し、他のconfig bitは保持する。
boardのpin列挙・route・debug競合・配線まで扱う汎用GPIO classではなく、実波形は未検証。

## resetと初期化の条件

SPはRAM末尾、GPは独立linkerで定義する。dataをFlash LMAからRAMへwordコピーし、bssをword消去する。
sectionの開始・終了を4 byteへ整列し、3 byte初期値・5 byteゼロ領域の末尾paddingも含めて検査した。
MIEを落とし、vectorのaddressとmodeを設定する。未処理handlerは停止するだけで、割り込み復帰を試していない。

constructor処理は無効／有効を明示的に比較。
無効時にpreinit／init arrayが残ればlinkを拒否する。初期化が必要なobjectを黙って無視する構成ではない。
有効時はpreinit、優先順init、通常のC++ global constructorの順に呼ぶ。
破棄・atexit・heap・例外・thread-safe staticの提供はこの試作に含めない。

stackは256 byteを予約し、static RAMと別に記録する。これは既知のpeakや安全な必要量を測定した値ではない。
static領域が予約stackと衝突するlinkを拒否し、heapを使える領域として残りを公開していない。

## 命令検査の証拠と境界

```sh
python3 tests/platform/core_redesign/build_minimal_runtime.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp01-runtime-link
```

参照を自動取得せず、hash一致する保存済み6ファイルを使う場合は `--sources 保存先` を指定する。
実行時は実機を操作せず、compiler／binutilsと入力・生成物だけを使う。

10条件×2 part×LTO有無、40構成がlinkと命令検査を通過。
各part／LTOでconstructor無効、RAM超過、Flash超過の3拒否を行い、計12件を期待どおり拒否した。
これらは成功した起動ではなく、linkの負の条件の証拠。

実際のELFから取り出したbinaryと、固定objdumpのno-aliases・numeric decodeを照合して命令を進める。
初期RAMは0xa5で汚し、data/bss全域と周囲の未変更領域、GP/SP、MIE、vector設定をmain到達直前に確認する。
V003では命令のregister参照もx0〜x15へ制限する。

これはMCU emulatorではない。constructorへのcallはABI hookとして記録し、本体を実行せず戻す。
hookはcaller-saved registerを壊し、callee-savedを保持してreset側の反復を検査する。
constructor payloadの副作用、mainのsignature、GPIOへの実MMIO、trap／IRQ・hardware stack・cacheは実行していない。
命令step数をMCU cycle・起動時間・real-time保証へ換算しない。

```sh
python3 tests/platform/core_redesign/check_runtime_faults.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --evidence work/exp01-runtime-link --out work/exp01-runtime-faults
```

元の生成物を変更せず、data書込みを0にする、bssに非zeroを書く、preinitを飛ばす3種のresetを別にcompileする。
2 part×LTO有無の12条件すべてで壊れた初期化を検出した。
元のbinaryやデコードを手書きで改変せず、変えたassemblyを再compileして検査した。

## GPIOのhost検査

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_runtime_gpio.py tests/unit/test_workspace.py \
  -o junit_family=xunit1
```

ASan／UBSan付き16件と配置規則3件、計19件が成功。新しいfault checker追加後の配置規則3件も再確認した。
既存plugin由来のexperimental warning19件。tests以下にconftest.pyを追加していない。

raw／固定class、raw／実行時pin classの4入口、2 targetについてread/writeの全transactionを独立Python模型へ照合。
clock bit保持、当該nibble以外の保持、lowを先に設定する順序、high／lowを検査した。
8 seedの各1000条件にpin 7／8／31／32／UINT_MAX等を含め、範囲外は一切MMIOせず拒否する。
Fake backendのtransaction確認であり、同時RMW、実周辺への到達、pin波形の確認ではない。

## 容量とclass比較

全行は [runtime-size-matrix.csv](runtime-size-matrix.csv)、入力・tool・比較対は [runtime-measurements.json](runtime-measurements.json)。
次はLTOなし、単位byte。static RAMとは別に、全構成でstackを256 byte予約している。

| 入口 | V003 Flash | X035 Flash | static RAM |
|---|---:|---:|---:|
| 空のmain | 264 | 328 | 0 |
| 空のmain・array呼出しあり | 312 | 376 | 0 |
| data／bss probe | 368 | 432 | 16 |
| constructor probe | 508 | 568 | 20 |
| 別TUのsetup／loop | 276 | 340 | 0 |
| 定数pin raw | 328 | 392 | 0 |
| 定数pin class | 328 | 392 | 0 |
| 実行時pin class | 372 | 436 | 4 |
| 実行時pin raw | 372 | 436 | 4 |
| 未使用pin object | 264 | 328 | 0 |

LTO時のsetup／loopは空のmainと同じ264／328 byte。定数pinのGPIO入口は312／376 byteになった。
array処理を常設した空構成は両partでFlashが48 byte増えた。明示的に無効にすれば省ける一方、必要な初期化を拒否する条件も必要になる。


static RAMと予約stackを混ぜず、vector・初期化・Flash内data imageを含むFlash量を記録した。
setup／loopは別translation unitで定義し、LTOの有無を比較した。
定数pinのraw／classはこの入口でbinary一致。実行時pinは容量が同じでもLTO時にbinaryが異なる。
classに名前を付けただけで常に命令・時間まで同じとする主張には使わない。

## 再現と次の範囲

host `work/exp01-runtime-host-20261020/`、最終ELF `work/exp01-runtime-link-20261020-r2/`、負の制御 `work/exp01-runtime-faults-20261020/` に入力と生記録を保存。
コマンドと終了値、stdout／stderr・JUnit・ELF・map・symbol・逆アセンブル・section・.su・hashを照合した。
移送archive `work/exp01-runtime-20261020-evidence.tar.gz`、701,178 byte、
SHA-256 `4d4d3c3c6cd043cc7bcc1059f0c5cc7d3be2d0be1c7ac5e07a163d701cd4f115`。初回失敗はarchive内のdevelopment-incomplete-runに含めた。
HEADだけでなく当時の試作を含むhash付き入力を使い、再実行は未作成の出力先を指定する。

初回はunused pinを2つのtranslation unitに定義してlinkが失敗した。ログを保持し、定義を1つに直した。
raw-dynamic条件のpreprocessor選択も照合し、最終runではrequested_pin symbolが必要な条件に存在することを検査した。
初回の部分結果は有効な最終値に混ぜていない。

次はclock設定・時刻源・I2Cの実backend、ISR入口と共有、資源引渡し・取消／停止を接続する。
IRQ無効のELFから任意のC++ ISRの成立を推定しない。
完成core、R/A/B/Cの実I/O比較、実stack peakと実時間、利用者の理解を確認したことにはしない。

## clock/timeの後続

[clock/time入口](clock-time-result.ja.md)では有限のclock切替、32／64 bit SysTick読出し、epochと区間変換を追加比較した。
このruntimeの40 binaryも回帰確認した。実周波数・精度・起動、継続する論理時刻を確認した意味ではない。
