# デバッグ出力

CH32RV では UART と debug interface を使う複数の `Print` / `Stream` を選べます。

| API | Host 側 | 方向 | 主な制約 |
|---|---|---|---|
| `Serial` | Arduino Serial Monitor | 双方向 | UART pin と baud rate が必要 |
| `SerialSDI` | `ch32rv monitor --source sdi` | target → host | WCH-LinkE の SDI print 対応 chip に限定 |
| `SerialRTT` | `ch32rv monitor --source rtt` | 双方向 | RAM ring buffer を使用し、poll 時に短時間 halt する |
| `SerialDMDATA` | `ch32rv monitor --source dmdata` | 双方向 | `SerialSDI` と同じ register を使うため併用不可 |
| `SerialDMSeq` | `ch32rv monitor --source dmseq` | 双方向 | DMI transport が必要。通し番号と CRC-8 を使用 |

API の buffer size、`printf()`、同時使用制約は各 library の README を正本とします。

## Arduino IDE / CLI

WCH-Link は `wchlink://…`、OEP probe は `oep://…` の port として discovery されます。WCH-LinkE の UART bridge は、それとは
別の serial port（Windows の `COMx`、Linux の `/dev/ttyACM…`）として出ます。

モニターが何を読むか（ch32rv の monitor の `source`）は、ツールメニューの **Monitor Source** で選びます。IDE 2 の Serial Monitor の窓には
monitor の設定のうちボーレートしか出ないためです（arduino-cli の `--config source=…` はメニューより優先）。

| Monitor Source | serial port（LinkE の COM など） | `wchlink://` | `oep://` |
|---|---|---|---|
| UART (Serial)（既定） | `uart`: UART bridge の素通し | ch32rv の既定（`dmdata`）のまま。UART は COM の口を選ぶ | `fixture-uart`: probe の UART の線 |
| DMSeq / DMDATA / SDI / RTT | その経路（debug で attach） | 同じ | 同じ |

`uart` 以外は debug で attach するので、走っている firmware のクロックが組み直されることがあり、V103 / V205 では register が 1 つ壊れることが
あります（[chip-caveats.ja.md](chip-caveats.ja.md)）。モニターの窓の先頭に `[ch32rv monitor] attaching may have changed the target clock`
が出たら attach した印です。

CLI では次の形です。

```sh
ch32rv probe list
ch32rv monitor --source rtt --chip CH32V103 --probe serial:<probe-serial>
```

通常の UART は OS の serial port を Arduino monitor で開きます。

```sh
arduino-cli monitor -p /dev/ttyACM0 -b ch32-riscv-ug:ch32rv:CH32V103
```

複数 probe がある場合は USB serial または discovery が返した port URI で対象を指定します。`/dev/ttyACM0` や
`COM7` のような番号は抜き差しで変わるため、設定の識別子として固定しません。

## Linux の権限

Board Manager installation には `60-ch32rv.rules` と `post_install.sh` が含まれます。権限不足が出る場合は
次で診断し、表示された udev rule を導入します。

```sh
ch32rv doctor
ch32rv doctor --emit-udev
```

UART / CDC port には distribution の serial-port group (`dialout` 等)への所属も必要です。

## 経路の選び方

- 通常の user I/O と互換性を優先するなら `Serial`
- UART pin と RAM を使わず一方向の log だけ欲しいなら `SerialSDI`
- 大きめの双方向 buffer が必要なら `SerialRTT`
- RAM を節約し、DMI が安定している環境なら `SerialDMDATA`
- 欠落・重複を検出できる console が必要なら `SerialDMSeq`

probe は一度に一つの process だけが開けます。upload、monitor、WCH utility、別 debugger が同じ probe を保持して
いる場合は、不要な process を終了してから接続します。
