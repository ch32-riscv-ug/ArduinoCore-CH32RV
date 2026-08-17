# Toolchain方針

文書状態: 候補選定前

## 目標

- 保守されているRISC-V GCC/binutils/newlibを利用する
- Linux、Windows、macOS向けartifactを固定する
- Arduino Board Managerから再現可能に取得できる
- version、配布元、SHA-256、build metadataを記録する
- 標準RISC-V ISAで動作する構成をdefaultにする
- WCH固有最適化は、必要性と効果を実測したoptional profileに限定する

## 現状

旧コアとWCH公式Arduino coreは、主に`riscv-none-embed-gcc 8.2.0`を使用しています。旧コアはGNU++14と`-fpermissive`にも依存しています。

GCC 8 laneは旧コードとの比較用に残せますが、新コアのdefault候補にはしません。

## 候補

- [riscv-collab/riscv-gnu-toolchain](https://github.com/riscv-collab/riscv-gnu-toolchain)を基にした固定配布物
- 継続的に更新され、出所とbuild optionを確認できる既存cross-toolchain distribution
- WCH toolchainは固有命令や高速割込みの比較lane

特定distributionとversionは未決定です。

## 必須の認定項目

### CPU/ABI

- RV32E + ILP32E
- RV32I/M/A/C + ILP32の対象組合せ
- FPU搭載機でのF/ILP32F採用可否
- CSR命令と必要なISA extension
- multilibの存在と正しいlibgcc/newlib選択

### runtime

- resetから`main`までの初期化
- data/BSS
- global/static constructor
- local static initialization
- weak symbol override
- interrupt attributeとcallee-saved register
- trap/HardFault相当のdefault処理
- LTO有無
- `--gc-sections`

### C/C++

- ArduinoCore-APIのhost/target build
- CとC++ headerの境界
- `printf`、`snprintf`、整数・浮動小数format
- exception/RTTIの既定方針
- `-fno-threadsafe-statics`等の互換性とサイズ効果
- warningをerrorにするown codeと、vendor用flagの分離

### サイズと性能

- empty sketch
- Blink
- Serial print
- Wire/SPI
- constructorを持つsketch
- ISR latencyと保存register
- Flash/RAM budgetの回帰閾値

### host OS

- x86_64 Linux
- Windows
- macOS Arm64
- macOS x86_64を継続するかは利用者需要とtool配布可能性で決める

## C++標準

ArduinoCore-APIの最低条件であるC++11は保証します。新コアのdefaultをGNU++17にする案がありますが、次を確認してからADRで決定します。

- 既存Arduino library互換性
- compiler/package size
- CH32V003級でのFlash/RAM
- vendor headerを隔離した状態でのwarning/error

`-fpermissive`はown codeでは使用しません。

## 配布

Board Manager packageへtoolとして登録する場合、[Arduino package index仕様](https://docs.arduino.cc/arduino-cli/package_index_json-specification/)に従い、host別archive、size、SHA-256を固定します。

release前には新規Arduino data directoryを使い、既存installに依存せずcoreをinstall・compileできることを各OSで確認します。

## Toolchain再配布時の遵守事項

Board ManagerからGCC、binutils、newlib等を配布する場合、binaryのSHA-256だけでは不十分です。

- 各componentのversion、license、noticeをinventory化する
- 対応sourceと適用patchを取得できる状態にする
- 再現可能なbuild script/configurationを保持する
- GCC Runtime Library Exceptionを含むruntime library条件を確認する
- toolchain、uploader、付属utilityもrelease SBOMの対象にする
- upstream artifactを再梱包した場合、その内容と理由を記録する
