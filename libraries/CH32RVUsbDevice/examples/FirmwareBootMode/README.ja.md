# FirmwareBootMode

[English](README.md)

動いているスケッチから、石の工場出荷の USB ブートローダーに入ります（EspUsbDevice の FirmwareBootMode と同じ）。BOOT に
触らずに flash 全体を書き直せます。ブートローダーは石の BOOT 域にあるので、入れ替えのコードを持たないスケッチでも使え、
ほかのすべての入れ替えの道の後ろにある、戻すための道になります。

## 必要なものと配線

CH32X035 / X033（`CH32RVUsbDevice::enterBootloader()` が工場出荷のブートローダー 1a86:55e0 に届く。X305 / X315 は同じ
レジスタを持つがまだ試していない。ほかの系列では普通のリセットで、ブートローダーには BOOT0 の端子でしか入れない）と、
PC への USB のケーブル。ログは 115200 bps の UART（`RX` / `TX`）。PC にはコアの書き込みの道具の ch32rv。

## 操作と期待する結果

スケッチは `Console` という名前の CDC のポートと、DFU ランタイムのインタフェースを見せます。ブートローダーへは 3 つの道が
あります。

| 操作 | ホストの側 |
|---|---|
| CDC のポートを 1200 bps で開いて DTR を下げる | `arduino-cli upload ...`、Arduino IDE、`stty -F /dev/ttyACM0 1200` |
| `DFU_DETACH` | `dfu-util -e`（複数の機器があるときは `-d 1209:0001`） |
| Console のポートに `b` を送る | `printf b > /dev/ttyACM0` |

デバイスはバスを離れ、1a86:55e0 として戻ってきます。あとはいつもどおり書きます（`ch32rv isp`、または USB の道での
arduino-cli upload）。初めの 2 つはコードが要りません（CDC のポートと DFU ランタイムのインタフェースが受けます）。3 つ目は
`CH32RVUsbDevice::enterBootloader()` で、戻りません（USB のコールバックの中からは、答えが出てから入る
`requestBootloader()` を使います）。
