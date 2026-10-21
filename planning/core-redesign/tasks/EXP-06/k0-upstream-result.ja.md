# K0参照元のビルド再現とwire契約の差分

2026-10-21。再現toolの実装commit `a741836`。
対象は[UC-03の固定commit](https://github.com/wagiminator/CH32V003-I2C-Knob/tree/5302daa8990ee850de18c963ade8968e4e976350)。
[入力・全15 fileのURLとhash](../../k0-upstream-inputs.json)、[tool・ELF・集計](k0-upstream-measurements.json)を保存した。
上流Uをそのまま再現する単位で、[固定K0](../../workloads.ja.md)との同等機能比較や実wire検証を完了したものではない。
source・header・startup・makefileを変更せず、製品コア・USB・Networkも編集していない。

## ビルド再現

上流の[makefile](https://github.com/wagiminator/CH32V003-I2C-Knob/blob/5302daa8990ee850de18c963ade8968e4e976350/software/i2c_knob/makefile)は、main・I2C slave・systemの3個のC source、48 MHz、rv32ec／ilp32e、Os・LTOありを指定する。
今回も同じsource・flags・linker scriptでGCCを直接呼び、観測用mapだけを追加した。
compilerはxPack GCC 14.3.0で、発行者の同梱binary作成環境まで一致させた扱いにはしない。

ローカルの検証済みcopyから2回、固定URLから再取得したcopyから2回、計4 ELFを生成した。
全4個のload imageはbyte単位で一致した。
15 fileのSHA-256・サイズに加え、Git blob SHA-1も固定treeの値と一致した。実依存のtoolchain headerとlibgccも記録した。

| 観測 | 値 |
|---|---:|
| Flash image | 1,348 B |
| .data | 0 B |
| .bss／静的RAM | 32 B |
| vectors object | 152 B |
| .init section | 156 B |
| entry | 0 |
| 初期stack pointer symbol | 0x20000800 |
| stack peak | 未測定 |

vectorは独立sectionでなく、jumpとともに.initへ置かれる。152 Bと156 Bを同じ容量として二重に加算しない。
選択した静的objectは、I2Cの配列10 B＋pointer／changed各1 B、encoderのint16値4個8 B＋flag等5 B、合計25 B。
link時の整列を含む.bss全体が32 Bとなる。

image SHA-256は `0cfcb9efd101ae41d7ebd5a812452c8a32bcb6262686cae7bdd57c617ff5a35f`。
上流同梱binaryは1,508 Bで、今回のimageとは一致しなかった。
同梱品の作成条件が固定されていないため、その完全再現とは区別する。
32 Bはstack・周辺register・外部masterの保持状態を含まず、実行時RAMの最大値ではない。

## 元のI2C公開面

[config.h](https://github.com/wagiminator/CH32V003-I2C-Knob/blob/5302daa8990ee850de18c963ade8968e4e976350/software/i2c_knob/config.h)と
[i2c_slave.h](https://github.com/wagiminator/CH32V003-I2C-Knob/blob/5302daa8990ee850de18c963ade8968e4e976350/software/i2c_knob/src/i2c_slave.h)は、7 bit address 0x36、配列10 B、register選択byteなしを既定とする。
I2CのSDA／SCLはPC1／PC2、encoder A／B／switchはPA2／PA1／PC4。
[PlatformIO設定](https://github.com/wagiminator/CH32V003-I2C-Knob/blob/5302daa8990ee850de18c963ade8968e4e976350/software/i2c_knob/platformio.ini)のboardはJ4M6で、P2のF4P6と同一個体・配線とは扱わない。

| offset | 上流の公開値 |
|---|---|
| 0〜1 | signed16 wheel value、little endian |
| 2 | switch state、8 bit |
| 3 | loop flag、8 bit |
| 4〜5 | signed16 minimum、little endian |
| 6〜7 | signed16 maximum、little endian |
| 8〜9 | signed16 step、little endian |

[i2c_slave.c](https://github.com/wagiminator/CH32V003-I2C-Knob/blob/5302daa8990ee850de18c963ade8968e4e976350/software/i2c_knob/src/i2c_slave.c)では、ADDRでpointerを0へ戻し、受信byteごとに配列へ直接反映してchangedを立てる。
送信も同じ配列を順次参照し、pointerが10以上なら0へ循環する。
STOP handlerに固定長の検査や完成設定の予約はなく、エラーIRQはBERR／ARLO／AFをclearする。
これはsourceの観察であり、実際のevent順・ACK／NACK・STOP／repeated STARTの波形を観測したものではない。

## 固定K0との違い

| 境界 | 上流U | 実験で固定したK0 |
|---|---|---|
| 公開データ | 同じ10 B配列をread／write | 公開snapshot8 Bと設定frame8 Bを分離 |
| 位置 | signed16 register | signed32 position |
| 設定変更 | 各受信byteで直接変更、changed flag | STOPで8 B完成を検査し、未反映1件を保持 |
| 連続設定 | byte配列とflagに集約 | 未反映が満杯なら後の取引を拒否し、先の設定を保持 |
| 設定検査 | 完成frame・sequence・sticky statusという契約はない | step 1〜127、下限≤上限、不正時に旧設定を維持 |
| 超過read | register配列へ循環 | 8 Bを超えたbyteは0 |
| 読出し保持 | live配列。独立した読出しsnapshot copyなし | 開始時に保持し、STOP／repeated STARTまで固定 |
| encoder入力 | 通常loopでpinをpollし状態を追う | 論理差分frontend、int16飽和とsticky overflow |
| 範囲・表示 | loop flagとswitch registerを持つ | 範囲へ飽和、適用sequence・statusを公開 |
| 初期設定 | mainはI2Cと入力pinを初期化。K0同様の既定設定適用はない | position0・step1・範囲±1000・sequence0 |

[main.c](https://github.com/wagiminator/CH32V003-I2C-Knob/blob/5302daa8990ee850de18c963ade8968e4e976350/software/i2c_knob/src/main.c)は、changedとBUSYを見て設定を回収し、IRQ禁止区間で複数byteを扱う。
wheelの配列更新にもBUSY確認とIRQ禁止を使い、switchの1 Bはloopで更新する。
これを取引全体のsnapshot保証や、一般的なnested critical sectionの証明に読み替えない。
register共有の競合・input edgeの取りこぼし・IRQ禁止時間は、source確認とcompileだけでは評価していない。

上流Uにはruntime初期化・clock／delay・GPIO・I2C slave IRQ等があり、mainと別の部品に分かれている。
共通化の切出しを調べる材料になるが、K0が追加した状態・失敗処理を省いたUの小ささだけで方式を順位付けしない。
旧K0 R/A/B/Cのhost／ABI結果を、この実I2C付きUへ置き換えた扱いにもしない。

## 次の接続で確認すること

- byte直接反映と、STOPでの完成frame受理を区別する。短い取引・repeated START・エラー終了でstagingをどう破棄するかを実frontendへ対応させる。
- 未反映設定が満杯なら、どの状態で何をACK／NACKするかをV003一次資料とpeer観測で確認する。上流のauto ACKをそのまま同じ受理保証にしない。
- 公開snapshotのcopy／切替と、IRQ／通常処理の共有規則を接続する。BUSYの確認だけで全競合が解消したとはしない。
- encoderの実pin解釈・debounce・poll／IRQを、論理差分の比較と分ける。上流のpoll回数を固定K0の入力頻度へ換算しない。
- 同等R対照へ機能と失敗処理を揃えてから費用を比較する。Uのregister互換を新コアの制約へ追加しない。
- board・part・配線・clock・peerを明示して実機検査へ移る。link成功を元boardでの安全な起動確認としない。

## 再現と限界

```sh
uv run --project tests --locked python tests/platform/core_redesign/reproduce_k0_upstream.py \
  --fetch \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-gcc \
  --out work/k0-upstream-new
```

`--fetch`が固定URLだけを取得する。ローカルcopyは `--source-root <LICENSE等を含むroot>` を指定する。
既存makefileのupload targetは呼ばず、全処理を新しいwork directoryへ分離する。通常pytest収集時にfetchやbuildは実行しない。
最終4 ELFの再現とworkspace規則3件を確認し、conftest.pyは追加していない。
今回、nativeなapplication／IRQ emulatorも実行していない。実MCUの起動・wire・入力・時間・stack peakは未検証。

上流のCC BY-SA 3.0表示と著者情報を保ってsourceを証拠へ含める。製品コアへ取り込んでいない。
証拠は `work/exp06-k0-upstream-20261021-evidence.tar.gz`、239,919 B、
SHA-256 `260fec95c23aa1f8be1f5bf7c079184abfdd9301f127fd3b1a8904d3bd47a71e`。
4 ELF・固定source・元LICENSE・toolchain依存・全command出力・JUnit・再集計scriptを含む。

[保持と契約差を読む課題](k0-upstream-exercise.ja.md)は利用者レビュー前である。
