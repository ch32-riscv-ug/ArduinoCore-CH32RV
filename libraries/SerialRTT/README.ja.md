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

```
probe-rs attach --chip CH32V003F4P6 <firmware.elf>
```

`--chip`にはboardメニューの`pnum`がそのまま使えます。渡すのは**ELF**です。
probe-rsはそこからcontrol blockの在り処を引きます。

**IDEのSerial Monitorでは読めません。** あれはserial portを開くものだからで、
繋ぐには自前のpluggable monitorが要ります(docs/todo.ja.md)。
Linux/macOSなら`socat`でptyに橋渡しすればIDEの窓でも読めます。

このベンチでの実測(probe-rs 0.32.0): `download`してから`attach`すれば、
走行中のストリームがそのまま出ます。**CH32V003でも動きます**
(23秒で18行、打ち込んだ文字のecho back込み)。CH32V203も同様。
probeがhaltしたまま残した状態にattachすると、それ以前の出力しか出てきません。
書き直すかresetしてからattachしてください。

## 何を払うか

他の2方式が使わないRAMを使います。CH32V003で空sketch(flash 624 byte、RAM 4 byte)
との差を実測した値です。

| | flash | RAM |
|---|---|---|
| includeしない | 0 | 0 |
| 参考: `SerialSDI` | +364 | +20 |
| 参考: `SerialDMDATA` | +700 | +36 |
| `SerialRTT`、バッファ256/16(既定) | +656 | +364 |
| `SerialRTT`、バッファ64/8 | +640 | +164 |

RAMの大半はバッファで、`#define`で変えられます。

```
-DCH32_RTT_UP_SIZE=64        // target→host、既定256
-DCH32_RTT_DOWN_SIZE=8       // host→target、既定16
```

sketchの隣に`build_opt.h`を置くか、arduino-cliの
`--build-property build.extra_flags=...`で渡します。
RAM 2 KBの部品ではやる価値がありますし、20 KBの部品では既定のままで問題ありません。

## どのdebugチャネルを使うか

| | host側ツール | 方向 | 代償 |
|---|---|---|---|
| `SerialSDI` | wlink、WCH-LinkUtility | 送信のみ | なし |
| `SerialDMDATA` | minichlink | 双方向 | なし |
| **`SerialRTT`** | **probe-rs attach** | **双方向** | **RAM** |

`SerialSDI`と`SerialDMDATA`はdebug moduleの同じレジスタを使うので併用できません。
`SerialRTT`はどちらのレジスタも使わないので、どちらとも同時に使えます。

## printf()の出力先を変える

```cpp
ch32_set_stdout(&SerialRTT);      // printf()がリングバッファへ
ch32_set_stdout(&Serial);         // UARTへ戻す
ch32_set_stdout(nullptr);         // 捨てる
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
- **includeしなければゼロ**です。includeすると、メソッドを一度も呼ばなくても
  インスタンス・vtable・バッファのぶん(上の表)は載ります。

control blockのレイアウトは公開されている仕様のもので、
シンボル名もhost側ツールが探す名前です。**SEGGERのコードは使っていません。**

## examples

- **HelloRTT** — 最小の出力。host側のコマンドも冒頭に書いてあります。
- **RttEcho** — 打った文字を読み返して送り返します。ブロックしません。
