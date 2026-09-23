# SerialDMSeq

debug moduleのデータレジスタを通る双方向の端末です。**UARTもピンも配線も不要**で、
しかも途中で**1 byteも落ちず、二重にもなりません**。

[SerialDMDATA](../SerialDMDATA/README.ja.md)・[SerialSDI](../SerialSDI/README.ja.md)と同じ
`data0`/`data1`(hartのアドレス空間に見えるdebug moduleのレジスタ)を、**dmseq**のframingで
使います。両方向に通し番号、target再起動を知らせるSYN bit、すべてのwordにCRC-8。
1フレームで**送り6 byte**、**受け2 byte**。**coreを止めることはありません。**

```cpp
#include <SerialDMSeq.h>

void setup() {
  SerialDMSeq.begin(115200);        // baudは無視されます(線がないので)
  SerialDMSeq.println("hello");
}
```

## SerialDMDATAではだめな理由

SerialDMDATAはminichlinkのframingで、通し番号がありません。probeの答えの書込みが黙って
落ちる(飛び線で起きます)と、probeは同じフレームをもう一度読み、byteが二重に届きます。
それを答えの書き直しで救おうとすると、sketchが同じ内容を2回出したのと区別できず、今度は
文字を落とします。どちらも実測しています(RP2350越しのCH32L103、ESP32越しのCH32V003)。
dmseqはこの二つを区別でき、CRCで化けたwordを捨てます。attachやflashが`data0`に残すゴミも
これで弾かれます。

仕様はoep-specの`docs/target-console-dmseq.ja.md`(`target.console`のframing 2)、
故障注入つきでframingを選んだ実験はoep-specの`experiments/dm-console-seq`です。

## hostでの読み方

```
ch32rv monitor --source dmseq
```

またはOEP probeの`target.console`をframing 2で。**minichlinkでは読めません**
(あちらはSerialDMDATAのframingです)。

## SerialSDI・SerialDMDATAとは同じsketchで使えません

3つとも同じ2本のレジスタに書くので、hostは他方のframingをノイズとして読みます。

| | hostのtool | 向き | 完全性 |
|---|---|---|---|
| `SerialSDI` | wlink、WCH-LinkUtility | 送りのみ | なし |
| `SerialDMDATA` | minichlink、`ch32rv --source dmdata` | 双方向 | なし |
| **`SerialDMSeq`** | **`ch32rv --source dmseq`、OEP** | **双方向** | **通し番号+CRC** |
| `SerialRTT` | probe-rs attach | 双方向 | - |

## 受信にはsketchのpollが要ります

入力は、sketchが出したフレームへのhostの答えにしか乗りません。印字している間はデータ
フレームがその役をし、暇なときは`available()`が空フレームを出して次の2 byteを促します。
`loop()`で`available()`を呼んでください。書き込みの直後には空フレームを出さないので、
`available()`と`print()`を交互に呼ぶloopでも、printのたびに往復が1回増えることはありません。

届いた分は16 byteのバッファ(`CH32_DMSEQ_RX_SIZE`)に置きます。次のフレームを置く場所がない
ときは受け取らず、hostが送り直すので、落ちることはありません。

## 知っておくとよいこと

- **hostが居なくても固まりません。** hostが一度答えるまでは20 ms(`CH32_DMSEQ_WAIT_MS`)、
  以後は1 s(`CH32_DMSEQ_HOST_WAIT_MS`)待ちます。待ちが切れると、フレームをTO bitを立てて
  出したままにし、hostが答えるまで以後の書込みは捨てます。`alive()`がその状態を返し、
  自動的に戻ります。1秒に1回以上pollするhostなら何も落ちません。待ちは`millis()`ではなく
  レジスタを読んだ回数で数えるので、割込み禁止中でも終わります。
- **SerialDMDATAより重いです。** CH32V003で空sketch(flash 624 byte、RAM 4 byte)と比べ、
  begin・read・writeするsketchで**flash +1268 byte、RAM +52 byte**(SerialDMDATAは+700 / +36)。
  CRCは16要素の表で計算します(256要素表と同じ速さ、bit計算より24 byte多いだけ)。
- **速さ**(1270 byteの出力、実測): ESP32-P4越しのCH32X035で41 kB/s、ESP32越し(SWIO)の
  CH32V003で8.4 kB/s、RP2350越しのCH32L103で9.2 kB/s。WCH-LinkEではアクセスごとにUSBの
  往復がかかるので数kB/sです。データ転送路ではなくコンソールです。
- **レジスタの番地はfamilyで違います**が、boardが`ch32-device-data`から渡すので設定は不要です。

## examples

- **HelloDMSeq** - 1秒ごとに印字し、打った文字を大文字にして返します。
