# 旧コア監査

## 調査対象

- Repository: [`ch32-riscv-ug/arduino_core_ch32_riscv_noneos`](https://github.com/ch32-riscv-ug/arduino_core_ch32_riscv_noneos)
- Commit: `b4e91720ae4c0ebf443db170c06e122c6acd15a7`
- Describe: `1.4-2-gb4e9172`
- 調査日: 2026-08-17

## 旧リポジトリの実体

旧リポジトリはArduinoコア本体を直接管理していません。WCH EVTを取得し、familyごとのコアへ加工してBoard Manager用ZIPを作成するgenerator/overlayです。

名称はArduinoコアですが、旧版は一般的な`pinMode()`や`digitalWrite()`等を実装することより、Arduino IDEからEVT APIとEVT sampleを利用することを主目的としていました。新コアでいう「旧版互換」は、Arduino API互換よりEVT利用方法の互換が中心です。

調査時点で追跡対象は63ファイルで、そのうち33ファイルがEVT依存patchでした。EVT本体と生成後のArduino coreはGitで管理されていません。

主な生成フローは次の通りです。

1. WCHの配布URLから7系列のEVT archiveを取得
2. `EVT/EXAM/SRC`をfamily別coreへ全量コピー
3. GPIO Toggle exampleの`User`をcore skeletonとしてコピー
4. conf headerへ`debug.h`と全Peripheral headerを追加
5. overlayとpatchを適用
6. EVTの`User`ディレクトリをArduino examplesへ収集
7. ZIPを作成しpackage JSONのchecksumとsizeを更新

## 根本的な問題

### EVT全体とC++の結合

`Arduino.h`が`debug.h`をincludeし、加工されたconf headerが全Peripheral headerをincludeします。このため、利用していないvendor headerのC++不整合まで、すべてのArduino sketchのビルド障害になります。

この境界の広さが直接生んだのは、主にC++ linkageとheader構造のpatchです。startup constructorはCRT所有権、weak handlerはoverride設計、未使用引数や演算子修正はvendor code品質という別の問題です。

旧startup patchは`__libc_init_array()`を`SystemInit()`より前に呼ぶ構成でした。global constructorがclockやhardwareへ触れる場合に危険なため、新runtimeでは初期化順序をownし、実機で検証する必要があります。

### 再現性がない

- EVT URLにversionとSHA-256が固定されていない
- generatorが`php`、`wget`、`unzip`、`rename`、`patch`、`zip`等のhost環境へ依存する
- shell commandの終了statusやpatch rejectを厳密に検証しない
- temp stagingとatomic promoteがない
- vendor sourceの由来とライセンスinventoryを生成しない
- versionがscript、platform、library、package metadataへ分散している
- package JSON更新は先頭platformのchecksum/sizeが中心で、version、URL、archive名は別途手作業になる

### board/device modelが粗い

旧`boards.txt`にはCH32L103、V003、V006、V103、V20x、V307、X035の7系列がありますが、family名中心です。

- exact SKUとpackageを表現しない
- variant実体がない
- memory menuを変えても主に`upload.maximum_*`だけが変わる
- linker/startup選択が実際のmemory/device選択と連動しない
- V20x、V307のstartup wrapperが特定device variantへ固定されている

確認されたドリフト例です。

- package metadataからCH32V006が欠落
- packageではCH32V307をCH32V30xと表記
- CH32V003 GPIO exampleはGPIODを初期化した後にGPIOAをtoggle
- macOS x86_64用OpenOCD metadataがArm64 assetを参照する箇所がある
- `platform.txt`、release、libraryのversionが一致しない

### 古いtoolchainへ固定

- GCC 8系の`riscv-none-embed`を利用
- GNU++14と`-fpermissive`へ依存
- 新しいGCC定義がpackageに存在してもbuild recipeでは利用しない
- core archiveを`--whole-archive`でlinkする

### 書き込み対象を選べない

WCH-LinkE/OpenOCDのupload recipeに、probe serial、USB topology、fixture laneなどを指定する経路がありません。複数WCH-Link接続時に意図したDUTへ書き込む保証がありません。

## 継承する知見

コードをそのまま移植するのではなく、以下を設計知識または回帰試験として継承します。

- ArduinoからEVT形式の低レベルコードを利用する目的
- weak `main()`によりArduino `setup()/loop()`とEVT `main.c`を共存させる考え方
- Cコード向けの`c_main()`入口
- familyごとのISA/ABI、memory、runtime差分。ただし値は再検証する
- C++ constructorが必要であること
- weak ISR、C/C++ linkage、header破損、vendor実装バグに関する33 patchの知見
- GPIO、UART、global constructorを使ったfamily別smoke testの意図
- Arduino Board Managerからインストールしてbinを生成・書き込みする導線

## 新コアへ持ち込まないもの

- `EVT/EXAM/SRC`一式をcoreへ複製する構造
- 全vendor headerの暗黙公開
- EVT assemblyへの重複patch
- `-fpermissive`
- family名だけのboard定義
- 手書きの巨大な`boards.txt`
- patch失敗を許容するgenerator
- 無検証のEVT example全量収集
- probeの選択条件を渡せないupload recipe

## 移行上の注意

- 旧patchを新ソースへ機械的に適用しない。各patchが示すfailure modeをtestへ変換する
- 旧版とのバイナリ互換は目標にしない。ソース互換範囲と移行表を用意する
- legacy Board Manager indexや既存release assetを破壊的に置換しない
- 新コアはbeta indexまたは手動installationで十分に検証してからstable indexへ追加する
