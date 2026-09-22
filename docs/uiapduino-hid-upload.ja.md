# UIAPduinoのHID書き込みと復旧

対象: UIAPduino Pro Micro CH32V003 V1.4 / `ch32rv` 0.9.0以降

## 1. 役割の分担

書き込みは次の2段階です。

1. CH32V003を製品bootloaderへ入り直し、USB HID `1209:b803`として列挙させる。
2. `ch32rv boot hid flash`でArduino buildの`.bin`を書き込む。

`ch32rv` 0.9.0が行うのは手順2です。手順1のSWIO操作は、現時点では
`wch-protocols` E129の実験用ESP32ジグfirmwareを使います。Arduino IDEのUploadボタンから
boot entryまで自動化するrecipeはまだありません。

## 2. 通常の書き込み

通常利用ではESP32ジグは不要です。RESETを押したままUSBへ接続するか、実行中のsketchから
BOOT_MODEを設定してsoftware resetし、製品bootloaderへ入ります。後者を利用するには正常に
動作するapplication側へboot entry処理をあらかじめ組み込んでおく必要があります。

Arduino IDE / CLIではboardに
`UIAPduino Pro Micro CH32V003 V1.4`（FQBN
`ch32-riscv-ug:ch32v:UIAPDUINO_V003_V14`）を選びます。このnamed boardの通常Uploadは
以下のHID書込みだけを担当し、RESET操作や外部ジグ操作は行いません。

bootloaderが見えていることを確認します。UIAPduinoは`1209:b803`、通常のrv003usbは
`1209:b003`です。

Linuxでは、例えば次のように確認できます。

```sh
lsusb -d 1209:b803
```

Arduino buildで生成された`.bin`を書き込みます。

```sh
ch32rv boot hid flash Blink.ino.bin
```

UIAPduinoだけを対象にして、別のrv003usb機器を選ばないようにする場合はVID:PIDを明示します。

```sh
ch32rv boot hid flash Blink.ino.bin --usb-id 1209:b803
```

書き込みはCH32V003 user flashの先頭`0x08000000`から行います。16 KiBを超えるimageは拒否されます。
64 byte pageごとに現在値を読み、異なるpageだけをerase/programし、read-back verifyします。成功後は
applicationを起動するため、`1209:b803`がUSBから消えるのは正常です。

Arduino platformへ専用upload recipeを追加する場合のコマンド本体は次の形です。

```text
"{path}/{cmd}" --non-interactive --progress none boot hid flash \
  "{build.path}/{build.project_name}.bin" --usb-id 1209:b803
```

既存のWCH-Link用`flash <ELF>` recipeとは別のupload toolとして定義します。HID routeへ`.elf`を
渡さず、`recipe.objcopy.bin.pattern`が生成する`.bin`を渡してください。

## 3. 検証・復旧用SWIOジグでboot modeへ入る

この節は自動HILと、applicationからbootloaderへ戻れない場合の復旧用です。製品boardの
標準書込み手順ではありません。

実験で確認した最小配線は次の2本です。

| ESP32ジグ | UIAPduino CH32V003 | 備考 |
|---|---|---|
| GPIO16 | PD1 / SWIO | 3.3 V、単線SWIO |
| GND | GND | GNDを共通化 |

GPIO23をPD7/RSTへ接続した配線も利用できますが、通常の復帰には不要です。GPIO23は使用しない間
INPUT（Hi-Z）にします。異なる電源から給電する場合は、3.3 V logicであることとGND共通を先に
確認してください。

ESP32へ次の実験firmwareを書き込みます。pin-mapと周辺機能のHILを兼ねるE132も、同じ
`N/B/R/H/S/W/V`コマンドを提供します。

```text
wch-protocols/experiments/e129_swio_only_cpu_boot/e129_swio_only_cpu_boot.ino
wch-protocols/experiments/e132_uiapduino_pin_map/e132_uiapduino_pin_map.ino
```

ESP32のserial consoleを開き、1 byteのASCII command `B`を送ります。改行は不要です。

```text
SWIO BOOT BEGIN
...
CPU RESUME name=prepare_boot_and_reset
SWIO CPU_RESET_ARMED delay_loops=5000000
SWIO BOOT END
```

この後、通常は約1～2秒で`1209:b803`が列挙されます。最大90秒待っても現れなければ失敗として
扱います。`B`はSWIOからRAM payloadを注入し、BOOT_MODEとPD4を設定して、CH32V003自身に
software resetを実行させます。user flashと製品bootloader領域は書き換えません。

## 4. 失敗時の復旧

### HIDで待ち受けなくなったがSWIOには応答する

この症状は、直ちに製品bootloaderの破損を意味しません。application側で動作している、
BOOT_MODEがuser側に残っている、またはbootへ移るreset sequenceだけが成立しなかった場合にも
`1209:b803`は現れません。SWIOに応答するなら、flashを書き換える前にRAM payloadでboot条件を
作り直します。

ここでいうSWIOはCH32V003のPD1に出ている1-wire debug信号です。ジグ側でSWDIOと呼んでいる
場合も同じ線を指し、SWCLKは使いません。

復旧順序は次のとおりです。

1. HIDやSWIOを開いている他のtoolを終了する。
2. ESP32ジグのserial consoleで`?`を送り、`READY commands=NBRHSWV`を確認する。
3. `B`を1回送り、`SWIO BOOT END`の後に`1209:b803`が現れるか待つ。
4. `B`で戻らなければ、`N`でBOOT_MODEをuser側へ明示的に正規化してsoftware resetし、
   `NORMALIZE END`を確認してから、もう一度`B`を送る。
5. GPIO23↔PD7/RSTも接続してある場合のfallbackだけ、`H`を1回試す。
6. `1209:b803`が現れたら、通常どおり`ch32rv boot hid flash`で正しい`.bin`を書き込む。

```text
?  -> ESP32ジグfirmwareの応答確認
B  -> SWIO-onlyでBOOT_MODE/PD4を準備し、CPU software reset（第一選択）
N  -> user modeへ正規化してCPU software reset（状態を揃える診断・前処理）
S  -> FLASH_STATRのBOOT_LOCK/BOOT_MODE/BOOT_STATUSを表示（診断のみ）
H  -> boot条件を準備し、GPIO23から外部reset（RST配線がある場合のfallback）
R  -> GPIO23からresetするだけ（boot modeには入れない）
```

`S`はtargetをattach/haltして状態を読む診断なので、そこで手順を終えず、続けて`B`またはresetを
実行します。`R`単独はboot条件を設定しないため、HID復旧操作としては使用しません。

出力からの切り分け:

| ESP32出力 | 主な確認箇所 |
|---|---|
| `reason=attach_failed` | GPIO16↔PD1、共通GND、3.3 V、SWIO idle HIGH、他probeとの競合 |
| `reason=normalize_failed` | DMI通信またはRAM payload転送。配線を直し、`N`を上限付きで再試行 |
| `reason=boot_payload_failed` | RAM payloadのwrite/read-backまたはDMI操作。一度`N`を通してから`B`を再試行 |
| `SWIO BOOT END`後もHIDなし | USB data cable、USB port、OS列挙、usbip/driver、製品bootloader本体を確認 |

Windowsの`usbipd list`で`0000:0002`の「デバイス記述子要求の失敗」として現れる場合は、
boot entry以前ではなくUSB列挙開始後の失敗である。WSLの`lsusb`に現れないことだけを根拠に
bootloader無応答と判定せず、USB配線・信号品質・clockと、BOOT領域の部分破損を切り分ける。

実機では一時的なSWIO/DMI誤読を検出して再送した例と、最初のboot payload操作だけ失敗して
再試行で復帰した例があります。ただし同じ操作を無制限に繰り返さず、配線確認後の`N → B`、
必要なら`H`までを1組の復旧試行として扱います。

`B`、`N`、`H`はいずれも製品bootloader領域を書き換えません。この手順でもSWIO操作は完了する
のにHIDが一度も列挙されず、USB側にも問題がない場合は、製品bootloader自体の破損を疑います。
E129～E131の手順はuser flashの復旧と既存bootloaderへのentryであり、破損したbootloaderの
再書き込み手順ではありません。bootloaderを復元する場合は、対象board版に対応する既知の正しい
imageを用意し、bootloader領域を明示的に扱える専用のSWIO書き込み手順を別途使用してください。

### `HID bootloader 1209:b803 was not found`

1. applicationが起動してHIDが消えた直後なら正常です。次の書き込み前にジグへ`B`を送ります。
2. USB cable、給電、OSのUSB列挙、Linuxのdevice permissionを確認します。
3. ジグのGPIO16↔PD1/SWIOとGNDを確認し、`B`をもう一度送ります。
4. `B`が繰り返し失敗し、GPIO23↔PD7/RSTも接続してある場合だけ、実験firmwareの`H`を試します。
   `H`はSWIOでboot状態を準備してから、RSTを20 ms LOWにするfallbackです。

### HID transfer timeout / device open error

HID deviceを開いている別の書き込みツールを終了し、USBを挿し直してから、ジグの`B`と
`ch32rv boot hid flash`を順に再実行します。処理はpage単位でpreverifyされるため、途中まで
一致しているpageは再書き込みされません。

### verify mismatch / flash status error

同じ操作を無制限に繰り返さず、次のcaptureを1回取ります。

```sh
ch32rv --capture hid-failure.ndjson boot hid flash Blink.ino.bin \
  --usb-id 1209:b803
```

captureには書き込むfirmwareの内容が含まれるため、公開前に扱いを確認してください。ジグで
再度boot modeへ入り直しても同じpageで失敗する場合は、電源・配線・flash protectionを調べます。

### `target is read-protected`

HID経路だけでは解除しません。SWIO対応probeで保護状態を確認し、必要ならunprotectしてから
製品bootloaderまたはapplicationを書き戻します。read protection解除はuser flash消去を伴う
可能性があるため、バックアップ無しで実行しないでください。製品bootloader領域には書き込まない
ことを復旧手順の条件にします。

### applicationもHIDも起動しない

まずジグの`B`で製品bootloaderへ戻します。HIDが再列挙できれば、正しい`.bin`をもう一度
書き込めます。HIDまで戻れない場合の最終手段はSWIOからの直接復旧です。E130/E131で、user
flashを64 byte page単位でerase/program/verifyし、製品bootloaderを残したまま復旧できることを
実機確認しています。ただしこれは現在まだ`ch32rv`の一般向けcommandではなく、実験用ESP32
firmwareとhost scriptによる経路です。

2026-09-19の実機復旧では、未使用の同型基準機をWCH-LinkE serial `49878F06CE37`で3回ずつ
読んだimageと比較しました。故障機のBOOT 1,920 byteとoption 16 byteは基準機と完全一致し、
壊れていたのはuser applicationだけでした。このためBOOTには書かず、異なる230/256 pageだけへ
初期applicationを書き戻しました。全16 KiBの再読出しが基準hashと一致した後、`N`→`B`で
`1209:b803`へ復帰し、専用boardのHID uploadとHIL suiteが成功しました。

この結果からも、HID不在だけを理由にBOOTを書き戻してはいけません。先に各領域を保全し、複数回
読出しまたは多数決読出しでSWIOの一時的な1 bit誤読を排除してから比較します。実測ではBOOT読出し
にも不安定sampleがありましたが、多数決後のimageは基準機と一致しました。

同日後半にも、`B`、`N→B`、`H`がすべて完了する一方でWindows列挙が`0000:0002`のままになる
状態を再現しました。このときは物理的な電源再投入をせず、BOOT/optionへも書かず、保全済みの
初期application 16 KiBだけをE132から64 byte単位でSWIO書込み・page verifyしました。256 page中
1 pageで一時的なverify誤読がありましたが上限付き再試行で回復し、256/256成功後の`N→B`で、同じ
USB BUSIDが`1209:b803`へ復帰しました。その後9,140 byteのTIM HIL imageをHID書込みし、起動と
実波形試験まで成功しています。

この追試では復旧前applicationの全体dumpを取っていないため、application内容が破損していたとは
断定しません。確定しているのは、USB抜き差しやBOOT書換えなしで、既知のapplicationへのSWIO復元と
`N→B`だけで復帰したことです。量産・自動ベンチでは、復旧前dumpを保存してから同じ処理を行います。

## 4a. 2026-09-22 の判明事項: bootloader に留まる条件は `RCC_RSTSCKR.PINRSTF`

製品 bootloader（BOOT 領域 1,920 B、factory capture と一致）を逆アセンブルすると、entry 直後に `RCC_RSTSCKR`（0x40021024）の
**bit26 = PINRSTF（外部 reset pin による reset）**を見て、立っていなければ即「PD4 low → BOOT_MODE clear → PFIC reset」で
application へ戻る。`B`（BOOT_MODE 設定 + PD4 detach + software reset）だけでは PINRSTF は立たないので、**直前に pin reset があった
ことが必要**。flag は RMVF を書くまで残るので、過去に RESET ボタン / GPIO23 を使った後は `B` だけで入れ、誰かが RMVF を書くと入れなく
なる。ArduinoCore-CH32 の `CH32.resetReason()`（`libraries/CH32/src/CH32System.cpp`）は初回読み出しで RMVF を書き全 flag を消す。
つまり `resetReason()` を呼ぶ sketch（`system_selftest` など）を動かした後は、pin reset 無しの boot entry は失敗する。これが
「B / N→B / H が完了するのに `0000:0002` のまま」の少なくとも一因である。

実測（OEP probe、同日）:

| 手順 | RSTSCKR | 結果 |
|---|---|---|
| RMVF で flag を消してから boot payload | SFTRSTF のみ | app へ戻る（`0000:0002` のまま） |
| GPIO23 で NRST を 20 ms low → boot payload | PINRSTF あり | **1209:b803 が 1 s で列挙** |
| BOOT_MODE=1 だけ設定して pin reset | PINRSTF あり | app が動く（bootloader は留まらない） |

もう 1 つ: RAM payload を app が動いている状態から resume すると SysTick 割込みが payload を壊す（`mcause=2`）。`mstatus=0` を書いて
から resume すること（E135 の loader 経路はこれをしていた）。

**ジグの置き換え**: `esp32-d0wd-v3-0070070d9394` には現在 oep-probe-arduino `examples/Esp32V003Probe`（OEP v0 probe）が入っており、
E129/E132 の `N/B/H` は次に対応する（同じ配線、GPIO23 → PD7/NRST を使う）。

```sh
cd ~/dev_oep/oep-client-python
uv run python -m oep_client.v0 --port /run/board-identify/by-id/esp32-d0wd-v3-0070070d9394 reset --mode boot   # pin reset → E129 payload → 1209:b803
uv run python -m oep_client.v0 --port /run/board-identify/by-id/esp32-d0wd-v3-0070070d9394 reset --mode user   # E130 normalize（N 相当）
uv run python -m oep_client.v0 --port /run/board-identify/by-id/esp32-d0wd-v3-0070070d9394 reset --mode pin    # NRST 20 ms low だけ（R 相当）
```

E129/E132 の jig sketch に戻す場合は該当 `.ino` を書き直す（OEP probe が上書きしている）。

**USB を使わない sketch で PC に「壊れたデバイス」を見せない方法**（同日実測）: この board は D− に固定 pull-up があるため、USB stack の
無い app が動いていると host は「デバイスあり・記述子応答なし」= `0000:0002` を表示する。app が **PD4（USB D−）を push-pull output LOW**
にすると host からは切断（SE0）に見え、`usbipd list` から消える。INPUT に戻すと `0000:0002` が再び現れる（2 往復再現）。
**2026-09-22 決定・実装**: UIAPduino variant は起動時（`initVariant()`、`variants/UIAPduino_Pro_Micro_CH32V003_V14/variant.cpp`）に
PD4 を push-pull LOW にする。通常の sketch では PC に何も見えない。software USB stack を使う sketch / library は begin 時に自分で
PD3/PD4 を設定するので競合しない。`initVariant()` は variant が定義するため sketch 側で再定義はできない（必要なら `setup()` で
`pinMode(PD4, INPUT)` に戻す）。

未決（利用者判断）: core の `resetReason()` が PINRSTF を消す挙動をどうするか。案 A: UIAPduino variant では RMVF を書かない
（reason は「累積」になり `reason_stable` の意味が変わる）。案 B: 現状維持し、boot entry は必ず pin reset 経由（本書の手順）。
案 C: `resetReason()` が PINRSTF だけ残して他を消す（ハードでは個別 clear 不可なので実現不能）。

## 5. 現在の制限

- 対応を実機確認したのはCH32V003とUIAPduino `1209:b803`です。
- HID書き込みとSWIO boot entryは別操作です。
- ESP32ジグのserial commandは安定版protocolではなく実験用です。
- Arduino IDEのUploadボタンからジグを制御する統合は未実装です。
- SWIO-only boot entryは外部RST不要で2/2、Arduino imageの書き込み・実行・bootloader復帰は
  2/2、HID書き込みは11/11成功を確認しています。

純正UIAPコアとのsource互換用に、専用board variantでは
`pinV32_DisconnectDebug(PD_1)`も定義します。引数はArduino pin番号の`PD1`ではなく、純正の
`PinName`表現`PD_1`です。この呼出しはSWIOをresetまで無効化するため、PD1をGPIOとして使う
applicationだけが明示的に呼んでください。通常のHIL sweepとboot entry試験では呼びません。

実験根拠は`wch-protocols`のE129（SWIO boot entry）、E130（page round-trip）、E131
（Arduino image全体）を参照してください。
