# SerialSDI

デバッグモジュール経由のシリアル出力です。**UARTもpinも配線も要りません。**

`data0`/`data1`の2ワードはRISC-Vデバッグモジュールのabstract data registerで、
hartのアドレス空間に見えています。番地は`hartinfo.dataaddr`がハードウェア自身で
述べている事実で、**魔法の番地ではありません**。
host（同梱のch32rv、またはWCH-LinkE自身）がデバッグの線越しにこれを読みます。
**coreは一度もhaltしません。**

番地は**familyで違います**。V2系(V003/V00x)が`0xE00000F4`、V3系の多く
(V205/V407/X315/M030)が`0xE0000340`、V4系とV103が`0xE0000380`です。
boardが`CH32RV_DM_DATA0_ADDR`として渡すので**sketch側で指定するものはありません**
(出所は`ch32-device-data`の`evidence/debug_data.csv`)。

```cpp
#include <SerialSDI.h>

void setup() {
  SerialSDI.begin(115200);          // 配線が無いのでボーレートは無視されます
  SerialSDI.println("hello");
}
```

## 読み方

**Arduino IDEのシリアルモニターで、そのまま読めます。** Monitor Sourceの既定のAutoが、`Serial`（UART）と一緒に
SerialSDIの出力も読みます（同梱のch32rv 0.35.0から）。portは`wchlink://…`（WCH-Link）か`oep://…`（OEPのprobe）を選びます。
ch32rvが郵便受けをデバッグの線越しに直接読むので、**probeの設定は何も変えません**。

```
ch32rv monitor --source sdi                    # SerialSDIだけを読む
ch32rv monitor --source auto                   # UARTなど、ほかの経路と一緒に読む
```

LinkEのCOM port（UARTブリッジ）だけを開いても、SerialSDIの出力は出てきません。`wchlink://…`のportを選んでください。

## LinkEの転送で読む（WCH純正のやり方）

WCH-LinkE自身に転送させる方法もあります。転送を有効にすると、LinkEは受け取った文字を**自分のUSB CDC port**へ流します。
ただし、転送している間は、**同じCDCにUARTの出力が届かなくなります**（UARTブリッジが止まります）。

- **WCH-LinkEだけ**です。初代WCH-Link(CH549)にも**WCH-LinkWにも**ありません。
- **probeが転送する系列だけ**です（V003 / V00x / V103 / V20x / V30x / V317 / X035 / L103）。ほかの系列はレジスタこそありますが、
  probeのfirmwareがポーリングしません（ch32rvが直接読む上の方法なら、ほかの系列でも読めます）。

```
ch32rv monitor --source sdi-forward            # 転送を有効にしてからCDCを読む
ch32rv monitor sdi off                         # 転送を切る（LinkEのRAMの設定なので、電源を切っても消えます）
```

WCH純正のツールでも有効にできます。

| | 対応OS | |
|---|---|---|
| **WCH-LinkUtility** | **Windowsのみ** | WCH公式。**Target**メニュー → **Enable SDI Printf** |
| **MounRiver Studio 同梱のOpenOCD** | Linux / macOS / Windows | 同じくWCH純正。`openocd -f wch-riscv.cfg -c "sdi_printf enable" -c init -c exit` |
| **[wlink](https://github.com/ch32-rs/wlink)** | Linux / macOS / Windows | `wlink sdi-print enable`（`wlink flash --enable-sdi-print --watch-serial firmware.elf`で書き込みと監視をまとめられます） |

IDEのシリアルモニターでは、転送を有効にしてから、Monitor SourceのUART（またはAuto）で同じportを開けば読めます
（モニターはLinkEの設定を変えないので、メニューに転送の項目はありません）。
転送を有効にしたままだと、LinkEが先に文字を取るので、ch32rvの`sdi`・`auto`では読めません（ch32rvが警告
`sdi-quiet`で`ch32rv monitor sdi off`を案内します）。
**詳しくは[docs/debug-output.ja.md](../../docs/debug-output.ja.md)にあります。**

## printf()の出力先を変える

```cpp
ch32rv_set_stdout(&SerialSDI);      // printf()がprobeへ
ch32rv_set_stdout(&Serial);         // UARTへ戻す
ch32rv_set_stdout(nullptr);         // 捨てる
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
- includeすると、使わなくてもinstanceとvtableがlink対象になります。

## examples

- **HelloSDI** — 最小の出力。host側のコマンドも冒頭に書いてあります。
- **PrintfToSDI** — `printf()`をprobe側へ移し、また戻します。

## サンプルの詳しい使い方

- [HelloSDI](examples/HelloSDI/README.ja.md)：配線・操作・API の使い方と改造時の注意。
- [PrintfToSDI](examples/PrintfToSDI/README.ja.md)：配線・操作・API の使い方と改造時の注意。
