# EXP-04 Flash媒体模型と保存領域の予約

2026-10-20。試作commit `7a2a638`。
V003／X035の物理単位を分け、保存領域の容量計算、独立linkerでの予約、host媒体模型を検査した。
製品core・USB・boards.txt・製品linkerは変更していない。保存形式・API・既定容量は未決定。
実Flash driver、電断試験、CPU／IRQ停止、実upload後の保持はまだ検証していない。

## 物理単位と固定資料

[入力manifest](../../flash-inputs.json)にpart、geometry、一次資料6件のURL・bytes・hashを保存した。
RMは両系列ともV1.9。PDFのページ番号はファイル先頭を1とした値。

- V003：[RM](https://raw.githubusercontent.com/ch32-riscv-ug/CH32V003/12d9aed0eae0276a133db00eb16bd492e8189f2d/datasheet_en/CH32V003RM.PDF) §16.2.1／16.4.1／16.4.3／16.4.6、PDF p.173／178／179／181。
- X035：[RM](https://raw.githubusercontent.com/ch32-riscv-ug/CH32X035/327874484eda7e50cb4cf40271718e68fafbc850/datasheet_en/CH32X035RM.PDF) §20.2.1／20.4.1／20.4.3／20.4.5、PDF p.238／244／246。
- SDKの照合：[V003 Flash source](https://raw.githubusercontent.com/ch32-riscv-ug/CH32V003/12d9aed0eae0276a133db00eb16bd492e8189f2d/EVT/EXAM/SRC/Peripheral/src/ch32v00x_flash.c)、[X035 Flash source](https://raw.githubusercontent.com/ch32-riscv-ug/CH32X035/327874484eda7e50cb4cf40271718e68fafbc850/EVT/EXAM/SRC/Peripheral/src/ch32x035_flash.c)。

| partと比較mode | byte読出し単位 | 1回のprogram | 選んだerase単位 | 高速bufferへのload |
|---|---:|---:|---:|---:|
| V003F4P6 standard | 1 B | 2 B | 1,024 B | 対象外 |
| V003F4P6 fast | 1 B | 64 B | 64 B | 4 Bずつ16回 |
| V003F4P6 fast-erase-1k | 1 B | 64 B | 1,024 B | 4 Bずつ16回 |
| X035C8T6 fast | 1 B | 256 B | 256 B | 4 Bずつ64回 |
| X035C8T6 fast-erase-1k | 1 B | 256 B | 1,024 B | 4 Bずつ64回 |

byte読出しはRMの8／16／32 bitアクセスから選んだ。幅の広いunaligned accessを許す意味ではない。
高速bufferへ4 Bずつloadできることと、Flashへ4 Bだけprogramできることを混同しない。
HSIを有効にする条件も資料にある。停止時間や実際の電源条件は別に確認する。
X035のstandard programをV003から類推して追加していない。V006・X033・H417等へこのgeometryを一般化しない。

同じprogram単位への再書込みの許可条件と、途中中断で影響する物理範囲は未確定。
形式比較の入口となる模型には、下記の保守的な制限を明示した。
これは物理仕様の調査完了を意味せず、再programを必要とする形式を成立済みと扱わない。

## 容量を省略しない領域計算

生成器はpart・mode・消去単位数を受け、予約byte、アプリ容量、link／物理address、program枠数を出す。
0は予約無効。menu候補として0／1／2／4単位を出すが、既定値や製品のmenuは変更しない。
整数単位数は任意に指定でき、端数byteの切上げや型の暗黙変換をしない。
boot先頭／末尾予約も別に差し引き、整列違反やアプリ領域が残らない指定を拒否する。

| 比較mode | 1／2／4単位の予約 | 1単位時のアプリFlash容量 |
|---|---:|---:|
| V003 fast | 64／128／256 B | 16,320 B |
| V003 standard、fast-erase-1k | 1,024／2,048／4,096 B | 15,360 B |
| X035 fast | 256／512／1,024 B | 63,232 B |
| X035 fast-erase-1k | 1,024／2,048／4,096 B | 62,464 B |

この表はboot予約0の比較。program枠数は予約÷program単位であり、設定の保存件数ではない。
metadata、保護方式、更新余地をまだ選んでいないため、有効payload量はJSONで`null`とした。
「64 B予約だから64 Bの設定を保護して保存できる」とは扱わない。
今後は予約、metadata、実データ、更新余地、作業RAMを分けて比較する。

## 独立linkerとimageの検査

アプリFlash領域を予約分だけ短くし、末尾に別のSTORE領域を置くlink probeを生成した。
`.persistent`はNOBITS／NOLOADで予約し、link addressと実Flash addressを別symbolへ出す。
予約領域へ初期値等のinput sectionを置くとlinkを拒否する。アプリのload imageが領域を超えた場合も拒否する。
初期値付きRAMの4 BがFlashのload imageに入ることも確認した。

V003はlink origin 0、物理Flash origin `0x08000000`、容量16,384 B。
X035は同じorigin条件で容量63,488 B。bin／hexのaddressはlink側なので、実uploaderでのmappingを別に確認する必要がある。
ELF section名だけで済ませず、file-backed PT_LOADの範囲、bin長と内容、hexのchecksum・address・内容を照合した。

これは配置だけの小さなprobeで、reset／CRT／clock／Flash driverを含まない。
probe image 24 B、初期値付きRAM 4 B、bss 4 Bを全40構成で確認したが、保存機能の実装費用ではない。
予約量を変えてもbinに予約分のpaddingを出さず、アプリに使える容量を減らして計上する。

`NOLOAD`は書込みimageへの混入を避ける条件で、uploadの消去範囲を制限しない。
明示された物理erase範囲と予約の重複、mass eraseの場合を分類する補助関数も検査した。
実際のupload tool・bootloader・保護解除・firmware更新の消去範囲を検査したわけではない。

## 媒体模型と中断条件

模型は最初に消去済みと宣言したbyte列を持ち、byte read、整列した1 program単位、1 erase単位を扱う。
programは、完了したerase以降にその単位へ一度も試みていない場合だけ許す。
全byteが`0xff`のprogramでも試行済みとし、再programは拒否する。
中断erase後は、そのerase範囲を再利用可能と推測せず、完全なeraseをやり直すまで拒否する。

この試行履歴はhost模型内部の状態で、製品向けの永続bitmapではない。
電源再投入時にFlashが`0xff`に見えるだけでは、その履歴を復元できない。
現在の検査は模型の履歴を保ったまま中断結果を観測するもので、実機reboot／形式の復旧を証明しない。

中断は指定bitのsubsetだけを変化させ、その操作でPowerCutを返す。
program／eraseともbyte mask `00／01／07／55／aa／ff`、programには前半byteだけの条件も使った。
切断前後を含む有限の注入集合であり、全bit組合せ、隣接領域への影響、低電圧時の実挙動を網羅しない。
無効な要求はbyte列・試行履歴・操作記録を変えずに拒否する。
このPython模型のmemory量や処理時間をMCUのRAM・cycleへ換算しない。

## 検証と再実行

- host **141件**：領域44、媒体94、workspace3。conftestを追加せず、禁止ルールも通過。
- 5 geometry×予約0／1／2／4×LTO off／onの**40 ELF**とbin／hexを確認。
- アプリ超過・persistent input混入の想定link拒否 **80件**。
- 正常40構成を再確認し、load先重複、bin末尾追加、hex破損、NOBITSからPROGBITSへの変更を**150件**検出。
- 固定seedの500領域配置と、5 geometry各200操作の正常媒体列も検査した。

[全40行のCSV](region-size-matrix.csv)と[入力・tool・archiveのJSON](region-measurements.json)を保存した。
証拠archiveには固定参照、生成linker、ELF／map／bin／hex、全command、故障artifact、JUnitとhost入力を含む。

```sh
uv run --project tests --locked pytest -c tests/pyproject.toml \
  tests/unit/core_redesign/test_flash_regions.py tests/unit/core_redesign/test_flash_model.py \
  tests/unit/test_workspace.py -o junit_family=xunit1

python3 tests/platform/core_redesign/flash_regions.py \
  --part CH32V003F4P6 --mode fast --erase-units 1 --out work/exp04-one-page

python3 tests/platform/core_redesign/check_flash_regions.py \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-g++ \
  --out work/exp04-flash-link

python3 tests/platform/core_redesign/check_flash_region_faults.py \
  --evidence work/exp04-flash-link --out work/exp04-flash-faults
```

出力先は未作成の場所を使う。link検査は`--sources 保存先`で固定参照の再取得を避けられる。
artifact故障検査は元の入力hashが現行sourceと一致することも要求する。

## 次の比較

RAW、固定blob、数値keyを1／2／4消去単位で比較する。公開RAWと内部専用、上書きと旧値保持、
commit marker、CRC等の検出、満杯・整理・世代上限・中断後の再更新を別の能力として扱う。
高速pageを一度だけprogramする条件では、同じpageへcommit markerを後付けする案を無条件に使えない。
別program単位、別erase領域、検出だけの形式などを分け、有効量と保護の差を測る。
物理条件が未確定の比較は条件付きの感度分析とし、実Flashで成立した方式に数えない。

実driverでは所有・HSI・保護・整列、CPU／IRQ／DMAへの影響、readbackとerror、時間・耐久性を確認する。
実upload保持、実電断、保存中のPD等との共存にはそれぞれ別の証拠が必要。
方式の採用と既定容量はP5に残し、[容量と保持を考える課題](region-exercise.ja.md)も利用者評価の素材とする。
