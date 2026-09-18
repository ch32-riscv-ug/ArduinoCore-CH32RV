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

## 3. SWIOジグでboot modeへ入る

実験で確認した最小配線は次の2本です。

| ESP32ジグ | UIAPduino CH32V003 | 備考 |
|---|---|---|
| GPIO16 | PD1 / SWIO | 3.3 V、単線SWIO |
| GND | GND | GNDを共通化 |

GPIO23をPD7/RSTへ接続した配線も利用できますが、通常の復帰には不要です。GPIO23は使用しない間
INPUT（Hi-Z）にします。異なる電源から給電する場合は、3.3 V logicであることとGND共通を先に
確認してください。

ESP32へ次の実験firmwareを書き込みます。

```text
wch-protocols/experiments/e129_swio_only_cpu_boot/e129_swio_only_cpu_boot.ino
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

実験根拠は`wch-protocols`のE129（SWIO boot entry）、E130（page round-trip）、E131
（Arduino image全体）を参照してください。
