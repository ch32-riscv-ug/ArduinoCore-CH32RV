# OEP を含む開発ワークフロー（統括）

文書基準日: 2026-09-29（同日に §4 以降を深掘りし、起動モードをやめた形に改めた）。状態: **提案**（決定済みの項目は明記する）。ch32rv の OEP 対応（§3）は、全体が固まってから検討する。

書き込み・モニタ・試験・波形の解析に関わる道具（ch32rv、OEP の probe と client、pytest、WireSkein）が増えたので、
「どの場面で、どの道具が、何を受け持つか」をこのリポジトリで 1 か所にまとめる。個々の仕様は各リポジトリの文書が正で、
ここはその間の分担と、まだ決めていないことを並べる。

## 1. 登場するもの

| もの | リポジトリ | 受け持つこと |
|---|---|---|
| ArduinoCore-CH32 | [ch32-riscv-ug/ArduinoCore-CH32](https://github.com/ch32-riscv-ug/ArduinoCore-CH32)（このリポジトリ） | core、ボード定義、試験（`tests/`）。この文書 |
| ch32rv | [ch32-riscv-ug/ch32rv](https://github.com/ch32-riscv-ug/ch32rv) | 同梱の書き込みツール。WCH-LinkE / WCH-Link での書き込み・消去・reset・gdb・monitor |
| OEP の仕様 | [Open-Embedded-Probe/oep-spec](https://github.com/Open-Embedded-Probe/oep-spec) | probe と host の間のプロトコル（core と標準インターフェース） |
| OEP の probe | [Open-Embedded-Probe/oep-probe-arduino](https://github.com/Open-Embedded-Probe/oep-probe-arduino) | ESP32-P4、classic ESP32、RP2350 の probe の実装 |
| OEP の client | [Open-Embedded-Probe/oep-client-python](https://github.com/Open-Embedded-Probe/oep-client-python) | Python の host 側。CH32 の書き込み手順（`ch32_flash`）も今はここ |
| WireSkein | [Open-Embedded-Probe/wireskein](https://github.com/Open-Embedded-Probe/wireskein) | ロジックの記録の解析。試験の期待との照合（`ws verify`） |
| wch-protocols | [ch32-riscv-ug/wch-protocols](https://github.com/ch32-riscv-ug/wch-protocols) | 実測の台帳（読むだけ） |

以下、リポジトリは名前（ch32rv、oep-spec など）で呼ぶ。

## 2. 使う場面

| 場面 | 書き込み | 実行中の出力を見る | 波形 | OEP のセッションを握るもの |
|---|---|---|---|---|
| U1 利用者、WCH-LinkE | ch32rv | LinkE の CDC（UART / SDI）、ch32rv の monitor（dmseq など） | — | —（LinkE） |
| U2 利用者、OEP の probe（USB の P4 など） | OEP の書き込みツール（vendor / HID / USJ） | probe の CDC の口（bind でコンソールを流す、§4.3） | — | 書き込みの間だけ書き込みツール |
| U3 利用者、シリアルしかない OEP の probe（classic ESP32） | 同じポートで OEP の書き込みツール | 同じポート（§4.3） | — | 書き込みの間だけ書き込みツール |
| U4 開発、ベンチの試験（LinkE） | ch32rv | ch32rv の monitor | — | —（LinkE） |
| U5 開発、ベンチの試験（OEP） | 書き込みツール（§3） | pytest が OEP の target.console で | pytest が fixture.capture で。照合は WireSkein | 試験の間ずっと pytest |
| U6 デバッグ | ch32rv の gdb server（LinkE） | — | — | —（OEP は未定） |

## 3. ch32rv と OEP の分担

### 決定済み

- 同梱の書き込みツールは ch32rv に一本化する（2026-09-01）。
- WCH-LinkE での書き込みとデバッグは ch32rv が受け持つ（2026-09-29）。
- OEP のセッションとロックは probe に 1 つ。同時に操作できる host は 1 つ（oep-core §3.3、§5〜§6）。
- target の知識（CH32 の flash の手順など）は host が持ち、probe は持たない（OEP v1 の前提）。

### 提案

- **ch32rv の OEP での仕事は、LinkE と同じく書き込み・デバッグ・人が使うモニタまで**にする。キャプチャや fixture の仲介は
  持たせない。
- **試験の間は pytest がセッションを握る**（U5）。コンソールもキャプチャも同じセッションで扱う。試験中のモニタを ch32rv に
  任せないので、キャプチャを ch32rv に通す必要も出ない。
- 書き込みは、段ごとにセッションを持ち替える: 書き込みツールが `open` → attach → 書き込み → reset → `end` で閉じ、pytest が
  次に `open` する。attach は probe に残るので、pytest はその接続をそのまま使える（oep-if-debug: 付いている線への attach は
  その接続を返す）。
- **当面は書き込みツールを選べるようにする**（`--writer ch32rv|oep`）。ch32rv に OEP の対応が入るまでは、client の
  `ch32_flash` で書く。入ったら pytest の書き込みも ch32rv に寄せ、CH32 の書き込み手順を 1 か所（ch32rv）にまとめる。
  IDE から OEP の probe で書き込むにも、どのみち ch32rv の対応が要る。
- pytest と ch32rv は、同じ probe のロック（ch32rv の `DeviceLock`: `$XDG_RUNTIME_DIR/ch32rv/<key>.lock` の flock）を共有する。
  [harness-requirements](harness-requirements.ja.md) の要求 c と同じ。

## 4. 口の原則: シリアルの口は常に OEP を受け、それ以外はコンソール（提案）

### 4.0 原則（起動モードは持たない）

1. **シリアルに見える口（USB CDC、USB-Serial/JTAG、USB-UART の変換チップ越しの UART）は、いつでも OEP のフレームを受ける。**
   フレーム以外のバイトは、その口に bind したコンソール（dmseq / SDI / fixture.uart）へ流す。bind が無ければ捨てる。
2. **ほかの経路（vendor bulk、HID）を持つ probe では、host はそちらで OEP を話す**（oep-core §3.3 の順: vendor bulk、HID、CDC）。
   すると CDC には実際にはコンソールしか流れない。ただし CDC が OEP を受けなくなるわけではなく、Web Serial やほかの経路に権限が
   無い環境からは、CDC で話せる。
3. **どの経路から来た要求も、セッションとロックは 1 つ**（oep-core §3.3、確認済み）。

これで「setup / debugger」のような起動モードは要らない。設定は同じ口で行い、bind はすぐ効く（再起動も要らない）。設定を
変え直すのも同じ口。OS に権限の無い経路に OEP を置いて入れなくなることも無い（v1-open-proposals §7 の復旧の問いの大部分が消える）。
起動モードの仕組み自体は残してよいが（ロジアナ用に CDC を持たない構成にする、など。X1）、この用途には使わない。

### 4.1 CDC の経路の形を、効率からシリアルとの共用へ変える

今の CDC / USB-Serial/JTAG のフレームは `length(u16) message`（CRC なし。oep-core §3.1）で、生のバイトとは混ぜられない。
**シリアルに見える口はすべて UART と同じ形（COBS + CRC-16/CCITT-FALSE、0x00 区切り）にする。** `length` の形は vendor bulk と HID
だけに残す。

| 経路 | 今 | 変更後 |
|---|---|---|
| vendor bulk、HID | `length(u16) message` | そのまま |
| USB CDC、USB-Serial/JTAG | `length(u16) message` | COBS + CRC-16、0x00 区切り |
| UART（変換チップ越し） | COBS + CRC-16、0x00 区切り | そのまま |
| TCP | `length(u16) message` | そのまま（共用しない） |

- 代償: COBS の膨らみは 254 byte につき 1 byte、CRC の計算が加わる。CDC の制御の速さ（X6: 往復 0.6 ms、host → probe 0.23 MB/s）
  はもともと制御にしか使わないので、差は出ない。大きなデータ（キャプチャのストリーミング）は vendor bulk で、変わらない。
- 利点: client の COBS の読み手が 1 つで済む（今は経路で `length` と COBS を選んでいる: `link.py` の `framing_for`）。「シリアルの
  ポートを名前で開くなら COBS」と決められる。

### 4.2 共用の規則（シリアルに見える口すべてに当てる）

1. probe は入力を常に見張る。0x00 が来たらフレームの候補として次の 0x00 までためる。COBS を解いて CRC-16 が合えば OEP の
   フレーム。合わなければ、ためたバイトをそのままコンソールへ送る。途中で 200 ms 途切れたら（oep-core §3.2 の読み直しの規則）、
   同じくコンソールへ送る。0x00 以外のバイトはためずにすぐコンソールへ送る。
2. probe → PC: コンソールの出力は生のまま。OEP の応答と push は `0x00 <COBS> 0x00` と前後に区切りを付けて送る。**その口への送信は
   1 つのキューを通す**（下の「送信のキュー」）。直接書くと、OEP のフレームの途中にコンソールの文字が挟まる。
3. **ロックを持つセッションの要求が来ている口では、生の転送を両方向とも止める。** コンソールの出力は probe の中の位置付きの
   ストリームに残り、`target.console` の read で読める。PC から来たフレームの外のバイトは捨てる。セッションがほかの経路
   （vendor / HID / 別の CDC）にあるときは、その口の生の転送は止めない（モニタは試験中も流れ続ける）。
4. セッションが終わったら（`end`、または lease の期限切れ）、止めていた口の生の転送を再開する。どこから流すかは §6 の 2。

**送信のキュー（probe 側）**

- 共用する口ごとに送信のキューを 1 つ持ち、書き手はキューに**単位**を入れるだけにする。単位は「OEP のフレーム 1 つ（前後の
  区切りを含めて丸ごと）」か「コンソールの塊（上限 64 byte 程度）」。書き出すのは 1 つのタスクだけで、単位を割らずに順に出す。
- OEP のフレームはコンソールの塊より先に出してよい（host の応答が、よくしゃべる target の出力で遅れないように）。コンソールの
  塊どうしの順は保つ。塊が小さいので、OEP の応答が待つのは最長で塊 1 つ分。
- コンソールの転送は、位置付きのストリームから読んでキューに入れる。キューに空きが無ければ読まずに待てばよく、捨てる必要は
  無い（ストリームが持っている）。OEP の送信は今の Endpoint の送信バッファと同じ扱い。
- 今の実装への注意: Endpoint は `Serial` に直接書く（Esp32V003Probe、Esp32P4X035Probe）。ESP32 の `HardwareSerial::write` は
  呼び出しごとに排他するだけなので、フレームを複数回の write で出すと、別のタスクの write が間に入る。フレームは 1 回の write
  で出すか、キューを通す。受信側は 1 本のバイト列なので、この問題は無い（規則 1 の見分けだけ）。

**client 側に要る変更**: 今の COBS の読み手（`link.py` の `_recv`）は、0x00 までのバイトをそのままフレームとして解き、失敗すると
「壊れた応答」として送り直す。共用の口では、セッションを開く前のコンソールの文字が必ず先に来るので、毎回それが起きる。
**フレームの外のバイト（0x00 の前の解けないまとまり）は雑音として捨て、応答の欠落は時間切れだけで判断する**形に変える。本当に
壊れた応答（変換チップの取りこぼし）も雑音に見えるが、時間切れで送り直すので結果は同じ（遅くなるだけ）。

**成り立つ理由と限界**

- テキストのコンソールには 0x00 が出てこないので、フレームの始まりと取り違えない。0x00 を含むバイナリをモニタから送ると、次の
  0x00 か 200 ms までためるので、その分だけ遅れて届く（捨てはしない）。モニタを使う人は OEP のバイナリを流さない前提。
- 変換チップ越しの生のコンソールは、取りこぼしから守られない（CP2102 は長い連続の送信でバイトを落とす。v003 の治具で実測）。
  OEP のフレームは CRC と送り直しで守られる。
- Linux の tty は 2 つのプロセスが同時に開ける。モニタと書き込みツールが同時に開くと PC → probe のバイトが混ざる。書き込み
  ツールは `exclusive=True` で開く（今の client はそう）。IDE 2 は書き込みの間モニタを切る（はず。未確認）。

### 4.3 流れ

**P4（USJ で OEP、HS の CDC にコンソールを bind）、Arduino IDE（U2）**

1. 設定: Web Serial で USJ を開き、target、bind（HS の CDC 0 に dmseq）、`save`。すぐ効く。再起動は要らない。
2. 書き込み: 書き込みツールが USJ で `open` → attach → 書き込み → reset → `end`。HS の CDC は別の口なので、コンソールは流れ続ける
   （bind の connection は host の detach や reset を越えて続く。P6 で確認）。
3. モニタ: HS の CDC を開く。reset 直後の最初の出力から見える（P6）。

**P4、pytest（U5）**: USJ で 1 つのセッションを握り、コンソールは `target.console`、キャプチャは `fixture.capture` で読む。
HS の CDC のモニタは同時に流れ続ける（位置付きのストリームは読み手が増えても取り合わない）。今の試験はこの形。

**classic ESP32（UART 1 本の共用）、Arduino IDE（U3）**

1. 設定: Web Serial で同じポートを開き、target、bind（port 0 に dmseq）、`save`。以後、同じポートにコンソールが流れる。
2. 書き込み: 書き込みツールがポートを開く（DTR / RTS on）。流れてくるコンソールの文字は雑音として捨てる。`open` → attach →
   書き込み → reset → `end`。この間、生の転送は止まっている。
3. モニタ: 同じポートを 115200 で開く。セッションは終わっているので、コンソールが流れる。打った文字は target へ届く。

**classic ESP32、pytest（U5）**: 試験の間ずっとセッションを握るので、ポートに流れるのは OEP のフレームだけ。混ざらない仕組みは
規則 3 そのもので、pytest 側に特別な分離は要らない。試験中にモニタを開くと、フレームのバイナリが見えるだけ。

### 4.4 probe の再起動の抑止

モニタや書き込みツールがポートを開くときの DTR / RTS で probe が再起動すると、RVSWD の同期が落ち、target まで再起動しうる
（session-and-exclusivity の前提）。probe は自分で止められるものは止める。

| 口 | 再起動の仕組み | 止め方 |
|---|---|---|
| P4 の USB-Serial/JTAG | 周辺回路が DTR / RTS の並びで chip を reset する（ハード） | `USB_DEVICE_CHIP_RST_REG` の `USB_UART_CHIP_RST_DIS`（bit2）を立てる。arduino-esp32 3.3.12 の HWCDC に API は無い（レジスタを直接書く） |
| TinyUSB の CDC（arduino-esp32 `USBCDC`） | DTR / RTS の並びで download モードへ、1200 baud の touch で reset（ソフト） | `USBSerial.enableReboot(false)` |
| EspUsbDevice の CDC（P4 の HS の口） | 持たない | 不要 |
| classic ESP32 の変換チップ越しの UART0 | 開発ボードの回路（EN と IO0 をトランジスタで駆動） | firmware では止められない。DTR と RTS を両方 on で開けば再起動しない（実測、2026-09-29）。`arduino-cli monitor` の既定は両方 on |

### 4.5 リンクの速さ（変換チップ越しの UART だけの問題）

- USB CDC と USB-Serial/JTAG では、baud は数字が渡るだけで、バイトは USB の速さで流れる。問題にならない。
- 変換チップ越しの UART では、PC 側で選んだ baud は ESP32 に伝わらない。ESP32 の UART と合っていなければ何も通らない。
- **115200 に固定する。** 理由: (a) 設定で変えられるようにすると、忘れたときに入れなくなる（復旧の問いが戻る）。(b) ESP32 の
  UART にはハードの自動 baud 検出があり ROM のブートローダーが使っているが、コンソールの生のバイトと混ざる口でそれに頼るのは
  危うく、未検証。(c) 115200 = 約 11 KB/s で、dmseq（DMI の polling で決まる）と書き込み（V003 16 KB を 2.9 s、oep-v1-core-freeze
  の実測）には足りる。
- 足りない場面は、fixture.uart を高い baud で素通しするときだけ。それは変換チップ越しの probe の用途に入れない（P4 でやる）。

## 4.5 フローを最初から（提案。2026-09-29 に見直し）

### LinkE

1. ピンは固定。target をつなぐ。**事前の設定はこれで終わり。**
2. IDE でポートを選ぶ（`wchlink://<serial>` か LinkE の CDC）。書き込みとモニタは §4.6。

### OEP の probe

**1. 設定のページを開く。** ブラウザで、probe に合う経路を選ぶ: CDC なら Web Serial、HID なら WebHID、vendor bulk なら WebUSB。
どれも同じ OEP（describe、session、probe.config）を話す。シリアルの口は常に OEP を受けるので（§4.0）、設定はいつでも
同じ口でできる。

**2. target を探して、線を決める。** `scan`（oep-if-debug §1）: probe が許すピンの組を順に試し、見つかった組を返す。probe は
組の並びを describe で宣言している（固定の組は channel_group、どのピンでもよい probe はその範囲）。見つかった組と、attach の
応答の target_id（chip_id）を、設定の `target` の項目（wire_fn、scheme、mask、value）と `bind` の pins に書く。

- **1 つの線（wire のインターフェース）に持てる connection は 1 つ。** 別のピンの組への attach は断られる（oep-if-debug §1）。
  複数の target を同時に持つには、probe が wire のインスタンスを複数持つ必要がある。今の probe はどれも 1 つ。

**3. attach の仕方を決める。** 自動の attach は設定の `bind` の attach で選ぶ（oep-if-probe-config §1.1）。

| attach | 意味 | 向く場面 |
|---:|---|---|
| 0 | host に任せる。host（書き込みツール、pytest）が attach したら、その connection でコンソールを流す | ベンチ。書き込み → モニタの導線でも足りる（P6 で確認） |
| 1 | 口が開かれたとき（DTR）に自動で attach | モニタを開いたら見たい。開いてから約 170 ms は出力が流れない（X3） |
| 2 | 起動時に自動で attach | probe と target を組にして置いておく。probe の再起動で target は再起動しない（P6） |

- 自動の attach は**止めない attach（method 0）だけ**で、target_id が設定と一致したときだけコンソールを開く。違えば外して
  bind_state で知らせる。ここまでは仕様にあり、X035 の治具で動いた（P6）。
- **attach 自体が問題を起こすことはある。** 止めない attach は、0.5 µs の分解能で見て動いているアプリを乱さなかった（X3）。
  一方、WCH-LinkE の attach は target のクロックを組み替える（L103 / V20x / V30x / X035。wch-protocols）。OEP の probe の
  attach は DM に書くだけで RCC には触れないので、この問題は LinkE の firmware の側。HPRE の bit3 を立てた X035 が attach だけで
  止まった件（E164）は core が HPRE の符号化を変えて避けた。

**4. target がいるかを、定期的に確かめられるか。**

- **線を駆動せずには分からない**（X2）: CH32 の debug の線は probe の内蔵プルに勝たず、何もつながっていないピンと同じに見える。
  確かめるには DMSTATUS を読むなど、線を駆動する操作が要る。target のアプリがその線を GPIO として使っていれば、駆動は出力と
  ぶつかる。
- **コンソールを bind している間は、ただで分かる。** dmseq / SDI は probe が DM の data レジスタを読み続けるので、target が
  消えれば DMI が失敗し、probe はストリームに link-lost を付けて connection を閉じ、bind は次の合図（attach 0 は host の
  attach、1 は次の DTR、2 は次の起動）まで待つ（oep-if-console §2、oep-if-probe-config §1.1）。P6 の試作は 250 ms ごとに
  attach をやり直した。
- 提案: **定期的な確認は、bind が無いときは行わない**（駆動しない）。bind があるときは、失敗したら間隔を広げながら attach を
  やり直す（250 ms から始めて数秒まで）。「いるか」を host が知りたいときは、describe の bind_state（待っている / 流している /
  不一致 / 失敗）を読む。

**5. シリアルの口に何を流すかを決める。** `bind`（port、source、attach、引数）。source は fixture.uart か target.console
（wire_fn、mechanism dmseq / SDI / DMDATA、pins）。

- **今の仕様は、口 1 つに source 1 つ。** つまり「特定の target だけを流す」。これが基本。
- **全部混ぜて流す**: 仕様に無い。足すなら、口に複数の source を結び、行の先頭に印（`[1]` など）を付ける形。行の途中で
  切り替わると読めないので、行単位で混ぜる。要る場面（複数の target を 1 つのモニタで見る）が出てから。
- **最後に attach したものだけ流す**: host の操作の順で見えるものが変わるので、勧めない。複数の target なら、口を target ごとに
  分ける（P4 は CDC を 3 口まで持てる。X1）か、印を付けて混ぜる。
- 複数の target が要るのは probe が wire を複数持つときだけ（上の 2）。今の probe では起きない。

**6. 保存する。** `save`。bind はすぐ効き、再起動は要らない。

### シリアルの口が無い probe（HID / vendor だけ）でも、Arduino のモニタに出せるか

出せる。IDE のモニタはシリアルポートに限らず、**pluggable monitor**（protocol ごとの外部ツール。IDE は TCP で受け取り、送信欄の
入力も渡す）で見られる。`wchlink://` のポートで ch32rv の monitor が dmseq を流したのと同じ形（§4.6 の実験）。

- discovery が probe を `oep://<serial>` のポートとして出し、`pluggable_monitor.pattern.oep` のツールが HID / vendor で OEP を
  話して target.console を TCP に流す。設定（DESCRIBE）で mechanism（dmseq / SDI）や fixture.uart を選べる。
- ツールは ch32rv か OEP の client のどちらが持つか、未定（§3。全体が固まってから）。
- HID の権限: Linux は hidraw が root だけなので udev の規則が要る（ch32rv の udev の規則に足す）。Windows は要らない。
- シリアルの口を持つ probe では、組み込みの serial-monitor で足りる（共用の口、§4.2）。

## 4.6 書き込みの経路: ポートを選ぶだけで書き込む（提案）

### 経路の一覧

| 経路 | 相手の USB | 入り方 | 今の道具 |
|---|---|---|---|
| WCH-LinkE / WCH-Link（SWD / RVSWD） | LinkE の CDC `1a86:8010`（DAP は `8012`）。シリアルポートとして見える | 常時 | ch32rv `flash`（今の `wch-link` programmer） |
| OEP の probe（RVSWD / SWIO） | P4 の USJ `303a:1001`、vendor / HID、または変換チップ（CP2102 `10c4:ea60` など）。シリアルポートとして見える | 常時 | OEP の host（今は client の `ch32_flash`。ch32rv は後） |
| ソフト USB のブートローダー（UIAPduino rv003usb、HID `1209:b803`） | HID。シリアルポートでは**ない** | 外からの操作（リセット 2 連発、BOOT 手順）。app が協力すれば touch1200 | ch32rv `boot hid flash`（今の `ch32rv_hid` tool） |
| 直 USB の factory ISP（X03x / X315 / H417、`4348:55e0`） | vendor。シリアルポートでは**ない** | BOOT ピン、または app が協力する touch1200（ch32rv `isp enter --via touch1200`） | ch32rv `isp flash` |
| UART の factory ISP（家系による） | 変換チップ越し。シリアルポート | BOOT ピン | ch32rv `isp --transport uart` |
| DFU（将来のブートローダー） | DFU class | — | ch32rv `boot dfu` |

### Uno のように、ポートだけでよいか

Uno はポートを選ぶだけで書き込める（プログラマの選択は「書き込み装置を使って書き込む」の別の操作）。同じことは、
**ポートに付いてくる情報（`{upload.port.protocol}`、`{upload.port.properties.vid / pid / serialNumber}`）を 1 つの書き込みツールが
受けて、経路を自分で決める**形でできる。Arduino の仕組み（pluggable discovery）はそのために `upload.tool.<protocol>` と port の
properties を持っている。今の `platform.txt` は `upload.tool=ch32rv` だけで、素の `upload` は programmer が無いと断る。

| 選んだポート | 見分け方 | 書き込みの経路 |
|---|---|---|
| LinkE の CDC | VID:PID `1a86:8010` / `8012`。serialNumber で個体も決まる（`--probe serial:<sn>`） | LinkE。**見分けられる** |
| ch32rv の discovery が出す `wchlink://<serial>`（protocol `wchlink`、実装済み） | protocol | 同上 |
| OEP の probe（USB の VID:PID を持つもの: P4 の USJ など） | VID:PID + serialNumber。同じ個体の vendor / HID を優先して開く | OEP |
| OEP の probe（変換チップ越し: classic ESP32） | VID:PID は汎用（CP2102）で見分けられない。**ツールが OEP の describe を送って確かめる**（COBS のフレーム、200〜300 ms で答えが無ければ違う）。共用の口なので、答えが無いときに送ったフレームは target のコンソールに文字として出るだけ | OEP。**モニタで開くのと同じポートに書き込む** |
| target 自身の CDC（core が USB CDC を持つ家系: X035 など。core が決める VID:PID） | VID:PID | touch1200 で ISP に入れ、`4348:55e0` の再列挙を待って ISP。Leonardo と同じ導線。**core 側に touch1200 で ISP へ跳ぶ実装が要る** |
| ポート無し | `upload.tool.default` | ツールが USB を走査（LinkE、OEP の USB の probe、HID のブートローダー、ISP の device）。**候補が 1 つならそれ**、複数なら候補を並べて断る |

- **ポートの一覧は広げられる（実験で確認、2026-09-29）。** `arduino-cli board list` の一覧は pluggable discovery が作っていて、
  OTA の IP は組み込みの `mdns-discovery`、`1-8 dfu` は `dfu-discovery` が出している。platform が自前の discovery を登録すれば
  同じ一覧に足せる。ch32rv の `arduino discovery` を platform.txt に仮登録したところ、LinkE 7 台が `wchlink://<serial>`
  （protocol `wchlink`、properties に vid / pid / serial / mode）として serial と dfu と並んで出た。

  ```text
  pluggable_discovery.required.0=builtin:serial-discovery      # 1 つでも自前を書くと組み込みも明示が要る
  pluggable_discovery.required.1=builtin:mdns-discovery
  pluggable_discovery.ch32rv.pattern="{runtime.tools.ch32rv.path}/ch32rv" arduino discovery
  ```

  同じ仕組みで、HID のブートローダー（`1209:b803`）と ISP の device（`4348:55e0`）も `hid://` / `isp://` の port として出せる
  （ch32rv の cli 文書に「ISP device・CDC monitor port も列挙する」の予定がある）。OEP の USB の probe（P4 の vendor / HID の口）
  も同じ discovery で `oep://<serial>` として出せる。変換チップ越しの OEP の probe は、組み込みの serial discovery が出す
  ポートのまま（describe で確かめる）。protocol ごとに `upload.tool.<protocol>` で tool が決まるので、`wchlink` / `hid` / `isp` /
  `oep` / `serial` を全部同じツールに向ける。
- discovery は USB の記述子だけを見て列挙し、AttachChip はしない（同じ probe への書き込みやモニタの最中でも乱さない。ch32rv の
  実装済みの仕様）。ホットプラグ（START_SYNC の追加・削除の通知）は未実装で、IDE の一覧の更新に任せている。
- `boards.txt` の `upload_port.vid / pid` に、その板の CDC と LinkE の VID:PID を並べれば、IDE がポートと板を結び付ける（ポート
  一覧に板名が出る）。
- **プログラマのメニューは残すが、上書きに使う**: ベンチのように書き込み装置が複数つながっているとき、ポートで決まる経路以外を
  強いるとき。普段は要らない。

### 1 つの書き込みツール

`platform.txt` の `upload.tool.serial` / `upload.tool.wchlink` / `upload.tool.default` を全部同じツールに向け、ツールが
`{upload.port.address}`、protocol、vid、pid、serialNumber、`{build.ch32rv_chip}` を受けて上の表で決める。ツールは ch32rv が
本命（LinkE / HID / ISP / DFU は既に持つ。OEP だけ無い）。ch32rv の OEP 対応まで、OEP の枝だけ client の `ch32_flash` を呼ぶ
薄い包みを置くか、ch32rv に OEP の枝を先に足すかは、§3 のとおり全体が固まってから決める。

### 発見したポートで、書き込みからモニタまでつながるか（実験、2026-09-29）

X035 の治具の LinkE（`FC928F068181`）で、platform.txt に仮登録して試した（終わったら戻した）。

| 段 | 結果 |
|---|---|
| `arduino-cli board list` | `wchlink://FC928F068181` が出る（前の実験のとおり） |
| `arduino-cli upload -p wchlink://FC928F068181 --fqbn …`（**programmer 指定なし**） | **通った**。`upload.tool.wchlink` の recipe で `--probe serial:{upload.port.properties.serial}` が `FC928F068181` に展開され、HelloDMSeq が X035 に書けた。ただし板に `upload.protocol` が無いと「A programmer is required to upload」で断られる（`--upload-property upload.protocol=wchlink` で通した）。**今の boards.txt で `upload.protocol` を持つのは UIAPduino だけで、素の upload が断られていた本当の理由はこれ** |
| `arduino-cli monitor -p wchlink://… --fqbn … --config source=dmseq` | monitor は見つかる（`pluggable_monitor.pattern.wchlink` で登録。**`--fqbn` が要る**: platform 自前の monitor は板の platform から引く。IDE は常に板を持つので問題ない）。しかし arduino-cli が **DESCRIBE の応答で落ちる**（nil 参照）。原因は ch32rv が `port_descriptor` を返し、仕様と arduino-cli は `port_description` を読むこと（[ch32rv-requests](ch32rv-requests.ja.md) の既知不具合に記録） |
| ch32rv の monitor に手で HELLO / CONFIGURE `source dmseq` / OPEN | **通った**。HelloDMSeq の `uptime` が IDE 相当の TCP に 7 秒間届いた。残りの経路は動く |

つまり、**発見したポートでモニタにつながる**。つながらないのは 1 語のキー名だけで、直せば IDE でポートを選ぶだけで
書き込み → dmseq のモニタまで通る。

| 経路 | IDE でそのポートを選んだときのモニタ | 空き |
|---|---|---|
| `wchlink://<serial>`（LinkE） | ch32rv の pluggable monitor: dmdata / dmseq / rtt（設定の `source`） | **UART / SDI は出ない**。LinkE の CDC の serial port を選び直す（書き込みと別のポート）。ch32rv の monitor に `uart` の source を足せば 1 つで済む（依頼 B-7） |
| LinkE の CDC（serial） | 組み込みの serial-monitor（UART / SDI） | 書き込みは VID:PID で LinkE と分かるので同じポートでできる（§4.6 の表） |
| `oep://<serial>`（OEP の USB の probe） | pluggable monitor が要る（target.console を TCP に pipe。ch32rv か OEP の client） | 未実装 |
| OEP の probe の serial（USJ、変換チップ） | 組み込みの serial-monitor（共用の口、§4.2） | — |
| ソフト USB / ISP / DFU | 書き込みの後に app が列挙する CDC（あれば） | 書き込みの装置とは別の口 |

pluggable monitor は IDE の送信欄の入力も target に届ける（ch32rv の実装）。設定（DESCRIBE の `configuration_parameters`）は
IDE のモニタの右上のメニューに出る（今は `source` だけ）。

## 5. 仕様と実装に要ること

| 項目 | 置き場 | 状態 |
|---|---|---|
| CDC / USB-Serial/JTAG のフレームを COBS + CRC-16 に（core §3.1 の表） | oep-spec | 未提案 |
| シリアルの口の共用の規則（§4.2: 見分け方、前後の区切り、セッションの口では生の転送を止める、再開の位置） | oep-spec（core §3 の経路の節） | 未提案 |
| USB を持たない probe の口（シリアル）の宣言と bind。今の bind と describe の port は USB の CDC だけが対象 | oep-spec（oep-if-probe-config §1、§3） | 未提案 |
| probe の再起動の抑止（§4.4）を probe 開発ガイドに | oep-spec（probe-development-guide） | 未着手 |
| client: シリアルのポートは常に COBS、フレームの外のバイトは雑音として捨てる | oep-client-python（`link.py`） | 未着手 |
| probe の共用する口の送信のキュー（フレームとコンソールの塊を単位に、1 つの書き手） | oep-probe-arduino（Endpoint と bind の間） | 未着手 |
| classic ESP32 の probe: 共用、bind、NVS の保存（今は Endpoint だけで ProbeConfig が無い） | oep-probe-arduino（Esp32V003Probe） | 未着手 |
| P4 の probe: USJ を COBS に、HS の CDC の bind を本線へ（ConsolePrototype から）、USJ の reset 抑止 | oep-probe-arduino（Esp32P4X035Probe） | 未着手 |
| Web Serial の設定のページと JavaScript の OEP client | 置き場は未定（§6 の 3） | 未着手 |
| ポートで経路を決める 1 つの書き込みツール（`upload.tool.serial` / `default` / `wchlink` / `hid` / `isp` / `oep`、port の properties を渡す）、`boards.txt` の `upload_port.vid/pid` | ArduinoCore-CH32（`platform.txt`、`boards.txt`）と ch32rv | 未着手 |
| ch32rv の discovery を platform.txt に登録（`pluggable_discovery.ch32rv.pattern`、組み込みの serial / mdns も明示） | ArduinoCore-CH32（`platform.txt`） | 実験で動作を確認、未登録 |
| core: USB CDC を持つ家系で touch1200 から ISP へ跳ぶ（Leonardo の導線） | ArduinoCore-CH32 | 未着手 |
| ch32rv の discovery に HID のブートローダーと ISP の device を足す（任意） | ch32rv | 予定あり（cli 文書） |
| ch32rv の OEP 対応 | ch32rv（依頼は [ch32rv-requests](ch32rv-requests.ja.md)） | 全体が固まってから |

## 6. 決めること

1. **§4.1 と §4.2 でよいか**（シリアルに見える口はすべて COBS、常に OEP を受け、それ以外はコンソール、セッションの口では生の
   転送を止める）。
2. **セッションが終わって生の転送を再開するとき、止めていた間のコンソールの出力をどうするか。**
   - 捨てて今から流す: 書き込みツールが reset した直後の出力（起動の banner）はモニタに出ない。
   - 全部流す: 長いセッション（pytest）の後に大量に流れる。
   - 案: セッションの中で最後に行った riscv-dm の reset の位置から流す（無ければ今から）。書き込み → モニタの導線で banner が
     見える。位置はストリームのマークで表せる（oep-if-common §1 の mark）。
3. Web Serial の設定のページと JavaScript の client の置き場（新しいリポジトリか、oep-client の隣か）。
4. §4.5 の流れでよいか。特に: 定期的な確認は bind が無いときは行わない（線を駆動しない）、口 1 つに target 1 つを基本にして
   「混ぜる」は印付きで後から、「最後に attach したもの」は採らない。
5. **書き込みは §4.6 のとおり「ポートだけで決める、プログラマは上書き」でよいか。** 変換チップ越しの OEP の probe を describe で
   確かめる方式でよいか（VID:PID では見分けられない）。
   ポートだけで書くには、板に `upload.protocol` を持たせる必要がある（無いと arduino-cli が programmer を要求する）。
   発見した `wchlink://` のポートで UART も見るには、ch32rv の monitor に `uart` の source を足す（依頼 B-7）か、UART は LinkE の
   CDC のポートを選ぶ、のどちらか。
6. core に touch1200 → ISP を入れるか（X035 など、USB CDC を持つ家系）。
7. デバッグ（gdb）を OEP の probe でも扱うか（U6）。ch32rv と一緒に後で。

## 7. 確認済み事実

- UART の OEP のフレームは COBS + CRC-16/CCITT-FALSE、0x00 区切り。CDC / USB-Serial/JTAG / TCP / vendor bulk は `length(u16) message`
  （oep-core §3.1）。probe はフレームの途中で 200 ms 途切れたら読み直す（同 §3.2）。
- host は同じ probe に複数の経路があれば vendor bulk、HID、CDC の順に試す（oep-core §3.3）。複数の経路はセッションとロックを 1 つ
  共有する（同、P1 で確認）。
- bind（口に何を流すか）と describe の port は、今は USB の CDC の口だけが対象（oep-if-probe-config §1、§3）。bind はすぐ効く
  （「いつ効くか: すぐ」）。
- CDC の口へのコンソールの bind と、host の detach / reset を越えて続くことは X035 の治具で動いた（P6）。設定の保存（NVS）は
  P4 で動いた（保存 2 ms。P4 の結果）。
- 1 つの wire のインターフェースの connection は 1 つ。生きている connection と違う組の attach は rejected unavailable
  （oep-if-debug §1）。scan は probe が許す組を順に試して見つかった組を返す。
- 線を駆動せずに target の有無は分からない（X2: debug の線は probe の内蔵プルに勝たない）。止めない attach は動いているアプリを
  0.5 µs の分解能で乱さず、約 170 ms かかる（X3）。連続の bind のコンソールは DMI の失敗で link-lost になり、P6 の試作は
  250 ms ごとに attach をやり直した。
- 経路ごとの速さ（X6）: vendor bulk 37 MB/s、CDC 8.2 MB/s、HID 0.84 MB/s（probe → host）。往復はどれも 1 ms 未満。
- CDC を 2 口以上持つと vendor のストリーミングが欠ける（X1）。Linux の cdc-acm は開くときに DTR / RTS を一度立てる（X1）。
- コンソールの受信は位置付きのストリームで、読んでも消えない。読み手が増えても取り合わない。同じ接続で DM の mailbox を使う
  方式（dmseq、dmdata、SDI）は 1 つだけ。
- 再起動の抑止: P4 の `usb_serial_jtag_reg.h` に `USB_SERIAL_JTAG_USB_UART_CHIP_RST_DIS`（`USB_DEVICE_CHIP_RST_REG` bit2）がある。
  arduino-esp32 3.3.12 の `USBCDC::enableReboot(bool)` は DTR / RTS の並びと 1200 baud の touch を抑止する。HWCDC には同じ API は
  無い。EspUsbDevice 2.5.1 の CDC は DTR / 1200 baud の処理を持たない。
- classic ESP32 の probe は、DTR と RTS を両方 on で開くと再起動せず、両方 off にして開くと再起動した（2026-09-29、pyserial）。
  `arduino-cli monitor` の既定は dtr=on、rts=on。
- oep-client-python の COBS の読み手は、0x00 の前のバイトをフレームとして解き、失敗すると壊れた応答として送り直す（`link.py`
  `_recv` → `cobs.unframe`）。フレームの形は USB の VID:PID で選ぶ（`framing_for`）。今の client は 115200 で開く。
- 今の classic ESP32 の probe（Esp32V003Probe）は Endpoint だけで、ProbeConfig（設定と保存）を持たない。P4 の X035 の probe は
  USB-Serial/JTAG で OEP を運び、コンソールの CDC は試作（Esp32P4X035ConsolePrototype）にある。
- 今の OEP の試験（`tests/manual/oep_*`）は、pytest に当たる Python のスクリプトが 1 つのセッションで書き込み・コンソール・
  キャプチャをすべて扱っている。書き込みは client の `ch32_flash`。
- ch32rv は WCH-LinkE / WCH-Link（`flash`）、HID のブートローダー（`boot hid`、`1209:b803`）、factory ISP（`isp`、USB `4348:55e0` と
  UART、X03x / X315 / H417 は `isp enter --via touch1200`）、DFU（`boot dfu`）に対応し、OEP の probe には対応していない。monitor の
  source は uart / sdi / dmdata / dmseq / rtt。`arduino discovery` は `wchlink://<serial>` の port を protocol `wchlink` で列挙する
  （実装済み）。`arduino monitor` は pluggable monitor（dmdata / rtt）。
- 今の `platform.txt`: `upload.tool=ch32rv`、`upload.tool.default=ch32rv`。素の `upload` は「A programmer is required」で断る
  （programmer `wch-link` を指定して書く）。UIAPduino は `upload.protocol=hid` と `ch32rv_hid` tool（`boot hid flash --usb-id 1209:b803`）。
- Arduino の pluggable discovery: port には protocol と properties（serial discovery は vid、pid、serialNumber）が付き、
  `upload.tool.<protocol>` で tool を選べる。ポート無しは `upload.tool.default`。組み込みは serial / mdns / dfu の 3 つ
  （`~/.arduino15/packages/builtin/tools/`）。platform の自前の discovery は `pluggable_discovery.<id>.pattern` で登録し、1 つでも
  書くと組み込みも `pluggable_discovery.required.N` で明示が要る（最初 `discovery.<id>.pattern` と書いて出なかった）。
- 実験（2026-09-29）: ch32rv `arduino discovery` を登録した `arduino-cli board list` に、LinkE 7 台が `wchlink://<serial>` として
  出た（protocol `wchlink`、properties vid / pid / serial / mode、hardware_id = serial）。登録は戻してある。
- 実験（2026-09-29）: `upload.tool.wchlink` と `upload.protocol` を与えると、`arduino-cli upload -p wchlink://FC928F068181` が
  programmer なしで X035 に書けた。arduino-cli は板に `upload.protocol` が無いと「A programmer is required to upload」を返す
  （バイナリに `upload.protocol` の参照がある）。platform 自前の monitor は `pluggable_monitor.pattern.<protocol>`（バイナリの
  文字列に `pluggable_monitor.pattern` / `pluggable_monitor.required`）で、`arduino-cli monitor` には `--fqbn` が要る。arduino-cli
  1.3.1 は DESCRIBE の応答の `port_description` を読み、無いと panic する。ch32rv 0.10.1 は `port_descriptor` を返す。
- ch32rv `arduino monitor` の DESCRIBE は `source`（dmdata / dmseq / rtt）だけを出す。uart / sdi は wrap しない（cli 文書）。手で
  OPEN すると X035 の HelloDMSeq の出力が TCP に届いた。
- 例のスケッチの `sketch.yaml`（index の platform を指すプロファイル）が付いたままだと、手元の platform で compile できない
  （"Platform … is not found in any known index"）。試験の道具は sketch.yaml を写さない。

## 関連

[harness-requirements](harness-requirements.ja.md) / [harness-probe](harness-probe.ja.md) /
[harness-testing](harness-testing.ja.md) / [ch32rv-requests](ch32rv-requests.ja.md) /
[debug-output](debug-output.ja.md) / oep-spec の `probe-cdc-and-persistence.ja.md`、`session-and-exclusivity.ja.md`、
`oep-if-probe-config.ja.md`、`v1-open-proposals.ja.md` §7
