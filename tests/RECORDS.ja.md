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

基板の型です。`<作り手>:<型>` の形で書きます。作り手は小文字・数字・`-`、型は英数字と `.` `_` `-` です（例 `wch:CH32X035C8T-R0`、`weact:CH32X035-CoreBoard-V1.0`、`uiapduino:pro-micro-ch32v003-v1.4`）。

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
| `{"to": "host"}` | 試験を流すホストにつながっている | usb、usb-classes、usb-media |
| `{"to": "esp32-s3", "serial": "<USB serial>", "mac": "<12 桁>"}` | 相手の ESP32-S3 につながっている（full speed） | usb-peer の各 case。USB host の case は、USB host を持つ型番だけ |
| `{"to": "esp32-p4", "serial": "<USB serial>", "mac": "<12 桁>"}` | 相手の ESP32-P4 の OTG HS の口につながっている（high speed） | usb-peer のデバイス側の case と、usb-peer-host / usb-peer-host-classes（DUT は USBHS のホスト）。input と storage は P4 のスケッチが無いので飛ばす |
| `{"to": "rp2040", "port": "<口>"}` | 同じ OEP プローブの口 `<口>` にいる RP2040（Pico）につながっている（full speed） | usb-peer の各 case（usb-peer-media を除く。RP2040 のホストは isochronous を持たない） |

`serial` には、ESP32 からこのホストへつながる口の USB serial（USB-UART ブリッジのもの、または USB-Serial/JTAG のもの）を書きます。どのホストでも、これで ESP32 を見つけます。`mac` には ESP32 の MAC を 16 進の小文字 12 桁で書きます（区切りなし）。どちらか一方は必ず書きます。CH340 のように USB serial を持たないブリッジでは `serial` を省き、`mac` だけを書きます。このときは board-identify の名前（`/run/board-identify/by-id/<チップ>-<mac>`）で口を探すので、board-identify が動いているホストでだけ見つかります。

ESP32-P4 は ESP32-S3 と同じスケッチ（`tests/sketch_support/usb_peer/*/peer_s3`）を、sketch.yaml の `p4_host` の設定で動かします。答えの行の頭は、どちらも `S3 ` です。

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

### 4.7 `note`・`updated`

- `note`: 人のためのメモです。試験は読みません。
- `updated`: 最後に書いた日です（`YYYY-MM-DD`）。`bench_record.py set` が書くたびに入れます。

## 5. 読み方

- 名前が無いオブジェクト（`no_resource` で断られたもの）は、空の記録として読みます。
- 壊れたオブジェクト（crc が合わない、oep-if-memo §3）と、JSON のオブジェクトでないものは、空として読み、警告を出します。書き直せば直ります。
- 検査に通らない記録があれば、チップに触る前に止まります。
- メモ欄を持たないプローブと WCH-Link は、記録を持てません。記録の要る case は流れません。
- WCH-Link の記録は、tests/.env の `CH32RV_PROBE_RECORD_<serial>` に、§4 と同じ形の JSON で書けます。`<serial>` はアドレス `wchlink://<serial>` の serial で、その Link にだけ効き、同じ検査を通します（例: `CH32RV_PROBE_RECORD_BC5F4B9807F7='{"parts": {"eth": {"to": "lan"}}}'`。uv の env-file は一重引用符で囲まないと読めません）。OEP の口の記録はプローブにあります。

## 6. 書き方と見方

[bench_record.py](bench_record.py) を使います。`set` は、渡したメンバーだけを変えます。

```sh
# 見る
uv run --project tests --locked --extra hardware python tests/bench_record.py show oep://30eda0e31108/x035s3

# 書く（--target auto はチップを読んで target を入れる）
uv run --project tests --locked --extra hardware python tests/bench_record.py set oep://30eda0e31108/x035s3 \
  --target auto --board weact:CH32X035-CoreBoard-V1.0 --supply usb --usb esp32-s3,serial=58FA041040,mac=e4b063b49f2c

# メンバーを消す
uv run --project tests --locked --extra hardware python tests/bench_record.py set oep://30eda0e31108/x035 --remove pd

# その口で何が流れるか（流さずに見る）
uv run --project tests --locked --extra hardware python tests/bench_record.py plan oep://30eda0e31108/x035
```

## 7. 例

2026-11-05 のプローブ 30eda0e31108 の口 `x035s3` です。

`oep.probe.memo/port/x035s3`:

```json
{"target":"ch32x035-c016abcd283fbc5c"}
```

`io.github.ch32-riscv-ug.arduino/port/x035s3`:

```json
{"board":"weact:CH32X035-CoreBoard-V1.0","supply":{"from":"usb"},"usb":{"to":"esp32-s3","serial":"58FA041040","mac":"e4b063b49f2c"},"note":"powered from the ESP32-S3's USB host port","updated":"2026-11-05"}
```

同じプローブの口 `x035` の、不具合の例です。

```json
{"board":"wch:CH32X035C8T-R0","defects":[{"id":"x035-adc-ch-i2c-unavailable","lose":["i2c","adc:3","adc:7","adc:11","adc:15"],"why":"lot digit 5 from the end is 0 (errata); I2C1 drives SDA high, the ADC channels read the previous one; confirmed 2026-11-03"}],"updated":"2026-11-05"}
```
