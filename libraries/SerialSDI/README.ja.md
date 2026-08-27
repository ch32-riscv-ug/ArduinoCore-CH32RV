# SerialSDI

デバッグモジュール経由のシリアル出力です。**UARTもpinも配線も要りません。**

`data0`/`data1`の2ワードはRISC-Vデバッグモジュールのabstract data registerで、
hartのアドレス空間に見えています。番地は`hartinfo.dataaddr`がハードウェア自身で
述べている事実で、**魔法の番地ではありません**。
指示を受けたWCH-LinkEがこれをポーリングし、自分のUSBシリアルへ転送します。
**coreは一度もhaltしません。**

番地は**familyで違います**。V2系(V003/V00x)が`0xE00000F4`、V3系の多く
(V205/V407/X315/M030)が`0xE0000340`、V4系とV103が`0xE0000380`です。
boardが`CH32_DM_DATA0_ADDR`として渡すので**sketch側で指定するものはありません**
(出所は`ch32-device-data`の`evidence/debug_data.csv`)。

```cpp
#include <SerialSDI.h>

void setup() {
  SerialSDI.begin(115200);          // 配線が無いのでボーレートは無視されます
  SerialSDI.println("hello");
}
```

## 書いただけでは出ません(それが正しい状態です)

このlibraryは**WCH-LinkEで有効化してはじめて使えます**。
何も設定していなければ1文字も出てきませんが、それは不具合ではなく、
**probeが転送を始めていない**だけです。条件は2つ。

- **WCH-LinkEであること。** 初代WCH-Link(CH549)にも**WCH-LinkWにも**ありません
- **chipが対応していること。** V003 / V00x / V103 / V20x / V30x / V317 / X035 / L103

有効にすると、probeは受け取った文字を**自分のUSB CDC portへ流します**。
つまり**普通のSerial Monitorがそのまま監視先**になります(専用のmonitorは要りません)。

**そのportはUARTブリッジと同じです。** `Serial.println()`も使っていると、
1つのSerial Monitorに**両方が混ざって**流れてきます。分離はできないので、
混ぜたくなければどちらか一方だけを使ってください。

## 有効化のしかた

probe-rsでは有効化できないので、そこだけ別のツールを使います。

| | 対応OS | |
|---|---|---|
| **WCH-LinkUtility** | **Windowsのみ** | WCH公式。**Target**メニュー → **Enable SDI Printf** |
| **MounRiver Studio 同梱のOpenOCD** | Linux / macOS / Windows | 同じくWCH純正。`openocd -f wch-riscv.cfg -c "sdi_printf enable" -c init -c exit` |
| **[wlink](https://github.com/ch32-rs/wlink)** | Linux / macOS / Windows | `wlink sdi-print enable` |

```
wlink sdi-print enable                 # 一度だけ。wlinkを終了しても転送は続きます
arduino-cli monitor -p /dev/ttyACM4 -b ch32-riscv-ug:ch32v:CH32V003
```

wlinkなら書き込みと監視をまとめることもできます。

```
wlink flash --enable-sdi-print --watch-serial firmware.elf
```

portはWCH-LinkE自身のCDC(`1a86:8010`)です。IDEなら同じportを選ぶだけです。
**OSごとの詳しい手順は[docs/debug-output.ja.md](../../docs/debug-output.ja.md)にあります。**
有効化をuploadに織り込む手は今のところありません(uploadはprobe-rsのため)。

対象外のseriesはレジスタこそありますが、probeのfirmwareがポーリングしません。

## printf()の出力先を変える

```cpp
ch32_set_stdout(&SerialSDI);      // printf()がprobeへ
ch32_set_stdout(&Serial);         // UARTへ戻す
ch32_set_stdout(nullptr);         // 捨てる
```

動くのは**stdioだけ**です。`Serial`という名前はコンパイル時に決まるので追随せず、
`Serial.println()`はこれまでどおりの出力先に出ます。

## 知っておくとよいこと

- **hostが居なくても固まりません。** ベンダ実装はprobeがフレームを取るまで
  無限に待ちますが、こちらは有限回で打ち切って捨てます
  (誰も繋がっていないUARTと同じ挙動)。デバッガを抜いてもsketchは止まりません。
- **送信専用です。** プロトコルにはhost→target方向もありますが、
  こちらでは検証していないので`read()`は常に-1を返します。
- 1フレーム7バイトで、次を書く前にprobeが取り終える必要があります。
  速くはありませんし、大量出力でUARTを置き換えるものでもありません。
- **includeしなければゼロ**です。ただしincludeすると、使わなくても
  インスタンスとvtableのぶん(V003実測でflash 364 byte / RAM 20 byte)は載ります。

## examples

- **HelloSDI** — 最小の出力。host側のコマンドも冒頭に書いてあります。
- **PrintfToSDI** — `printf()`をprobe側へ移し、また戻します。
