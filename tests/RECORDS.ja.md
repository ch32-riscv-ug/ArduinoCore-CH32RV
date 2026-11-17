# プローブのメモ欄に置く記録

このコアの実機試験が、OEP プローブのメモ欄に口ごとに置く記録の決まりです。名前、中身の形、メンバーごとの形、読み方、書き方をまとめます。

メモ欄の仕組みと、どのホストにも共通の決まりは、oep-spec の [interfaces/oep-if-memo.ja.md](https://github.com/Open-Embedded-Probe/oep-spec/blob/main/interfaces/oep-if-memo.ja.md) にあります（§1〜§3 が規範、§4 が host の使い方）。この文書は、その §4 の上に、このコアの持ち分（`io.github.ch32-riscv-ug.arduino/…`）を決めるものです。ほかのホスト実装が同じ記録を読むときにも、この文書を正とします。

検査の実装は [harness/records.py](harness/records.py)、記録から試験の case を決めるのは [harness/place.py](harness/place.py) の `choose_cases` です。この文書と実装が食い違うときは、この文書に合わせて実装を直します。

## 1. 何を置くか

置くのは、**基板ごとに違い、機器から読めない事実**だけです。どの基板の型が口につながっているか、プローブの線のほかに何が配線されているか、DUT の USB がどこへ行っているか、その基板だけの不具合です。

置かないもの:

- **プローブが言えること**: 線とその先の端子（プローブのラベル `<口>.<役割>`、`<口>@<端子>`）。
- **チップが言えること**: 型番、UID、チップ ID（`ch32rv target info`）。ただし、チップ ID で型番が 1 つに決まらないときの `part`（§3）は置きます。
- **型や系列で同じこと**: 型ごとの基板上の部品、系列ごとの周辺は、試験の側（コードと表）が持ちます。
- **守らせる必要のある値**: プローブは中身を読まないので、「この端子は駆動しない」のような安全の値はプローブの設定に置きます（oep-if-memo §4.2）。

記録は写しです。正しい中身はホストの側にも残してください。

## 2. 名前と中身の書き方

1 つの口につき、2 つの記録を置きます。

| 名前 | 持ち主 | 決まりの場所 |
|---|---|---|
| `oep.probe.memo/port/<口>` | OEP（どのホストも共通） | oep-if-memo §4.3。このコアでの使い方は §3 |
| `io.github.ch32-riscv-ug.arduino/port/<口>` | このコア | この文書の §4 |

- **口の名前**は、プローブのラベルの `<口>` を小文字にしたものです（`X035` → `x035`）。名前の全体は 64 バイト以内です（oep-if-memo §4.1）。口の名前は 16 文字までなので、収まります。
- **版の番号は付けません。** メンバーを足すことは、いつでもできます。読み手は知らないメンバーを読み飛ばします。メンバーの意味や形を変えるときは、名前を変えます（`…/port2/<口>` のように）。同じ名前のまま意味を変えてはいけません。
- **中身**は UTF-8 の JSON で、いちばん外はオブジェクトです。空白なしの詰めた形で書きます（`{"board":"wch:CH32X035C8T-R0"}`）。大きさは、プローブの `max_object` 以内です。いまのプローブは 2048 バイトです。
- **書き直すときは**、プローブのロックを取ったセッションの中で読んで書きます。こうすると、その間にほかのホストが書くことはありません。渡されたメンバーだけを変え、知らないメンバー（ほかの道具のもの）は残します。中身が空になったら、オブジェクトを消します。
- **日付**は `YYYY-MM-DD` です。

## 3. `oep.probe.memo/port/<口>`（共通の記録）

形は oep-if-memo §4.3 のとおりです。このコアは次の 3 つを使います。

| メンバー | 形 | このコアでの使い方 |
|---|---|---|
| `target` | `<系列>-<UID の 16 進>`（小文字。例 `ch32x035-1ff9abcd880ebc48`） | 口につながっているはずの個体です。試験の前にチップを読んで照合し、違えば何も書き込まずに止まります。アドレスの 3 段目（`oep://<unit>/<口>/<target>`）と同じ形です。`<系列>` は `ch32rv target info` の `family` を小文字にしたもので、型番ではありません（V203 / V208 は `ch32v20x`） |
| `part` | メーカーの書き方の型番（例 `CH32V103C8T6`） | チップ ID で型番が 1 つに決まらないときだけ書きます。`ch32rv target info` の候補の 1 つでなければ止まります |
| `note` | 文字列 | 人のためのメモです。試験は読みません |

`target` が無ければ照合はせずに進み、その旨を警告します。

## 4. `io.github.ch32-riscv-ug.arduino/port/<口>`（このコアの記録）

どのメンバーも省けます。無いメンバーは「その事実が無い」ではなく「書かれていない」と読み、それを要る試験は理由を付けて飛ばします。端子は DUT の端子名を小文字で書きます（`pa5`）。

### 4.1 `board`

基板の型です。`<作り手>:<型>` の形で書きます。作り手は小文字・数字・`-`、型は英数字と `.` `_` `-` `&` です（例 `wch:CH32X035C8T-R0`、`weact:CH32X035-CoreBoard-V1.0`、`uiapduino:pro-micro-ch32v003-v1.4`、`wch:CH32F&V208W-R0`）。型は作り手の書き方のまま書きます（WCH の評価ボードの `&` も外さない。資料や基板の印刷と同じ文字で探せるように）。ベンチの名前（口の名前や profile 名）には型を使わないので、`&` がシェルや環境変数の名前に出ることはありません。

型ごとの基板上の部品は、試験の側が持ちます。今は `uiapduino:pro-micro-ch32v003-v1.4` だけが case を変えます（hid）。

### 4.2 `supply`

基板の電源がどこから来るかです。`{"from": "probe" | "usb" | "other"}` の形で書きます。

- `probe`: プローブから給電されています。プローブの USB の口を切れば、基板の電源も落ちます。プローブの GPIO から給電する形も含みます。power の case はこのときだけ流します。
- `usb`: 基板自身の USB から給電されています。
- `other`: それ以外です（外部電源など）。

### 4.3 `usb`

DUT 自身の USB 端子がどこへつながっているかです。

| 形 | 意味 | 流す case |
|---|---|---|
| `{"to": "host"}` | 試験を流すホストにつながっている | usb、usb-classes、usb-media、usb-ss-iso、usb-firmware |
| `{"to": "host", "ss_only": true}` | 同じく、ただしケーブルが SuperSpeed の線だけ（USB 2.0 の線を切ってある・無い） | 同じ。usb の ISP への切り替え（dfu-detach、1200 bps）は飛ばす（ISP のブートローダーは USB 2.0 なので見えない） |
| `{"to": "esp32-s3", "serial": "<USB serial>", "mac": "<12 桁>"}` | 相手の ESP32-S3 につながっている（full speed） | usb-peer の各 case。USB host の case は、USB host を持つ型番だけ |
| `{"to": "esp32-p4", "serial": "<USB serial>", "mac": "<12 桁>"}` | 相手の ESP32-P4 の OTG HS の口につながっている（high speed） | usb-peer のデバイス側の case と、usb-peer-host / usb-peer-host-classes（DUT は USBHS のホスト）。input と storage は P4 のスケッチが無いので飛ばす |
| `{"to": "rp2040", "port": "<口>"}` | 同じ OEP プローブの口 `<口>` にいる RP2040（Pico）につながっている（full speed）。別のプローブの Pico は `"port": "oep://<unit_id>/<口>"`（WCH-Link の先の DUT など） | usb-peer の各 case（usb-peer-media を除く。RP2040 のホストは isochronous を持たない） |
| `{"to": "ch32rv", "port": "<アドレス>", "controller": "<口>"}` | 別の CH32RV の基板の `controller` の口につながっている。`port` はその基板のアドレス（`oep://<unit_id>/<口>[/<target>]` か `wchlink://<serial>`）、`controller` は下の「口ごとの形」のキーと同じ名前 | usb-peer の各 case。デバイスの case では相手がホスト、ホストの case では相手がデバイス。相手のライブラリにまだ無いものは飛ばす（下） |

`serial` には、ESP32 からこのホストへつながる口の USB serial（USB-UART ブリッジのもの、または USB-Serial/JTAG のもの）を書きます。どのホストでも、これで ESP32 を見つけます。`mac` には ESP32 の MAC を 16 進の小文字 12 桁で書きます（区切りなし）。どちらか一方は必ず書きます。CH340 のように USB serial を持たないブリッジでは `serial` を省き、`mac` だけを書きます。このときは board-identify の名前（`/run/board-identify/by-id/<チップ>-<mac>`）で口を探すので、board-identify が動いているホストでだけ見つかります。

ESP32-P4 は ESP32-S3 と同じスケッチ（`tests/sketch_support/usb_peer/*/peer_s3`）を、sketch.yaml の `p4_host` の設定で動かします。答えの行の頭は、どちらも `S3 ` です。

**CH32RV の相手。** 相手の基板は、DUT と同じように自分のアドレスで見つけます。run_hardware.py は `port` の基板の石を
ch32rv で読み（石の ID で型番が決まらないときは、その口の記録の `part`）、その口の記録の `board` と `options` を使って、
相手のスケッチ（`tests/sketch_support/usb_peer/*/peer_ch32rv`）をその基板の fqbn でビルドし、arduino-cli でそのアドレスへ
書き、SerialDMSeq で話します（`TEST_SERIAL_PORT_PEER_CH32RV`）。相手の口の記録の `target` が石と違えば止めます。
`controller` は相手のスケッチの build_opt.h に入ります（デバイスの case は `PEER_USB_HOST_PORT`、ホストの case は
`PEER_USB_CONTROLLER`）。デバイスの case では相手がホストなので、`usbd` やホストの無い型番（X035F8U6 など）では飛ばします。
DUT 側の口がキーで決まっているとき、試験はつながる速さを遅い方の口から決めます（SS どうしなら 3、片方が FS なら 1）。
相手の基板の記録の `usb` は、逆向きに DUT を指してかまいません（たどりません）。

**間のハブ。** 相手（esp32-s3 / esp32-p4 / rp2040 / ch32rv）の形に `"hub": "fs" | "hs-stt" | "hs-mtt"` を足すと、DUT と
相手の間にその種類のハブがあることを表します（`fs` は FS のハブ、`hs-stt` / `hs-mtt` は HS のハブでトランザクション
トランスレーターが 1 つか口ごとか）。ホストの case は、相手がそのハブの先にいること（HS のハブなら FS の相手は split で
届く）を DUT の `where` で確かめます。書かないときは、相手はルートの口に直結です。`{"to": "host"}` には書けません。

今の CH32RV のライブラリで相手になれない case は、理由を付けて飛ばします（place.py の `CH32RV_PEER_LACKS`）:
usb-peer-media（CH32RVUsbHost に isochronous が無い）、usb-peer-net（CH32RVUsbHost に CDC-NCM が無い）。

```json
{"usb": {"super_speed": {"to": "ch32rv", "port": "oep://30eda0e31108/h417-c", "controller": "super_speed"},
         "full_speed": {"to": "host"}}}
```

**口ごとの形。** USB のコントローラーを複数持つ石で、口ごとに相手が違うとき（H417 の公式 EVT: FS に Pico、SS に PC）は、
コントローラーの名前をキーにして、それぞれに上の表の形を書きます。

```json
{"usb": {"full_speed": {"to": "rp2040", "port": "rp2040-h417"}, "super_speed": {"to": "host", "ss_only": true}}}
```

- キーは、コアで口を選ぶときの名前（`CH32RVUsbController` の `FullSpeed` など、`CH32RV_USB_HOST_PORT_FULL_SPEED` など）を
  小文字にしたものです: `full_speed`（USBFS / OTG_FS）、`high_speed`（USBHS）、`super_speed`（USBSS）、`usbd`（V20x の USBD。
  デバイスのドライバはまだ無い）、`high_speed_2`（V407 / V467 の 2 つ目の USBHS）。V20x の 2 つの Full Speed は
  `full_speed` と `usbd` で分けます。
- キーは「どのコントローラーか」だけを表します。そこで流せる case（デバイスだけか、ホストもか）は、試験が系列のデータと
  実装の状況から決めます（place.py の choose_cases）。X315 の `super_speed` はデバイスだけ、H417 の `super_speed` はハードでは
  両対応でも今の実装はデバイスだけ、のように、同じキーでも石と実装で範囲が変わります。
- usb・usb-classes・usb-media・usb-ss-iso・usb-firmware は `{"to": "host"}` の口で、usb-peer の各 case はプローブなどの相手（esp32-s3 / esp32-p4 /
  rp2040 / ch32rv）の口で流れます。run_hardware.py が、その口をスケッチに渡します（デバイスは `USB_TEST_CONTROLLER`、ホストは
  `USB_PEER_HOST_PORT`）。1 つの形は、系列の既定のコントローラー（`Auto`）のままです。
- 書き方: `tests/bench_record.py set <アドレス> --usb rp2040,port=rp2040-h417 --usb-port full_speed`（CH32RV の相手は
  `--usb ch32rv,port=oep://<unit_id>/<口>,controller=super_speed --usb-port super_speed`）。`--usb-port` の口だけを書き換え、
  ほかの口の記録は残ります（1 つの形の記録は、口ごとの形に置き換わります）。

### 4.4 `parts`

基板の外に足した部品です。`{"<部品>": {...}}` の形で書きます。知らない部品は読み飛ばします。

**`sd`**: SD カードの差し込み口です。`bus` で、どのつなぎ方かを決めます。

| `bus` | 要るメンバー |
|---|---|
| `spi` | `sck`、`miso`、`mosi`、`cs` |
| `sd1` | `clk`、`cmd`、`dat0` |
| `sd4` | `clk`、`cmd`、`dat0`、`dat1`、`dat2`、`dat3` |

例: `{"bus": "sd1", "clk": "pa5", "cmd": "pa7", "dat0": "pa6"}`。`bus`・`to`・`kind`・`note` 以外のメンバーは、どれも端子として検査します。

**`eth`**: 基板の Ethernet の口が、このホストと同じ LAN（DHCP あり）につながっています。`{"to": "lan"}` の形で書きます。eth の case は、これがあって、系列が Ethernet を持つときだけ流します。

### 4.5 `pd`

基板の USB-C に USB PD のシンクがあり、充電器がつながっています。`{"max_mv": <整数>, "vbus_pin": "<端子>", "vbus_ratio": <数>}` の形で書きます。

- `max_mv`: 基板が受けられる最高の電圧です（mV、5000〜48000）。省くと 5000 です。
- `vbus_pin` と `vbus_ratio`: VBUS を分圧して測る端子と、その分圧の比です。2 つはそろえて書きます。あれば、契約した電圧を測って確かめます。

### 4.6 `defects`

その基板（その個体）だけの不具合です。`[{"id": "<名前>", "lose": ["<機能>", ...], "why": "<理由>"}]` の形で書きます。

- `id`: 不具合の名前です。小文字と `-` で、ほかと重ならないものにします（例 `x035-adc-ch-i2c-unavailable`）。
- `lose`: 使えない機能です。今の名前と、飛ばす case は次のとおりです。

  | 機能 | 飛ばす case |
  |---|---|
  | `i2c` | wire |
  | `spi` | spi |
  | `uart` | uart |
  | `usb` | usb と usb-peer の全部 |
  | `sd` | sd |
  | `pd` | pd |
  | `eth` | eth |

  ほかの名前も書けます。今は飛ばす case が無いだけで、細かいものは `<機能>:<番号>` の形で書きます（例 `adc:3` は ADC のチャネル 3）。
- `why`: 何を見てそう判断したかです。日付も書きます。

### 4.7 `options`

その台で試験を流すときに要る、ボードメニューの値です。`{"<メニュー>": "<値>", ...}` の形で、名前と値は boards.txt の
`<board>.menu.<メニュー>.<値>` のものを書きます（例 `{"vio18": "3v3", "debugspeed": "low"}`）。

- 試験は、これを fqbn に入れてからビルド・書き込みします。`CH32_TEST_OPTIONS` で同じメニューを指定したときは、そちらが勝ちます。
- boards.txt に無い値は、チップに触る前に止まります。
- 例: H417 の公式 EVT（6-2）は、VIO18 の端子をプローブで駆動するので `vio18: 3v3` が要ります。PB8 / PB9（SWD）が USB3-A の
  USB 2.0 の線と同じ端子なので、SS の列挙を乱さないように `debugspeed: low` も要ります。
- `bench_record.py set <アドレス> --option vio18=3v3 --option debugspeed=low` で書けます。

### 4.8 `link_uart`

WCH-Link の口だけの欄です。Link の UART の橋渡しが DUT のどの端子につながっているかを書きます。
`{"tx": "<端子>", "rx": "<端子>"}` の形で、`tx` は DUT の TX（Link が受ける）、`rx` は DUT の RX（Link が送る）です。

- つながっていない向きは書きません。どちらもつながっていないときは `{}` です。
- 試験（uart）は、両方あれば往復を、`tx` だけなら DUT が送る向きだけを確かめます。`{}` なら skip します。端子が Serial の
  既定と違えば、その端子で Serial を開きます。
- この欄が無い口は、今までどおり Serial の既定の端子で往復を確かめます。
- WCH-Link の口の記録は tests/.env の `OEP_PORT_MEMO_<シリアル>` に書きます（bench_record.py は OEP の口だけ）。
- 例: nanoCH32H417 v1.0（内蔵の LinkE。回路図で、Link の UART3 が SB3 / SB4 を通って PA10 / PA9）は
  `{"tx": "pa9", "rx": "pa10"}`。M030 のモーター基板は TX だけ。

### 4.9 `spi`

OEP の口の欄です。spi の試験が SPI の CS に使うラベル付きの線を `{"cs": "<端子>"}` で書きます。

- ハーネスの中で SPI の線（SCK・MOSI・MISO）の隣を通る線を CS にすると、その縁を拾って CS に短い High が入り、プローブの
  SPI の相手がフレームの終わりと見て数え直します（6-2、2026-11-14: PD7 の隣の PD6）。プローブのチャンネルの番号からは
  ハーネスの並びが分からないので、離れている線を記録で決めます。
- この欄が無い口は、試験が自分で選びます（DUT の端子として離れた線、次にプローブのチャンネルの離れた線）。
- 書き方: `tests/bench_record.py set <アドレス> --spi-cs pa0`。

### 4.10 `note`・`updated`

- `note`: 人のためのメモです。試験は読みません。
- `updated`: 最後に書いた日です（`YYYY-MM-DD`）。`bench_record.py set` が書くたびに入れます。

## 5. 読み方

- 名前が無いオブジェクト（`no_resource` で断られたもの）は、空の記録として読みます。
- 壊れたオブジェクト（crc が合わない、oep-if-memo §3）と、JSON のオブジェクトでないものは、空として読み、警告を出します。書き直せば直ります。
- 検査に通らない記録があれば、チップに触る前に止まります。
- メモ欄を持たないプローブと WCH-Link は、記録を持てません。記録の要る case は流れません。
- WCH-Link の記録は、tests/.env の `OEP_PORT_MEMO_<serial>` に、§4 と同じ形の JSON で書けます。`<serial>` はアドレス `wchlink://<serial>` の serial で、その Link にだけ効き、同じ検査を通します（例: `OEP_PORT_MEMO_BC5F4B9807F7='{"parts": {"eth": {"to": "lan"}}}'`。uv の env-file は一重引用符で囲まないと読めません）。名前は oep-client の pytest プラグインのもので、WCH-Link のロックの名前 `probe:<serial>` から決まります。
- OEP の口の記録はプローブにあります。tests/hw の試験では、oep-client のプラグインが読み、tests/.env の `OEP_PORT_MEMO_<unit>_<口>`（大文字、`-` は `_`）があればそちらを丸ごと使います（両方あって違えば警告）。中身の検査はどちらもコアです。

## 6. 書き方と見方

[bench_record.py](bench_record.py) を使います。`set` は、渡したメンバーだけを変えます。

```sh
# 見る
uv run --project tests --locked --extra hardware python tests/bench_record.py show oep://30eda0e31108/x035f8u6-s3

# 書く（--target auto はチップを読んで target を入れる）
uv run --project tests --locked --extra hardware python tests/bench_record.py set oep://30eda0e31108/x035f8u6-s3 \
  --target auto --board weact:CH32X035-CoreBoard-V1.0 --supply usb --usb esp32-s3,serial=58FA041040,mac=e4b063b49f2c

# メンバーを消す
uv run --project tests --locked --extra hardware python tests/bench_record.py set oep://30eda0e31108/x035c8t6-s3 --remove pd

# その口で何が流れるか（流さずに見る）
uv run --project tests --locked --extra hardware python tests/bench_record.py plan oep://30eda0e31108/x035c8t6-s3
```

## 7. 例

2026-11-05 のプローブ 30eda0e31108 の口 `x035s3`（2026-11-13 から `x035f8u6-s3`）です。

`oep.probe.memo/port/x035s3`:

```json
{"target":"ch32x035-c016abcd283fbc5c"}
```

`io.github.ch32-riscv-ug.arduino/port/x035s3`:

```json
{"board":"weact:CH32X035-CoreBoard-V1.0","supply":{"from":"usb"},"usb":{"to":"esp32-s3","serial":"58FA041040","mac":"e4b063b49f2c"},"note":"powered from the ESP32-S3's USB host port","updated":"2026-11-05"}
```

同じプローブの口 `x035`（2026-11-13 から `x035c8t6-s3`）の、不具合の例です。

```json
{"board":"wch:CH32X035C8T-R0","defects":[{"id":"x035-adc-ch-i2c-unavailable","lose":["i2c","adc:3","adc:7","adc:11","adc:15"],"why":"lot digit 5 from the end is 0 (errata); I2C1 drives SDA high, the ADC channels read the previous one; confirmed 2026-11-03"}],"updated":"2026-11-05"}
```
