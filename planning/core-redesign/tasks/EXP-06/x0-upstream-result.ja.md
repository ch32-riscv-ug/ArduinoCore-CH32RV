# X0参照元のビルド再現と実device関数の観測

2026-10-21。再現tool／記録driverの実装commit `bfb67c4`。
対象は[UC-01の固定commit](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/tree/7eba86abd2467bb53f609189d8c3c7d75e4d6c00)。
[入力・全24 fileのURLとhash](../../x0-upstream-inputs.json)、[測定・観測・tool記録](x0-upstream-measurements.json)を保存した。
上流そのものをU対照として再現する単位で、X0候補の仕様を変更したり、同等機能の比較を完了したりしたものではない。
製品コア・USB・Networkは編集していない。

## ビルドを再現する

固定sourceを読出し、既存makefileの7個のC source・flags・linker scriptを使って直接GCCを呼ぶ。
source／header／startup／makefileには変更を加えない。追加したmap出力は観測用で、uploadや実機起動はしない。
元のprojectは[PlatformIO設定](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/blob/7eba86abd2467bb53f609189d8c3c7d75e4d6c00/software/pd_adapter/platformio.ini)でF7P6を指定し、
[makefile](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/blob/7eba86abd2467bb53f609189d8c3c7d75e4d6c00/software/pd_adapter/makefile)は48 MHz・rv32imac／ilp32・Os・LTOあり。
今回のcompilerはxPack GCC 14.3.0。発行者が同梱binaryを作ったcompiler版まで再現した扱いにはしない。

ローカルのhash検証済みcopyから2回、固定URLから取得し直したcopyから2回、最終記録の計4 ELFを生成した。
4個のload imageはbyte単位で一致した。source fileを変更していないことと、依存するtoolchain header・libgccのhashも確認した。

| 観測 | 値 |
|---|---:|
| Flash image | 4,984 B |
| .data | 40 B |
| .bss | 140 B |
| 静的RAM合計 | 180 B |
| vector section | 220 B |
| entry | 0 |
| 初期stack pointer symbol | 0x20005000 |
| stack peak | 未測定 |

image SHA-256は `fa56b7743308274480f3026609a71adc8e6f8750e206867cc0f12a3960390fb1`。
上流同梱のbinaryは5,264 Bで、今回のimageとは一致しなかった。
同梱品の元tool環境が固定されていないため、同梱binaryの完全再現を達成したとしない。
link成功とsymbolの配置は、nakedなreset handler・割り込み・PDが実siliconで正しく動くことの証明ではない。

## 元のdevice関数をそのまま呼ぶ

上流のoled.c／ina219.cと元headerをhostでcompileし、I2C_start／restart／write／read／stopだけを記録関数へ置き換えた。
GPIO・software I2C・SysTick・PDはnative実行へlinkしない。
ASan／UBSanと警告errorで16入力を実行し、ローカルcopyと再取得copyの計32回で全出力が一致した。
これはhostの呼出し列で、SCL／SDA・ACKの電気観測やcontrollerの状態模型ではない。

| 呼出し | 記録した取引のdata byte数・形 |
|---|---|
| OLED_init | command control 0x00＋9 command＝10 B |
| OLED_setCursor | control・page範囲・column／page指定＝7 B |
| OLED_clearLine | cursor取引7 B、その後data control 0x40＋256 zero＝257 B |
| OLED_clearScreen | clearLine 0／2の4取引 |
| OLED_printVal | control 0x40＋5文字×16 B＝81 B |
| OLED_printStrのNO PPS | control 0x40＋6文字×16 B＝97 B |
| INA_init | configの3 Bとcalibrationの3 B、2取引 |
| INA_read | register pointerを1 B write、同じ取引内でrepeated START、2 B read、ACK→NACK、STOP |

init／cursor／clear／INAのbyte列・順序・読出し結果をassertした。
数値・文字表示はcontrol・長さ・spacingを確認し、glyph byte列そのものは記録のみ。独立したfont正解表と照合したとはしない。
[oled.h](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/blob/7eba86abd2467bb53f609189d8c3c7d75e4d6c00/software/pd_adapter/src/oled.h)の0x78と
[ina219.h](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/blob/7eba86abd2467bb53f609189d8c3c7d75e4d6c00/software/pd_adapter/src/ina219.h)の0x80は、上流I2C APIへ渡すwrite address byte。
今回のMasterが受け取る7 bit addressなら、それぞれ0x3c／0x40へ対応させる必要がある。

## Uと固定X0は何が違うか

X0の固定条件は[用途契約](../../workloads.ja.md)と[P2入力](../../p2-inputs.json)。そこを今回の観測へ読み替えない。

| 境界 | 固定上流U | X0の固定条件・既存候補 |
|---|---|---|
| 表示 | SSD1306 128×32、上流独自5×16文字、vertical指定 | 128×64 page byte、共通3×5 glyphと空白列 |
| 転送単位 | 同期の一括呼出し。257／97／81 B等 | 描画payload最大32 B＋controller header。headerとsetupは未確定 |
| I2C | PA5／PA6のsoftware I2C、定数400 kHz | P2はC8のhardware I2C default PA10／PA11。実速度は未測定 |
| bus失敗 | software writeはACKを無視。stretch不可の前提 | 受理・Busy・error prefix・cancel・停止／回収を別に扱う |
| 計測 | INA219の2 B registerを電圧／電流で別々に読む | 用途値はint32のmv／ma、論理結果payload8 B。物理手順は別 |
| 入力 | INC／DECで20 mV変更、RSTは積算reset。明示Applyなし | 増加・減少・Apply、容量4、受理sessionを保持 |
| PD | 実controller、IRQと通常側更新。同期negotiate API | 5 tickの仮想終端、用途状態・失敗・旧session回収の比較 |
| 周期 | loopの描画・I2C・待機に依存 | 計測100 tick、追加touch20 tick、frame開始最小100 tick |
| 表示内容 | 電圧・電流・電力・時間・積算電荷／energy等 | 希望値・成立値・測定値・有効／stale／接続状態 |

[config.h](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/blob/7eba86abd2467bb53f609189d8c3c7d75e4d6c00/software/pd_adapter/config.h)、
[oled.c](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/blob/7eba86abd2467bb53f609189d8c3c7d75e4d6c00/software/pd_adapter/src/oled.c)、
[i2c_soft.c](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/blob/7eba86abd2467bb53f609189d8c3c7d75e4d6c00/software/pd_adapter/src/i2c_soft.c)、
[ina219.c](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/blob/7eba86abd2467bb53f609189d8c3c7d75e4d6c00/software/pd_adapter/src/ina219.c)、
[main.c](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/blob/7eba86abd2467bb53f609189d8c3c7d75e4d6c00/software/pd_adapter/src/main.c)から確認した。
controllerの正規仕様へ適合するかは、これらの上流sourceだけでは証明しない。

## 分割と進行の調査材料

上流はmainとは別に、startup／clock／delay・GPIO・millis・software I2C・OLED・INA・PDの部品を持つ。
[system.c](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/blob/7eba86abd2467bb53f609189d8c3c7d75e4d6c00/software/pd_adapter/src/system.c)がvector・reset・.data copy・BSS clear・初期化を持ち、
[millis.c](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/blob/7eba86abd2467bb53f609189d8c3c7d75e4d6c00/software/pd_adapter/src/millis.c)はSysTick IRQで時刻を進める。
[usbpd_sink.c](https://github.com/wagiminator/CH32X035-USB-PD-Adapter/blob/7eba86abd2467bb53f609189d8c3c7d75e4d6c00/software/pd_adapter/src/usbpd_sink.c)はIRQの受信解析・GoodCRC後の処理・resetと、通常側のPD_updateを持つ。
PD_negotiateは5 msのbusy delayとupdateを繰り返して待つ。mainにも起動待ち、キーrepeat待ち、通常delayと初期交渉失敗時の無限待機がある。
そのため、上流をそのまま短いcallbackへ入れてノンブロッキング化できたとは扱わない。

この構成は共通化の候補を調べる材料で、Arduino Coreの方が軽い／重いという仮説の結論ではない。
機能・失敗処理・bus実装・描画内容・buffer保持・stackを揃えたR対照との比較が引き続き必要。

## 次の接続条件

- SSD1306の一次資料で、128×64・既存page byte rendererへ合うaddressing／column／page・初期化・headerを照合する。上流の128×32 vertical初期化を流用して成立済みにしない。
- payload32 B＋headerの実storage、commandとdataの取引境界、失敗した部分frameの再開／再描画を定義する。既存比較のglyph・配置・payload上限は変更しない。
- INAのregister手順・calibration・conversion状態と、論理8 B結果への集約・stale・途中失敗を接続する。単に8 Bを一度readした扱いにしない。
- 上流のPA5／PA6と実験hardware routeの配線差をfixtureで明示する。既存boardへ同じimageをそのまま書けるとは扱わない。
- PDをIRQで必須な部分と通常進行へ切り分け、外部protocolの期限を別途確認する。同期negotiate APIを短いtaskへ隠して終了にしない。
- controller／計測の未確定入力は、候補の有利な測定値を見てから変更しない。比較契約を変える必要がある場合は高位判断へ戻す。

## 再現手順と限界

```sh
uv run --project tests --locked python tests/platform/core_redesign/reproduce_x0_upstream.py \
  --fetch \
  --compiler .tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-gcc \
  --native-compiler /usr/bin/gcc \
  --out work/x0-upstream-new
```

`--fetch`だけが固定URLを取得する。既存copyを使う場合は `--source-root <LICENSE等を含むroot>` に置き換える。
通常のpytest収集時にsource取得・compile・uploadは行わない。toolは各実行の専用TMPDIRを使う。
最終は4 ELFの再現、16 native条件×2 copy、workspace規則3件を確認した。
初期のsnapshot directory不足を修正し、共有/tmpのquotaで失敗したcompile、workspace pluginのdisk I/O errorと長すぎるUNIX socket pathは、専用かつ短い一時領域で解消した。失敗記録もarchiveへ保存した。

実MCUの起動・I/O・PD source・電圧／電流・時間・stack peakは未検証。
sourceは上流のCC BY-SA 3.0表示と著者情報を保って証拠へ含める。製品コアへ取り込んだものではない。
証拠は `work/exp06-x0-upstream-20261021-evidence.tar.gz`、565,350 B、
SHA-256 `ceae947a928abfd49c824049f704ff0f0e5c5363480304908a449da394467eaf`。
最終4 ELF・2 native recorder・全source・元LICENSE・toolchain依存・全command出力・失敗記録・再集計scriptを含む。

[比較条件を読む課題](x0-upstream-exercise.ja.md)は利用者レビュー前である。

対応する小容量側の[K0上流再現](k0-upstream-result.ja.md)も完了した。両Uを元のsource／flagsで再現したことと、K0／X0の比較契約へ同等化したことを区別して、P3／P4の費用・実機評価へ接続する。

続く[device入力の一次資料照合](x0-device-result.ja.md)では、上流ADC設定のdecode、header込みwire、address順の模型と通信Low継続timeoutを確認した。通信成功・新conversion・用途反映を区別し、この上流build／host呼出しの成功を実更新周期や実wire成立へ読み替えない。
