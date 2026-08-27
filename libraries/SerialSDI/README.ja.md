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

## host側での受け取り方

probe-rsでは現状受け取れません。
[wlink](https://github.com/ch32-rs/wlink)と、firmware 2.10以降のWCH-LinkEが要ります。

```
wlink flash --enable-sdi-print --watch-serial firmware.elf
```

### IDEのSerial Monitorで読む

probeは受け取った文字を**自分のUSB CDC portへ流す**ので、
一度有効にしてしまえば**普通のSerial Monitorがそのまま監視先**になります
(専用のmonitorは要りません)。

```
wlink sdi-print enable                 # 一度だけ。wlinkを終了しても転送は続きます
arduino-cli monitor -p /dev/ttyACM4 -b ch32-riscv-ug:ch32v:CH32V003
```

portはWCH-LinkE自身のCDC(`1a86:8010`)です。IDEなら同じportを選ぶだけです。
有効化をuploadに織り込む手は今のところありません(uploadはprobe-rsのため)。

probe側が対応しているのは V003 / V00x / V103 / V20x / V30x / X035 / L103 です。
他のseriesはレジスタこそありますが、probeのfirmwareがポーリングしません。

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
