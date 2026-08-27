# デバッグ出力の受け取り方(OS別)

文書基準日: 2026-08-27

CH32のsketchから文字を出す経路は4つあり、**host側で必要なものがそれぞれ違います**。
UARTと`SerialSDI`はArduinoのSerial Monitorがそのまま使えます。
`SerialRTT`と`SerialDMDATA`はSerial Monitorでは読めないので、専用のコマンドで受けます。
この文書はその手順を、OSごとの差分込みでまとめたものです。

| 経路 | host側 | Serial Monitorで読めるか | 配線 |
|---|---|---|---|
| `Serial`(UART) | 何も要らない | **読める** | WCH-LinkEのUARTブリッジ、または外付けadapter |
| [`SerialSDI`](../libraries/SerialSDI/README.ja.md) | WCH-LinkUtility(またはwlink)で一度有効化 | **読める**(probeのCDC port) | 不要 |
| [`SerialRTT`](../libraries/SerialRTT/README.ja.md) | `probe-rs attach` | 読めない | 不要 |
| [`SerialDMDATA`](../libraries/SerialDMDATA/README.ja.md) | `minichlink -T` | 読めない | 不要 |

`SerialRTT`と`SerialDMDATA`をSerial Monitorに繋ぐには、Arduinoの
pluggable monitorプロトコルを喋る専用ツールを配布する必要があります。
今は用意していないので、この文書の手順で受けてください
(方針は[todo](todo.ja.md)を参照)。

## 0. 共通: ビルド成果物の置き場所を自分で決める

`SerialRTT`は**ELFを渡す必要があります**(probe-rsがそこからRTT control blockの
在り処を引くため)。IDEのビルド成果物はOS依存の一時ディレクトリに出るので、
CLIで`--build-path`を指定して固定するのがいちばん確実です。

```sh
arduino-cli compile \
  --fqbn ch32-riscv-ug:ch32v:CH32V103:pnum=CH32V103R8T6 \
  --build-path ./build \
  ./MySketch
# → ./build/MySketch.ino.elf  (と .bin / .hex)
```

`--build-path`は既存のディレクトリをそのまま使い、中身を消しません(arduino-cli 1.3.1で確認)。
sketchを差し替えるときは古い`*.elf`が残らないよう、消してから使ってください。

IDEでビルドしたELFを使いたい場合は、環境設定で「詳細な出力を表示: コンパイル」を
有効にすると、ログの最後に一時ディレクトリのpathが出ます。
そこの`<sketch名>.ino.elf`が同じものです。

書き込みは経路を問わず共通です。

```sh
arduino-cli upload \
  --fqbn ch32-riscv-ug:ch32v:CH32V103:pnum=CH32V103R8T6 \
  --programmer wch-link --input-dir ./build ./MySketch
```

probeが複数刺さっているときは、`probe-rs list`が出す`VID:PID:Serial`をそのまま渡します。

```sh
arduino-cli upload ... --upload-property upload.probe_args="--probe 1a86:8010:434A124C5596"
```

## 1. OSごとの下準備

probeの**vendor interface**(debug用)と**CDC port**(UART/SDI用)は別物で、
必要な権限もOSごとに違います。

### Linux

CDC portは`/dev/ttyACM*`として見えます。ユーザーを`dialout`グループへ入れてください。

vendor interfaceは`udev` ruleが無いと`libusb_open() = -3 (LIBUSB_ERROR_ACCESS)`で
開けません。`/etc/udev/rules.d/60-wch-link.rules`に置きます。

```
# WCH-Link / WCH-LinkE. 8010 と 8012 が RISC-V mode、8011 が ARM mode(CMSIS-DAP)
SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="8010", GROUP="plugdev", MODE="0660"
SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="8011", GROUP="plugdev", MODE="0660"
SUBSYSTEM=="usb", ATTR{idVendor}=="1a86", ATTR{idProduct}=="8012", GROUP="plugdev", MODE="0660"
```

```sh
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### macOS

ドライバは要りません。CDC portは`/dev/cu.usbmodem*`です
(`/dev/tty.usbmodem*`もありますが、**`cu.`のほうを使ってください**。
`tty.`はDCD待ちでopenがブロックすることがあります)。
vendor interfaceの権限設定も不要です。

### Windows

CDC portは`COM3`のように見えます。デバイスマネージャーか
`arduino-cli board list`で番号を確認してください。

**vendor interfaceにはドライバが要ります。** 公式の
`WCH-LinkUtility.ZIP`に入っている`Drv_Link/WCHLinkDrv_WHQL_S.exe`を実行するのが
いちばん確実で、これでprobe-rsもwlinkも動くようになります。

うまくいかないときの手段として[Zadig](https://zadig.akeo.ie/)でWinUSBを
割り当てる方法もありますが、**`WCH-Link (Interface 0)`だけ**に当ててください。
CDC側に当てるとCOM portが消えます(`Options > List All Devices`で選べます)。

### WSL2

USBはWindows側にしか見えないので、`usbipd-win`でWSLへ渡します。

```powershell
usbipd list
usbipd bind   --busid <BUSID>          # 初回のみ、管理者権限
usbipd attach --wsl --busid <BUSID>
```

WSLの`vhci_hcd`は**high-speed portを8本しか持たない**ので、
9台目のattachは`no free port`で失敗します。使わないデバイスは`usbipd detach`してください。
attach後はLinuxの手順(udev rule、`dialout`)がそのまま必要です。

## 2. `Serial`(UART)

普通のSerial Monitorです。WCH-LinkEはdebug interfaceの他にCDCを持っていて、
**そこがUARTブリッジ**なので、1本のケーブルで書き込みとUART受信の両方が取れます。

```sh
arduino-cli monitor -p /dev/ttyACM4 -b ch32-riscv-ug:ch32v:CH32V103
```

ボーレートの既定は**115200**をboardが宣言しているので`--config`は要りません。
変えたいときは`--config baudrate=9600`のように渡します。

| OS | portの例 |
|---|---|
| Linux / WSL | `/dev/ttyACM4` |
| macOS | `/dev/cu.usbmodem1234561` |
| Windows | `COM7` |

## 3. `SerialSDI` — WCH-LinkEで有効化してはじめて使える

**この経路は「そのままでは出ない」のが正しい状態です。** sketchを書いても、
何も設定していなければ1文字も出てきません。coreの不具合でもsketchの間違いでもなく、
**probe側でSDI printを有効にするまで、probeが転送しない**からです。

前提は2つあります。

- **WCH-LinkEであること。** 初代WCH-Link(CH549)にも**WCH-LinkWにも**ありません。
  WCH-LinkUtilityは`Only WCH-LinkE support SDI Printf function!`と言って断りますし、
  wlinkのソースも`support_sdi_print()`を**LinkE(CH32V305ベース)だけ**に限定しています
  (LinkWは電源制御には対応していますが、SDI printは対象外です)
- **chipが対応していること。** V003 / V00x / V103 / V20x / V30x / **V317** /
  X035 / L103 です(wlinkの`RiscvChip::support_sdi_print()`より)。
  対象外だと``Current chip type does`t support SDI Printf function!``になります

そして有効にすると、**通常のUART Serialと同じportに混ざって出てきます**。
WCH-LinkEのCDCは**UARTブリッジとSDIの受け口を兼ねている**ので、
`Serial.println()`とSerialSDIの両方を使っていると、
1つのSerial Monitorに両方が流れ込みます。これは仕様であって、分離はできません。
分けたいときはどちらか一方だけを使ってください。

有効化のやり方は3つあり、環境で選びます。

| | 対応OS | 位置づけ |
|---|---|---|
| **WCH-LinkUtility** | **Windowsのみ** | WCH公式。単体配布はWindows専用 |
| **MounRiver Studio 同梱のOpenOCD** | Linux / macOS / Windows | 同じくWCH純正。CLIから叩ける |
| **wlink** | Linux / macOS / Windows | サードパーティ(ch32-rs)のCLI |

### 3.1 WCH-LinkUtility(WCH公式、Windowsのみ)

公式ユーティリティです。probeのドライバとファームウェアも同じZIPに入っているので、
Windowsならこれ1つで下準備が済みます。

1. WCHのWCH-Link製品ページから`WCH-LinkUtility.ZIP`を取って展開する
2. `Drv_Link/WCHLinkDrv_WHQL_S.exe`でドライバを入れる(初回のみ)
3. `WCH-LinkUtility.exe`を起動し、**Target**メニュー → **Connect WCH-Link**
4. **Target**メニュー → **Enable SDI Printf**
5. WCH-LinkUtilityを閉じる(probeを掴んだままだと他のツールが使えません)

戻すときは同じメニューの**Disable SDI Printf**です。
公式マニュアル(`Doc/WCH-LinkUserManual-EN.pdf`)の
**5.2.11 SDI Virtual Serial Port Function**が該当箇所で、
「EnableSDIPrintfをチェックし、WCH-LinkEのCOM portを開く」「**V1.80以降**」とあります
(手元で確認したのはV2.20)。

**ZIPの中身は`.exe`と`.dll`とWHQLドライバだけで、Linux/macOS版はありません。**

### 3.2 MounRiver Studio 同梱のOpenOCD(Linux / macOS / Windows)

Linux版MounRiver Studioに入っているOpenOCD(WCHのfork)は、
**`sdi_printf`というコマンドを持っています**。
MRSのdownloadダイアログにある`SDIPrintf`のチェックはこれを呼んでいます。
LinkUtilityが無い環境ではこれが純正の手段です。

```sh
cd <MounRiver>/OpenOCD/bin
./openocd -f wch-riscv.cfg -c "sdi_printf enable" -c init -c exit
# → Info :  SDI_PRINTF  ENABLE
```

**順番が決まっています**。`sdi_printf`は`init`の**前**、`exit`は`init`の**後**です。
逆にすると`The 'sdi_printf' command must be used before 'init'.`と言われます。

**このコマンドで無効化はできませんでした。** 引数に`disable`/`0`/`off`のどれを渡しても
`SDI_PRINTF ENABLE`になります(MRS側も有効化のフラグしか持っていません)。
戻すときはWCH-LinkUtilityの**Disable SDI Printf**か、`wlink sdi-print disable`を使ってください。

Linuxでは`libjaylink.so.0`が要りますが、**MRSが同梱しています**。

```sh
export LD_LIBRARY_PATH=<MRS>/resources/app/resources/linux/components/WCH/Others/CommunicationLib/default
```

このベンチでの実測(2026-08-27、CH32V003 + WCH-LinkE): `probe-rs download`で書き込み →
上のopenocdで有効化 → `arduino-cli monitor`で`uptime N s`が読めました。
**wlinkを一切使わずにLinuxで完結します。**

### 3.3 wlink(サードパーティCLI、Linux / macOS / Windows)

[wlink](https://github.com/ch32-rs/wlink)はこのcoreに同梱していません。
releaseのバイナリを取るか`cargo install --git`で入れてください。

```sh
wlink sdi-print enable        # 一度だけ。wlinkを終了しても転送は続きます(実測)
wlink sdi-print disable       # 戻す
```

書き込みと監視をまとめてやることもできます(wlink自身の窓に出ます)。

```sh
wlink flash --enable-sdi-print --watch-serial ./build/MySketch.ino.elf
```

wlinkのドキュメントは**firmware 2.10以降**のWCH-LinkEを条件に挙げています。

### 3.4 読む

どれで有効化しても、読み方は同じです。**普通のSerial Monitor**が監視先になります。

```sh
arduino-cli monitor -p /dev/ttyACM4 -b ch32-riscv-ug:ch32v:CH32V103
```

portは**WCH-LinkE自身のCDC**(`1a86:8010`)です。
前述のとおり、ここには**UART Serialの出力も一緒に流れてきます**。

**有効化はuploadに含まれません。** 書き込みツールがprobe-rsのため、
今のところ1回は手で有効にする必要があります。

## 4. `SerialRTT` — `probe-rs attach`

probe-rsはこのcoreが書き込みに使っているツールなので、**追加で入れるものはありません**。
渡すのは**ELF**です。

```sh
probe-rs attach --chip CH32V103R8T6 ./build/MySketch.ino.elf
```

- `--chip`にはboardメニューの`pnum`がそのまま使えます
- attach時にtargetは**resetされません**。すでに走っているところへ横から入ります。
  probeがhaltしたまま残した状態にattachすると、それ以前の出力しか出ません。
  その場合は`probe-rs reset --chip <pnum>`してからattachしてください
- 打った文字はtargetの`read()`へ届きます(down channel)
- probeを選ぶときは`--probe 1a86:8010:<serial>`

probe-rsの実体はcoreがvendorしているものが使えます。

| OS | 場所 |
|---|---|
| Linux / macOS | `<core>/.tools/probe-rs/<version>/probe-rs` |
| Windows | `<core>\.tools\probe-rs\<version>\probe-rs.exe` |

Board Managerで入れた場合は`~/.arduino15/packages/ch32-riscv-ug/tools/probe-rs/<version>/`
(Windowsは`%LOCALAPPDATA%\Arduino15\packages\...`)にあります。

## 5. `SerialDMDATA` — `minichlink -T`

[ch32fun](https://github.com/cnlohr/ch32fun)のminichlinkが要ります。
**このcoreは同梱していないので、自分でビルドしてください。**

```sh
git clone https://github.com/cnlohr/ch32fun
cd ch32fun/minichlink
make                     # Linux: libusb-1.0-dev と libudev-dev が要ります
./minichlink -T
```

| OS | ビルドに要るもの |
|---|---|
| Linux | `sudo apt install build-essential libusb-1.0-0-dev libudev-dev` |
| macOS | `brew install libusb`、あとは`make` |
| Windows | MSYS2 (mingw64) で`make`、または同梱のprebuilt binary |

- `-T`は**ターミナルをtty(端末)から読みます**。
  pipeやリダイレクトで標準入力を与えると、EOFを`0xff`の連打として送り続けるので、
  **必ず端末から起動してください**
- 打った文字はtargetの`read()`へ届きます(1往復3 byte)
- `SerialSDI`と**同じレジスタ**を使うので、両方を1つのsketchで使うことはできません

## 6. つまずきやすいところ

- **`SerialSDI`を有効にするとCDCの下り(host→target)が死ぬことがある。**
  bannerは読めるのにtargetへ文字が届かない、という形で出ます。
  probeを挿し直す(WSLなら`usbipd detach`+`attach`)と直ります
- **probe-rsやminichlinkでflashすると、probeが持っているchip情報が古くなる**ことがあります。
  直後の`probe-rs info`が失敗したら`wlink reset`、
  またはbenchの`smoke.py`が使っている**リセット無しの再検出**
  (vendor commandの`81 0d 01 03`)で戻ります
- **`--build-path`は中身を消しません。** sketchを差し替えたら、古い`*.elf`を掴まないよう
  ディレクトリごと消してから使ってください
- **probeを掴めるのは1つのプロセスだけです。** WCH-LinkUtilityやwlinkを開いたままだと
  `arduino-cli upload`が失敗します。設定を変えたら閉じてください
- **portは固定ではありません。** 抜き差しやreboot、WSLのattach順で番号が変わります。
  `arduino-cli board list`か、probeのUSB serial(`probe-rs list`)で確認してください
