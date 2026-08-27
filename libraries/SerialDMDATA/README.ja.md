# SerialDMDATA

debug moduleのdataレジスタ経由の双方向ターミナルです。
**UARTもpinも配線も要りません。** RAMは36 byteだけです。

使うレジスタは[SerialSDI](../SerialSDI/README.ja.md)と同じ
(debug moduleの`data0`/`data1`、hartのアドレス空間に見えているもの)ですが、
**中身の解釈の取り決めが違い**、そちらにはhost→targetの向きがあります。
`data0`の最下位byteがstatus wordで、bit 7が「targetが置いた」、
bit 6が「targetが待ちを諦めた」、下位bitが長さです。
1往復で**送り7 byte・受け3 byte**。**coreは一度もhaltしません。**

```cpp
#include <SerialDMDATA.h>

void setup() {
  SerialDMDATA.begin(115200);       // 配線が無いのでボーレートは無視されます
  SerialDMDATA.println("hello");
}
```

## host側での受け取り方

[ch32fun](https://github.com/cnlohr/ch32fun)のminichlinkのframingです。
**このcoreはminichlinkを同梱していません。** 上のrepositoryからビルドしてください。

```
minichlink -T
```

プロトコルは公開されているframingから実装したもので、ch32funのコードは使っていません。

**IDEのSerial Monitorでは読めません。** あれはserial portを開くものだからで、
繋ぐには自前のpluggable monitorが要ります(docs/todo.ja.md)。
同じdebug出力でも`SerialSDI`はprobeが自分のCDCへ流すのでSerial Monitorで読めます。
OSごとの手順は[docs/debug-output.ja.md](../../docs/debug-output.ja.md)にまとめてあります。

## SerialSDIとは同じsketchで使えません

同じ2つのレジスタに書くので、片方のframingで読んでいるhostにはもう片方はノイズです。
手元にあるツールで選んでください。

| | host側ツール | 方向 | 代償 |
|---|---|---|---|
| `SerialSDI` | wlink、WCH-LinkUtility | 送信のみ | なし |
| **`SerialDMDATA`** | **minichlink** | **双方向** | **なし** |
| `SerialRTT` | probe-rs attach | 双方向 | RAM |

`SerialRTT`はこのレジスタを使わないので、併用できます。

## 受信にはsketch側のpollingが要ります

hostは**レジスタから何かを取り出した後にしか**書き込みません。
つまり何も置かないtargetは何も受け取れません。`available()`がその役目をします。
届いていれば取り込み、次の3 byteへの招待として空フレームを置きます。
`loop()`でこれを呼ぶことが、このチャネルを双方向にしている実体です。

届いたぶんは16 byteのバッファ(`CH32_DMDATA_RX_SIZE`)に置きます。
1フレーム入る空きが無くなればhostは待たされます。
このバッファがechoを成立させている実体です。レジスタは1フレームしか持たず、
sketch自身の次の`print()`がそれを上書きするので、
置き場所が無いと3フレームに2フレームを失います。
読む量よりはるかに多くprintするsketchでは溢れることがあります。
その場合は`available()`を呼ぶ頻度を上げてください。

## printf()の出力先を変える

```cpp
ch32_set_stdout(&SerialDMDATA);   // printf()がprobeへ
ch32_set_stdout(&Serial);         // UARTへ戻す
ch32_set_stdout(nullptr);         // 捨てる
```

動くのは**stdioだけ**です。`Serial`という名前はコンパイル時に決まるので追随せず、
`Serial.println()`はこれまでどおりの出力先に出ます。

## 知っておくとよいこと

- **hostが居なくても固まりません。** 前のフレームをprobeが取るのを有限回だけ待ち、
  諦めます。しかもそれをstatus wordに*latch*するので、
  以降のwriteは空回りせずタダで返ります。`alive()`がその状態を返し、
  hostがattachすれば自動的に戻ります。
- **番地はfamilyで違います**(V2系`0xE00000F4`、V3系の多く`0xE0000340`、
  V4系とV103`0xE0000380`)。boardが`ch32-device-data`の
  `evidence/debug_data.csv`から渡すので、設定するものはありません。
- **RAMをほとんど使いません。** CH32V003で空sketch(flash 624 byte、RAM 4 byte)
  との差を実測すると flash 700 byte / RAM 36 byte
  (インスタンス・vtable・受信バッファ)。同じことに`SerialRTT`はRAM 364 byteかかります。
- 1往復7 byteは速くありません。トレース用であって、
  実データを流すUARTの置き換えではありません。
- **includeしなければゼロ**です。includeすると、メソッドを一度も呼ばなくても
  インスタンスとvtableのぶん(上の700/36)は載ります。

## examples

- **HelloDMDATA** — 1秒ごとに出力し、打った文字を返します。
