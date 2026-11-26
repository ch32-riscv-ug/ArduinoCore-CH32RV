# デバッグ出力

CH32RV では UART と debug interface を使う複数の `Print` / `Stream` を選べます。

| API | Host 側 | 方向 | 主な制約 |
|---|---|---|---|
| `Serial` | Arduino Serial Monitor | 双方向 | UART pin と baud rate が必要 |
| `SerialSDI` | `ch32rv monitor --source sdi` | target → host | 郵便受け（DM の DATA0）を使う。`SerialDMDATA`・`SerialDMSeq` と併用不可 |
| `SerialRTT` | `ch32rv monitor --source rtt` | 双方向 | RAM ring buffer を使用し、poll 時に短時間 halt する |
| `SerialDMDATA` | `ch32rv monitor --source dmdata` | 双方向 | `SerialSDI` と同じ register を使うため併用不可 |
| `SerialDMSeq` | `ch32rv monitor --source dmseq` | 双方向 | DMI transport が必要。通し番号と CRC-8 を使用 |

API の buffer size、`printf()`、同時使用制約は各 library の README を正本とします。

**どれを使うか:**

1. **UART が使えるなら `Serial`。** debug interface に attach しないので、target に何も起きません。
2. **UART が使えないなら `SerialDMSeq`。** debug の線だけで双方向に使え、通し番号と CRC-8 で取りこぼしや重複が分かります。
3. **ログを出すだけ（一方向）で、大きさを最小にしたいなら `SerialSDI`。** 例のスケッチ（X035）で `SerialSDI` 1264 B、
   `SerialDMSeq` 2392 B でした。PC からスケッチへは送れません。

`SerialDMDATA`（ch32fun の minichlink と同じ書き方。番号が無く、重複や欠けを見分けられない）と `SerialRTT`（poll のたびに
短時間 halt する）は、それが要る事情があるときに選びます。

## Arduino IDE / CLI

WCH-Link は `wchlink://…`、OEP probe は `oep://…` の port として discovery されます。WCH-LinkE の UART bridge は、それとは
別の serial port（Windows の `COMx`、Linux の `/dev/ttyACM…`）として出ます。

モニターが何を読むか（ch32rv の monitor の `source`）は、ツールメニューの **Monitor Source** で選びます。IDE 2 の Serial Monitor の窓には
monitor の設定のうちボーレートしか出ないためです（arduino-cli の `--config source=…` はメニューより優先）。既定は **Auto** で、
スケッチが使っている経路を全部一緒に読みます。

**Auto（既定、ch32rv 0.35.0 から）:**

- **読むもの:** UART（LinkE の CDC、OEP の probe の fixture-uart）と、郵便受け系の 1 つ（`SerialDMSeq`・`SerialSDI`・`SerialDMDATA`
  のうちスケッチが使うもの）と、RTT（`SerialRTT` を使っていれば）を、1 本にまとめて出します。どの経路から来た行かは付きません。
  行は経路ごとにまとめて出すので、2 つの経路の行が 1 行に混ざることはありません。
- **送るもの:** 窓から送った文字は、スケッチの出力が最後に来た経路へ送ります。まだ何も来ていなければ UART へ送ります。
  `SerialSDI` は受け取れないので、最後が SDI のときも UART へ送ります。
- **何を読んでいるか:** 窓に `[ch32rv monitor] auto: uart + dmseq + rtt` のような行が出ます（変わるたびに出ます）。
- **`wchlink://`:** モニターを開くたびにスケッチを最初から走らせ直します（接続でクロックが組み直されたり、V103 / V205 で
  レジスタが壊れたりする副作用を、走らせ直しで消すため）。開く前に窓で打った文字は捨てます。スケッチが自分で再起動したら、
  郵便受け系と RTT を選び直します。
- **`oep://`:** スケッチを止めず、走らせ直しもしません。郵便受け系は DMSeq → SDI → DMDATA の順に 3 秒ずつ試して、データが来た
  ものに決めます（決まるまで数秒かかります）。決めた経路が 5 秒黙ったら試し直します。UART は、probe の口に rx / tx のラベルが
  あるときだけ読みます。
- **serial port（LinkE の COM など）:** UART だけを読みます。

| Monitor Source | serial port（LinkE の COM など） | `wchlink://` | `oep://` |
|---|---|---|---|
| Auto（既定） | `auto`: UART だけ | `auto`: CDC の UART・郵便受け系・RTT | `auto`: fixture-uart・郵便受け系・RTT |
| UART (Serial) | `uart`: UART bridge の素通し | `uart`: Link の CDC | `fixture-uart`: probe の UART の線 |
| DMSeq / DMDATA / SDI / RTT | その経路（debug で attach） | 同じ | 同じ |

`SDI` は、WCH-Link でも ch32rv が郵便受けを DMI で直接読むので、UART と一緒に読めて、Link の設定も変えません。
LinkE 自身の SDI の転送（WCH-LinkUtility・wlink などで有効にするもの）はメニューにありません。メニューのどの経路も、
LinkE の転送の設定を変えません（入れることも切ることもしません）。転送を有効にしてあれば、転送された文字は CDC に来るので、UART（または Auto）で読めます。転送している間は、CDC に
UART の出力が届きません。CLI では `ch32rv monitor --source sdi-forward` が転送を有効にしてから読みます。

**Arduino IDE 2 の制限（2026-11-25、[arduino/arduino-ide#2988](https://github.com/arduino/arduino-ide/issues/2988)）:** IDE は、
その口で一度モニターを開くと、monitor の設定を全部 `pluggable-monitor-settings.json` に保存し、次からはそれを Tools メニューより
優先します。そのため、あとから Monitor Source を変えても、IDE を再起動しても、最初の source のまま読みます。変えるときは、IDE を
閉じて、IDE の設定のフォルダー（Windows は `C:\Users\<ユーザー名>\.arduinoIDE`）の `pluggable-monitor-settings.json` から、
その口の項目（`ch32-riscv-ug:ch32rv:<ボード>-<口>-<protocol>`）を消してから、メニューを選び直してモニターを開きます。
arduino-cli の `monitor` は、起動のたびにメニューの値を読むので、この制限はありません。既定の Auto が全部を読むのは、このためです。

`uart` 以外の source を選ぶと debug で attach するので、走っている firmware のクロックが組み直されることがあり、V103 / V205 では
register が 1 つ壊れることがあります（[chip-caveats.ja.md](chip-caveats.ja.md)）。モニターの窓の先頭に
`[ch32rv monitor] attaching may have changed the target clock` が出たら attach した印です（Auto の `wchlink://` は走らせ直すので出ません）。

CLI では次の形です。

```sh
ch32rv probe list
ch32rv monitor --source auto --probe serial:<probe-serial>
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
