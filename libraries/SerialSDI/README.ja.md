# SerialSDI

デバッグモジュール経由のシリアル出力です。**UARTもpinも配線も要りません。**

`0xE0000380`/`0x384`の2ワードはRISC-Vデバッグモジュールのabstract data registerで、
hartのアドレス空間に見えています。この部品では`hartinfo`が`dataaddr=0x380`と
自己申告するので、**魔法の番地ではなくハードウェアが述べている事実**です。
指示を受けたWCH-LinkEがこれをポーリングし、自分のUSBシリアルへ転送します。
**coreは一度もhaltしません。**

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

**wlinkがセッションを保持している間だけ**probeが転送するので、
`--watch-serial`はおまけではなく手順の一部です。

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
- sketchが名前を書かない限り、このインスタンスはリンクされません。

## examples

- **HelloSDI** — 最小の出力。host側のコマンドも冒頭に書いてあります。
- **PrintfToSDI** — `printf()`をprobe側へ移し、また戻します。
