# OEP を含む開発ワークフロー（統括）

文書基準日: 2026-09-29（同日に §4 以降を深掘り）。状態: **提案**（決定済みの項目は明記する）。ch32rv の OEP 対応（§3）は、全体が固まってから検討する。

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
| U2 利用者、OEP の probe（USB の P4 など） | OEP の書き込みツール（vendor / HID / USJ） | probe のコンソール専用の CDC の口（bind、§4.3） | — | 書き込みの間だけ書き込みツール |
| U3 利用者、シリアルしかない OEP の probe（classic ESP32） | 同じポートで OEP の書き込みツール | 同じポート（§4.4） | — | 書き込みの間だけ書き込みツール |
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

## 4. 口の原則: CDC で最初に入り、ほかの経路があれば CDC はモニタ専用にする（提案）

### 4.0 原則

1. **どの probe も、最初は CDC（シリアル）で OEP を話す。** 権限の壁がいちばん低い経路だから（Linux は dialout、Windows は
   usbser、macOS はそのまま、ブラウザは Web Serial。oep-spec `probe-cdc-and-persistence.ja.md` §5.1）。まっさらな probe
   （保存が無い）は、必ずこの形で列挙する。設定はこの経路で行う（§4.2）。
2. **ほかの経路（vendor bulk、HID、USB-Serial/JTAG）を持つ probe は、設定のあと、CDC をモニタ専用にして OEP をほかの経路に
   移す。** CDC の口には、bind でコンソール（dmseq / SDI / fixture.uart）を流す。OEP の制御は CDC を通らない。
3. **シリアルしかない probe（classic ESP32）は、移す先が無いので、同じ 1 本を OEP とコンソールで共用する**（§4.4）。

2 は起動モード（`oep.probe.config` の boot_mode）で表す。USB の構成（interface の数と並び）は列挙のときに決まるので、同じ
起動の中で CDC の役割を切り替えることはできない（切り替えは再列挙 = probe の再起動）。したがって「最初に CDC、その後ほかへ」は
**起動ごとの構成の違い**として持つ。

| モード | OEP の制御 | CDC の口 | いつ |
|---|---|---|---|
| setup | CDC（functions bit1） | OEP | 保存が無いとき。設定と復旧 |
| debugger | vendor bulk / HID / USJ | コンソール（bind） | 設定を保存して再起動した後 |
| logic | vendor bulk だけ（CDC なし） | — | 帯域を優先するとき（X1: CDC が 2 口以上あるとストリーミングが欠ける） |

- setup モードは「保存が無いときに probe が選ぶモード」（oep-if-probe-config §2: 保存を読めないときや boot_mode が組めないときは
  probe が自分で選ぶ）に当たる。ユーザーの方針「既定の振る舞いを仕様に書かない」（同 §5）とは、**保存が無い状態から host が
  入れる口が 1 つは要る**という点で折り合いを付ける必要がある（§6 の 1）。
- P4 の X035 の治具は今、OEP を USB-Serial/JTAG（OS からは CDC に見える）で運び、HS の口にコンソールの CDC を足す試作がある
  （P6）。原則 2 の「ほかの経路」に USJ を数えるなら、この形は debugger モードそのもの。HS の vendor / HID に移すかは帯域と
  権限で決める（制御だけなら USJ で足りる: X6 で往復 1 ms 未満）。

### 4.1 経路ごとに、何がどこを流れるか

| probe | setup（保存なし） | debugger（保存あり） |
|---|---|---|
| P4（USJ + HS） | USJ = OEP。HS は列挙しない、または vendor + CDC(OEP) | USJ = OEP（または HS の vendor / HID）。HS の CDC = コンソール |
| classic ESP32（UART 1 本） | UART = OEP（生の転送なし） | UART = OEP とコンソールの共用（§4.4） |
| RP2350（USB CDC 1 口） | CDC = OEP | vendor か HID を足せば P4 と同じ。足さなければ共用（§4.4） |

### 4.2 設定は一度だけ、ブラウザから

- setup モードの probe のポートを Web Serial で開き、OEP の `oep.probe.config` を読み書きする JavaScript の client（describe、
  session、probe.config だけを話す最小のもの）。Web Serial は Chrome / Edge。
- 設定するもの: target と線（wire、ピン、target_id の照合）、CDC の口に流すコンソール（bind: source 2 の mechanism dmseq /
  SDI、または source 1 の fixture.uart）、boot_mode = debugger、リンクの速さ（シリアルしかない probe。§4.4）。`save` → `reboot`。
- **再起動の後、そのページは同じ口では話せない**（CDC はコンソールになる）。設定を変え直すには、P4 なら WebUSB（vendor）か
  WebHID（HID）で入る、または setup モードで起動し直す。シリアルしかない probe は共用なので同じ口で続けて話せる。
- setup モードへ戻る手段が要る（**復旧**。oep-spec `v1-open-proposals.ja.md` §7 と同じ問い）: ボタンを押しながら起動、
  リセット 2 連発、`erase` して再起動、など。debugger モードで OS に権限が無い経路にだけ OEP を置くと、この手段が無いと
  入れなくなる。

### 4.3 P4（USB の probe）での流れ

U2（利用者、Arduino IDE）:

1. setup モードで設定（§4.2）。再起動して debugger モード。OS には USJ のシリアルと、HS の CDC「Target console」が見える。
2. 書き込み: 書き込みツールが USJ（または vendor / HID）で `open` → attach → 書き込み → riscv-dm の reset → `end`。この間も
   HS の CDC のコンソールは流れ続ける（bind の connection は host の detach や reset を越えて続く。P6 で確認）。
3. モニタ: HS の CDC を開く。reset 直後の最初の出力から見える（P6: "hello from the debug module" から取れた）。

U5（pytest）: pytest は USJ で 1 つのセッションを握り、コンソールは `target.console` で、キャプチャは `fixture.capture` で読む。
HS の CDC を同時に開いても、同じコンソールの写しが流れるだけで、pytest とは混ざらない（位置付きのストリームは読み手が増えても
取り合わない）。今の試験（`tests/manual/oep_*`）はこの形で動いている。

### 4.4 シリアルしかない probe で、1 本を共用する

```text
 PC ──USB── CP2102 ──UART0── classic ESP32（probe）──SWIO / RVSWD── CH32（target）
                              │ 0x00 で区切られ CRC の合うまとまり → OEP として処理
                              │ それ以外のバイト → target のコンソールへ（dmseq の入力、または fixture.uart の TX）
                              │ target のコンソールの出力 → 生のまま PC へ
```

**規則**

1. probe は入力を常に見張る。0x00 が来たらフレームの候補として次の 0x00 までためる。COBS を解いて CRC-16 が合えば OEP の
   フレーム（UART の形は今のまま: COBS + CRC-16/CCITT-FALSE、0x00 区切り。oep-core §3.1）。合わなければ、ためたバイトを
   そのままコンソールへ送る。途中で 200 ms 途切れたら（oep-core §3.2 の読み直しの規則）、同じくためたバイトをコンソールへ送る。
   0x00 以外のバイトは、ためずにすぐコンソールへ送る。
2. probe → PC: コンソールの出力は生のまま。OEP の応答と push は `0x00 <COBS> 0x00` と**前にも区切りを付けて**送り、フレームの
   途中にコンソールのバイトを挟まない（送信を直列にする）。
3. **ロックを持つセッションがある間は、生の転送を両方向とも止める。** コンソールの出力は probe の中の位置付きのストリームに残り、
   `target.console` の read で読める。PC から来たフレームの外のバイトは捨てる。
4. セッションが終わったら（`end`、または lease の期限切れ）、生の転送を再開する。どこから流すかは §6 の 3。

**client 側に要る変更**: 今の COBS の読み手（oep-client-python `link.py` の `_recv`）は、0x00 までのバイトをそのままフレームとして
解き、失敗すると「壊れた応答」として送り直す。共用の口では、セッションを開く前のコンソールの文字が必ず先に来るので、毎回それが
起きる。**フレームの外のバイト（0x00 の前の解けないまとまり）は雑音として捨て、応答の欠落は時間切れだけで判断する**形に変える。
本当に壊れた応答（CP2102 の取りこぼし）も雑音に見えるが、時間切れで送り直すので結果は同じ（遅くなるだけ）。

**成り立つ理由と限界**

- テキストのコンソールには 0x00 が出てこないので、フレームの始まりと取り違えない。0x00 を含むバイナリをモニタから送ると、次の
  0x00 か 200 ms までためるので、その分だけ遅れて届く（捨てはしない）。モニタを使う人は OEP のバイナリを流さない前提。
- **リンクの速さは固定。** USB-UART の変換チップは、PC 側で選んだ baud を ESP32 に知らせない。今の client は 115200 で開く。
  モニタも 115200 で開く必要がある。dmseq には baud が無い（DMI の polling の速さで決まる）。fixture.uart を流す場合の target
  の UART の速さは、bind の引数で別に決める。速さを設定の項目にするかは §6 の 4。
- **DTR / RTS**: classic ESP32 の開発ボードは DTR / RTS で再起動する回路を持つ。実測（2026-09-29、pyserial）: DTR と RTS を
  両方 on で開くと再起動せず、両方 off で開くと再起動した（起動のログ 325 バイト）。`arduino-cli monitor` の既定は dtr=on
  rts=on なので再起動しない。Arduino IDE 2 のモニタは同じ pluggable monitor を使うはずだが、未確認。
- 取りこぼし: CP2102 は長い連続の送信でバイトを落とす（v003 の治具で実測）。OEP のフレームは CRC と送り直しで守られる。生の
  コンソールは守られない（欠けたら欠けたまま）。
- Linux の tty は 2 つのプロセスが同時に開ける。モニタと書き込みツールが同時に開くと、PC → probe のバイトが混ざる。書き込み
  ツールは `exclusive=True` で開く（今の client はそう）。IDE 2 は書き込みの間モニタを切る（はず。未確認）。

**Arduino IDE での流れ（U3）**

1. ボードを選び、ポートに probe のシリアルを選ぶ。書き込みの方法（programmer）に「OEP probe (serial)」を選ぶ。
2. 書き込み: 書き込みツールがポートを開く（DTR / RTS on）。コンソールの文字が流れてくるが、client は雑音として捨てる。`open` →
   attach → 書き込み → reset → `end`。この間、生の転送は止まっている。
3. モニタ: 同じポートを開く。セッションは終わっているので、target のコンソールが流れる。打った文字は target へ届く。

**pytest（U5）**: 試験の間ずっとセッションを握るので、ポートに流れるのは OEP のフレームだけ。コンソールは `target.console` で、
キャプチャは `fixture.capture` で読む。混ざらないようにする仕組みは「セッションの間は生の転送を止める」規則そのもので、
pytest 側に特別な分離は要らない。試験中にモニタを開くと、フレームのバイナリが見えるだけ。

### 4.5 2 つの読み方（記録）

「最初に CDC でアクセスし、ほかの経路があれば CDC はモニタ専用に」は、次の 2 通りに読める。

| 読み方 | 内容 | 判断 |
|---|---|---|
| A. 起動ごとの構成 | 保存が無い起動は CDC = OEP（setup）。設定して再起動したら、CDC はコンソール、OEP はほかの経路（debugger） | **採る**。USB の構成は列挙で決まるので、これが自然 |
| B. 同じ起動の中で切り替える | CDC で OEP を受けているが、ほかの経路でセッションが開いたら CDC をコンソールに切り替える | USB では取れない（口の役割は列挙で決まる）。シリアルしかない probe では、§4.4 の共用の規則がこれに当たる |

## 5. 仕様と実装に要ること

| 項目 | 置き場 | 状態 |
|---|---|---|
| 起動モードの並び（setup / debugger / logic）と、保存が無いときのモード（setup） | oep-spec（oep-if-probe-config §2〜§3） | 未提案（describe の mode の形は既にある） |
| USB を持たない probe の口（シリアル）の宣言と bind。今の bind と port は USB の CDC だけが対象 | oep-spec（oep-if-probe-config §1、§3） | 未提案 |
| 1 本の口の共用（フレームの見分け方、前後の区切り、セッション中は生の転送を止める、再開の位置） | oep-spec（core §3 の経路の節か、oep-if-probe-config） | 未提案 |
| リンクの速さの設定の項目（シリアルしかない probe） | oep-spec（oep-if-probe-config） | 未提案 |
| setup モードへ戻る手段（復旧） | oep-spec（v1-open-proposals §7）。probe の作りに依る部分はガイド | 未決 |
| client: フレームの外のバイトを雑音として捨てる | oep-client-python（`link.py` の COBS の読み手） | 未着手 |
| classic ESP32 の probe: 共用、bind、NVS の保存（今は Endpoint だけで ProbeConfig が無い） | oep-probe-arduino（Esp32V003Probe） | 未着手 |
| P4 の probe: setup / debugger の 2 モード（ConsolePrototype の bind を本線へ） | oep-probe-arduino（Esp32P4X035Probe） | 未着手 |
| Web Serial の設定のページと JavaScript の OEP client | 置き場は未定（§6 の 5） | 未着手 |
| programmer の登録（OEP probe）と、書き込みツールの呼び出し | ArduinoCore-CH32（`platform.txt`、`programmers.txt`） | 未着手 |
| ch32rv の OEP 対応 | ch32rv（依頼は [ch32rv-requests](ch32rv-requests.ja.md)） | 全体が固まってから |

## 6. 決めること

1. **保存が無いときのモードを setup（CDC = OEP）と定めてよいか。** 「既定の振る舞いを持たない」方針との折り合い。
2. **§4.4 の共用の規則でよいか**（0x00 で始まるフレーム、CRC が合わなければコンソールへ、セッション中は生の転送を止める）。
3. **セッションが終わって生の転送を再開するとき、止めていた間のコンソールの出力をどうするか。**
   - 捨てて今から流す: 書き込みツールが reset した直後の出力（起動の banner）はモニタに出ない。
   - 全部流す: 長いセッション（pytest）の後に大量に流れる。
   - 案: セッションの中で最後に行った riscv-dm の reset の位置から流す（無ければ今から）。書き込み → モニタの導線で banner が
     見える。位置はストリームのマークで表せる（oep-if-common §1 の mark）。
4. リンクの速さを設定で選べるようにするか、115200 に固定するか。
5. Web Serial の設定のページと JavaScript の client の置き場（新しいリポジトリか、oep-client の隣か）。
6. setup モードへ戻る手段を、probe ごとにどれにするか（v1-open-proposals §7）。
7. デバッグ（gdb）を OEP の probe でも扱うか（U6）。ch32rv と一緒に後で。

## 7. 確認済み事実

- UART の OEP のフレームは COBS + CRC-16/CCITT-FALSE、0x00 区切り（oep-core §3.1）。probe はフレームの途中で 200 ms 途切れたら
  読み直す（同 §3.2）。
- host は同じ probe に複数の経路があれば vendor bulk、HID、CDC の順に試す。CDC で OEP を運ぶのはほかに手がないときに限る
  （oep-core §3.3）。
- bind（口に何を流すか）と describe の port は、今は USB の CDC の口だけが対象（oep-if-probe-config §1、§3。「USB に関わる項目は
  任意の群」）。boot_mode は保存が無いときや組めないときは probe が自分で選ぶ（同 §2）。
- 起動モードの切り替えと保存（NVS）は P4 で動いた（保存 2 ms、再列挙 1.4〜1.6 s。P4 の結果）。CDC の口へのコンソールの bind と、
  host の detach / reset を越えて続くことは X035 の治具で動いた（P6）。
- 経路ごとの速さ（X6）: vendor bulk 37 MB/s、CDC 8.2 MB/s、HID 0.84 MB/s（probe → host）。往復はどれも 1 ms 未満。
- CDC を 2 口以上持つと vendor のストリーミングが欠ける（X1）。
- Linux の cdc-acm は開くときに DTR / RTS を一度立てる（X1）。DTR は「開かれた」の目安にしかならない。
- コンソールの受信は位置付きのストリームで、読んでも消えない。読み手が増えても取り合わない。
- 同じ接続で DM の mailbox を使う方式（dmseq、dmdata、SDI）は 1 つだけ。
- classic ESP32 の probe は、DTR と RTS を両方 on で開くと再起動せず、両方 off にして開くと再起動した（2026-09-29、pyserial）。
  `arduino-cli monitor` の既定は dtr=on、rts=on。
- oep-client-python の COBS の読み手は、0x00 の前のバイトをフレームとして解き、失敗すると壊れた応答として送り直す（`link.py`
  `_recv` → `cobs.unframe`）。
- 今の classic ESP32 の probe（Esp32V003Probe）は Endpoint だけで、ProbeConfig（設定と保存）を持たない。P4 の X035 の probe は
  USB-Serial/JTAG で OEP を運び、コンソールの CDC は試作（Esp32P4X035ConsolePrototype）にある。
- 今の OEP の試験（`tests/manual/oep_*`）は、pytest に当たる Python のスクリプトが 1 つのセッションで書き込み・コンソール・
  キャプチャをすべて扱っている。書き込みは client の `ch32_flash`。
- ch32rv は WCH-LinkE / WCH-Link / ブートローダーに対応し、OEP の probe には対応していない。monitor の source は uart / sdi /
  dmdata / dmseq / rtt。

## 関連

[harness-requirements](harness-requirements.ja.md) / [harness-probe](harness-probe.ja.md) /
[harness-testing](harness-testing.ja.md) / [ch32rv-requests](ch32rv-requests.ja.md) /
[debug-output](debug-output.ja.md) / oep-spec の `probe-cdc-and-persistence.ja.md`、`session-and-exclusivity.ja.md`、
`oep-if-probe-config.ja.md`、`v1-open-proposals.ja.md` §7
