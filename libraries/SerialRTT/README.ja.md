# SerialRTT

RAM上のリングバッファ経由の双方向シリアルです。**UARTもpinも配線も要りません。**
しかもhost側は、このcoreが書き込みに使っているツールそのものです。

sketchはRAMに小さなcontrol blockを置きます。magic文字列と、バッファごとの記述子
(先頭アドレス・サイズ・リングバッファに要る2つのoffset)だけです。
probeはこれを見つけて(ELFの`_SEGGER_RTT`シンボル、またはRAMのスキャン)、
あとはdebug transport越しにそのメモリを読み書きするだけです。
**coreは一度もhaltしません。** hostがもう1本のバッファに書けるので`read()`も動きます。

```cpp
#include <SerialRTT.h>

void setup() {
  SerialRTT.begin(115200);          // 配線が無いのでボーレートは無視されます
  SerialRTT.println("hello");
}
```

## host側での受け取り方

同梱のch32rvで、WCH-LinkでもOEP probeでも読めます。

```
ch32rv monitor --source rtt --chip CH32V203
```

control blockはRAMから自分で探すのでELFは要りません。pollのたびにcoreを一瞬止めます。
IDEではportを選び、monitorの`source`を`rtt`にします。
`probe-rs attach --chip <型番> <firmware.elf>`でも読めます。

OSごとの手順は[docs/debug-output.ja.md](../../docs/debug-output.ja.md)にまとめてあります。

probeがtargetをhaltしたままにしていると新しい出力は生成されません。書き直すかresetして
実行を再開してからattachしてください。

## 何を払うか

debug register方式と異なりRAMにbufferを確保します。buffer sizeは`#define`で変更できます。

```
-DCH32RV_RTT_UP_SIZE=64        // target→host、既定256
-DCH32RV_RTT_DOWN_SIZE=8       // host→target、既定16
```

sketchの隣に`build_opt.h`を置くか、arduino-cliの
`--build-property build.extra_flags=...`で渡します。
RAMに余裕がないtargetではbufferを小さくしてください。

## どのdebugチャネルを使うか

| | host側ツール | 方向 | 代償 |
|---|---|---|---|
| `SerialSDI` | wlink、WCH-LinkUtility | 送信のみ | なし |
| `SerialDMDATA` | ch32rv monitor --source dmdata、minichlink | 双方向 | なし |
| **`SerialRTT`** | **ch32rv monitor --source rtt** | **双方向** | **RAM** |

`SerialSDI`と`SerialDMDATA`はdebug moduleの同じレジスタを使うので併用できません。
`SerialRTT`はどちらのレジスタも使わないので、どちらとも同時に使えます。

## printf()の出力先を変える

```cpp
ch32rv_set_stdout(&SerialRTT);      // printf()がリングバッファへ
ch32rv_set_stdout(&Serial);         // UARTへ戻す
ch32rv_set_stdout(nullptr);         // 捨てる
```

動くのは**stdioだけ**です。`Serial`という名前はコンパイル時に決まるので追随せず、
`Serial.println()`はこれまでどおりの出力先に出ます。

## 知っておくとよいこと

- **hostが居なくても固まりません。** `write()`は待ちません。空きぶんだけ書いて
  残りは捨てます(誰も繋がっていないUARTと同じ挙動)。待つのは`flush()`だけで、
  それも有限回で打ち切ります。
- **割り込みからの書き込みは安全ではありません。** write offsetの公開は1命令ですが、
  書き手が2つあるとバイトが混ざります。
- `end()`してもバッファは残します。既にattachしているhostから見て、
  ストリームが壊れたのではなく終わったように見えるためです。
- includeすると、メソッドを一度も呼ばなくてもinstance、vtable、bufferがlink対象になります。

control blockのレイアウトは公開されている仕様のもので、
シンボル名もhost側ツールが探す名前です。**SEGGERのコードは使っていません。**

## examples

- **HelloRTT** — 最小の出力。host側のコマンドも冒頭に書いてあります。
- **RttEcho** — 打った文字を読み返して送り返します。ブロックしません。
