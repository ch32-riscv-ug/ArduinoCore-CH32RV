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
