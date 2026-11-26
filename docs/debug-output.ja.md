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

モニターが何を読むかは、ch32rv の monitor の設定 **Monitor Source**（id は `source`）で決まります。既定は **Auto** で、
スケッチが使っている経路を全部一緒に読むので、ふつうは選ぶ必要がありません。

**Auto（既定、ch32rv 0.35.0 から）:**

- **読むもの:** UART（LinkE の CDC、OEP の probe の fixture-uart）と、郵便受け系の 1 つ（`SerialDMSeq`・`SerialSDI`・`SerialDMDATA`
  のうちスケッチが使うもの）と、RTT（`SerialRTT` を使っていれば）を、1 本にまとめて出します。どの経路から来た行かは付きません。
  行は経路ごとにまとめて出すので、2 つの経路の行が 1 行に混ざることはありません。
- **送るもの:** 窓から送った文字は、スケッチの出力が最後に来た経路へ送ります。まだ何も来ていなければ UART へ送ります。
  `SerialSDI` は受け取れないので、最後が SDI のときも UART へ送ります。
- **Auto で開いた印:** 開いた直後に、窓の先頭に `[ch32rv monitor] auto: uart; looking for dmseq / sdi / dmdata, rtt` のような行が
  1 行だけ出ます。どの経路が見つかったかは出しません（その経路の出力が窓に出ることが、読めている印です）。
- **`wchlink://`:** モニターを開くたびにスケッチを最初から走らせ直します（接続でクロックが組み直されたり、V103 / V205 で
  レジスタが壊れたりする副作用を、走らせ直しで消すため）。開く前に窓で打った文字は捨てます。スケッチが自分で再起動したら、
  郵便受け系と RTT を選び直します。
- **`oep://`:** スケッチを止めず、走らせ直しもしません。郵便受け系は DMSeq → SDI → DMDATA の順に 3 秒ずつ試して、データが来た
  ものに決めます（決まるまで数秒かかります）。決めた経路が 5 秒黙ったら試し直します。UART は、probe の口に rx / tx のラベルが
  あるときだけ読みます。
- **serial port（LinkE の COM など）:** UART だけを読みます。

| Monitor Source | serial port（LinkE の COM など） | `wchlink://` | `oep://` |
|---|---|---|---|
| `auto`（既定） | UART だけ | CDC の UART・郵便受け系・RTT | fixture-uart・郵便受け系・RTT |
| `uart` | UART bridge の素通し | Link の CDC | ―（`fixture-uart`: probe の UART の線） |
| `dmseq` / `dmdata` / `sdi` / `rtt` | その経路（debug で attach） | 同じ | 同じ |

`SDI` は、WCH-Link でも ch32rv が郵便受けを DMI で直接読むので、UART と一緒に読めて、Link の設定も変えません。
LinkE 自身の SDI の転送（WCH 純正のやり方）は、あまりおすすめしません。ch32rv が Link を開くたびに（書き込みも、Auto の
モニターも）切れ、転送している間は CDC に UART の出力が届かず、IDE では読めないためです（LinkE 2.23 は、ch32rv が最初に送る
probe 情報の問い合わせで転送を止めます）。利点は、WCH-Link で直接読むより約 2 倍速いことだけです（V307 で約 20 KB/s と約
10 KB/s。OEP は直接読んで約 28 KB/s）。それでも使うときは、書き込んだ後に `ch32rv monitor sdi on`（または WCH-LinkUtility・
wlink）で有効にして、Link を開かない `uart` で読みます。戻すときは `ch32rv monitor sdi off` です（[SerialSDI の README](../libraries/SerialSDI/README.ja.md)）。

**選び方:** Auto 以外にしたいときに使います。

- **Arduino IDE 2:** 選べません。IDE 2 の Serial Monitor の窓は、monitor の設定のうちボーレートと改行しか出さないためです。
  また IDE 2 は、その口で一度モニターを開くと、monitor の設定を全部 `pluggable-monitor-settings.json` に保存し、次からはそれを
  使い続けます（2026-11-25、[arduino/arduino-ide#2988](https://github.com/arduino/arduino-ide/issues/2988)）。前の版で一度開いた
  口が Auto にならないときは、IDE を閉じて、IDE の設定のフォルダー（Windows は `C:\Users\<ユーザー名>\.arduinoIDE`）の
  `pluggable-monitor-settings.json` から、その口の項目（`ch32-riscv-ug:ch32rv:<ボード>-<口>-<protocol>`）を消します。
- **arduino-cli:** `arduino-cli monitor -p <口> --config source=dmseq` のように、開くたびに選べます。sketch.yaml の profile では
  `port_config:` の `source:` に書けます。
- **VS Code:** [Arduino CLI Wrapper](https://marketplace.visualstudio.com/items?itemName=tanakamasayuki.vscode-arduino-cli-wrapper)
  は、monitor の設定（Monitor Source・Baud Rate (UART) など）をラベルで出すので、口ごとに選べます（Windows と WSL で確認、
  2026-11-26）。

ツールメニューの Monitor Source は 2026-11-26 に外しました（IDE 2 では上の保存のため、変えても効かなかったため）。

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

WCH-Link と OEP probe を root 以外で開くには、udev のルールが要ります（入れ方は [README](../README.ja.md#インストール)）。
ボードマネージャで入れると `post_install.sh` が手順を出しますが、普通のユーザーで動くので、ルールそのものは書けません。
sketch.yaml の profile で入れた場合は、`post_install.sh` は動きません。権限が無いと、次のようになります。

- **port の一覧:** OEP probe は口の名前が読めないので、`oep://<unit>` が 1 つだけ「no permission: udev rules missing」の
  label で出ます。
- **モニター（Auto）:** debug の経路が開けなくても、UART だけで開いて読み、`[ch32rv monitor] warning: reading the UART only; …`
  の行で原因と手順を出します。
- **書き込みや、debug の経路を選んだモニター:** エラーに、打つ手順（その環境の `ch32rv` の絶対パス入り）がそのまま出ます。

診断は `ch32rv doctor` です（PATH には無いので、`~/.arduino15/packages/ch32-riscv-ug/tools/ch32rv/<版>/ch32rv doctor`）。

ルールは WCH-Link と OEP probe の serial port（tty）も、ログインしている利用者が開けるようにします（`uaccess`）。ほかの
USB-UART などの serial port には、distribution の serial-port group（`dialout` 等）への所属が要ります。

## 経路の選び方

- 通常の user I/O と互換性を優先するなら `Serial`
- UART pin と RAM を使わず一方向の log だけ欲しいなら `SerialSDI`
- 大きめの双方向 buffer が必要なら `SerialRTT`
- RAM を節約し、DMI が安定している環境なら `SerialDMDATA`
- 欠落・重複を検出できる console が必要なら `SerialDMSeq`

probe は一度に一つの process だけが開けます。upload、monitor、WCH utility、別 debugger が同じ probe を保持して
いる場合は、不要な process を終了してから接続します。
