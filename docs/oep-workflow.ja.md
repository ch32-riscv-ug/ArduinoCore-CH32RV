# OEP を含む開発ワークフロー（統括）

文書基準日: 2026-09-29。状態: **提案**（決定済みの項目は明記する）。

書き込み・モニタ・試験・波形の解析に関わる道具（ch32rv、OEP の probe と client、pytest、WireSkein）が増えたので、
「どの場面で、どの道具が、何を受け持つか」をこのリポジトリで 1 か所にまとめる。個々の仕様は各リポジトリの文書が正で、
ここはその間の分担と、まだ決めていないことを並べる。

## 1. 登場するもの

置き場は、このリポジトリのルートからの相対パス。

| もの | 置き場 | 受け持つこと |
|---|---|---|
| ArduinoCore-CH32 | このリポジトリ | core、ボード定義、試験（`tests/`）。この文書 |
| ch32rv | `../ch32rv` | 同梱の書き込みツール。WCH-LinkE / WCH-Link での書き込み・消去・reset・gdb・monitor |
| OEP の仕様 | `../../dev_oep/oep-spec` | probe と host の間のプロトコル（core と標準インターフェース） |
| OEP の probe | `../../dev_oep/oep-probe-arduino` | ESP32-P4、classic ESP32、RP2350 の probe の実装 |
| OEP の client | `../../dev_oep/oep-client-python` | Python の host 側。CH32 の書き込み手順（`ch32_flash`）も今はここ |
| WireSkein | `../../dev_oep/wireskein` | ロジックの記録の解析。試験の期待との照合（`ws verify`） |
| wch-protocols | `../wch-protocols` | 実測の台帳（読むだけ） |

## 2. 使う場面

| 場面 | 書き込み | 実行中の出力を見る | 波形 | OEP のセッションを握るもの |
|---|---|---|---|---|
| U1 利用者、WCH-LinkE | ch32rv | LinkE の CDC（UART / SDI）、ch32rv の monitor（dmseq など） | — | —（LinkE） |
| U2 利用者、OEP の probe（USB の P4 など） | OEP の書き込みツール | probe のコンソール専用の口（bind） | — | 書き込みの間だけ書き込みツール |
| U3 利用者、シリアルしかない OEP の probe（classic ESP32） | 同じポートで OEP の書き込みツール | 同じポート（§4） | — | 書き込みの間だけ書き込みツール |
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

## 4. シリアルしかない probe で、1 本のポートを OEP とコンソールで共用する（提案）

classic ESP32（USB-UART の変換チップ越しの UART 0 が 1 本だけ）を probe にして、Arduino IDE ではポートを 1 つ選ぶだけで
書き込みからモニタまでつながる形にする。

```text
 PC ──USB── CP2102 ──UART0── classic ESP32（probe）──SWIO / RVSWD── CH32（target）
                              │ OEP のフレーム → probe が処理
                              │ それ以外のバイト → target のコンソールへ（dmseq の入力、または UART）
                              │ target のコンソールの出力 → そのまま PC へ
```

### 4.1 設定は一度だけ、ブラウザから

- ブラウザの Web Serial で probe のポートを開き、OEP の `oep.probe.config` を読み書きする。設定するのは次の 3 つで、probe が
  保存する。
  - target と線（wire、ピン、target_id の照合）
  - ポートに流すコンソール（source: dmseq / fixture.uart / SDI、attach の仕方）
  - PC との間のリンクの速さ
- 設定のページは OEP の最小限（describe、session、probe.config）を話す JavaScript の client になる。Web Serial は Chrome / Edge
  だけが持つ。

### 4.2 共用の規則

- **PC → probe**: `0x00` で始まり `0x00` で終わるまとまりで、COBS を解いて CRC-16 が合うものを OEP のフレームとして扱う（UART の
  OEP のフレームは今も COBS + CRC-16、0x00 区切り。oep-core §3.1）。それ以外のバイトは、ためずにすぐコンソールへ送る。CRC が
  合わないまとまりは、受けたままコンソールへ送る（取り違えたときの逃げ道）。
- **probe → PC**: コンソールの出力は生のまま流し、OEP の答えは 0x00 区切りのフレームで送る。OEP の client は、フレームの外の
  バイトを捨てる（今の COBS の読み手も、0x00 までの不正なまとまりを捨てて合わせ直す）。
- **ロックを持つセッションがある間は、生の転送を止める。** コンソールの出力は probe の中の位置付きのストリームに残り、OEP の
  target.console で読める。セッションが終わると、生の転送を止めたところから再開する（probe のバッファからあふれた分は落ちる）。
  書き込みツールの書き込み中（U3）も、pytest の試験中（U5）も、ポートに流れるのは OEP のフレームだけになる。
- セッション中に PC から来たフレームの外のバイトは捨てる（そのとき OEP を話している相手は、生のバイトを送らない）。

### 4.3 これで成り立つ理由と限界

- テキストのコンソールには 0x00 が出てこないので、フレームの始まりと取り違えない。0x00 を含むバイナリをモニタから送ると
  取り違えることがあるが、CRC で弾いてそのまま転送する（そのまとまりの分だけ遅れる）。
- モニタを使う人は OEP のバイナリを流さない前提。
- **リンクの速さは固定。** USB-UART の変換チップは、PC 側で選んだ baud を ESP32 に知らせない。モニタの baud は、設定した
  リンクの速さに合わせる必要がある。target の UART の速さ（fixture.uart を流す場合）は別に設定する。dmseq には baud が無い。
- **DTR / RTS で probe が再起動することがある。** classic ESP32 の開発ボードの自動リセットの回路のため。2026-09-29 の実測では、
  DTR と RTS を両方 on で開くと再起動せず、両方 off にして開くと再起動した（ESP32 の起動のログ 325 バイト）。IDE のモニタと
  書き込みツールの開き方を揃える必要がある。実際の Arduino IDE 2 のモニタでは未確認。
- 取りこぼし: CP2102 は長い連続の送信でバイトを落とすことがある（v003 の治具で実測）。OEP のフレームは CRC と送り直しで守る。
  生のコンソールは守られない。

### 4.4 Arduino IDE での流れ（U3）

1. ボードを選び、ポートに probe のシリアルを選ぶ。書き込みの方法（programmer）に「OEP probe（serial）」を選ぶ。
2. 書き込み: 書き込みツールがポートを開き、OEP で attach → 書き込み → reset → `end`。この間、生の転送は止まっている。
3. モニタ: 同じポートを開く。セッションは終わっているので、target のコンソールが流れる。入力は target へ届く。

書き込みツールは、ch32rv の OEP 対応（目標）か、それまでの間の client の書き込み。`platform.txt` と `programmers.txt` への
登録が要る。

### 4.5 pytest（U5）

pytest は試験の間ずっとセッションを握るので、生の転送は止まり、ポートに流れるのは OEP のフレームだけになる。コンソールは
OEP の target.console で読み、キャプチャは fixture.capture で読む。**混ざらないように分けるのは「セッションの間は生の転送を
止める」規則**で、pytest 側に特別な分離は要らない。

### 4.6 USB の probe（P4）との違い

P4 は USB で口を複数出せるので、コンソール専用の口（bind）に流せば共用は要らない（U2）。同じ規則を P4 の制御の口に当てて
もよいが、必須ではない。

## 5. 仕様と実装に要ること

| 項目 | 置き場 | 状態 |
|---|---|---|
| 制御の口の共用（フレームの見分け方、セッション中は生の転送を止める、止めた後の再開） | oep-spec（oep-if-probe-config の bind か、core の経路の節） | 未提案 |
| USB を持たない probe の口の宣言（今は bind が USB の項目） | oep-spec（oep-if-probe-config §1） | 未提案 |
| リンクの速さの設定の項目 | oep-spec（oep-if-probe-config） | 未提案 |
| classic ESP32 の probe の共用の実装 | oep-probe-arduino（Esp32V003Probe） | 未着手 |
| Web Serial の設定のページと、JavaScript の OEP client | 置き場は未定（§6） | 未着手 |
| ch32rv の OEP 対応（書き込み・reset・monitor） | ch32rv（依頼は [ch32rv-requests](ch32rv-requests.ja.md)） | 未依頼 |
| programmer の登録（OEP probe（serial）） | このリポジトリ（`platform.txt`、`programmers.txt`） | 未着手 |
| pytest の書き込みツールの選択（`--writer`） | このリポジトリ（`tests/manual/oep_smoke`） | 未着手 |

## 6. 決めること

1. §3 の分担（ch32rv は書き込み・デバッグ・人のモニタまで、試験中は pytest がセッションを握る）でよいか。
2. §4 の共用の規則（0x00 で始まるフレーム、セッション中は生の転送を止める）でよいか。止めている間の生のコンソールを、
   再開時に流すか捨てるか。
3. リンクの速さを設定で選べるようにするか、115200 に固定するか。
4. Web Serial の設定のページと JavaScript の client の置き場（新しいリポジトリにするか、oep-client の中か）。
5. ch32rv への OEP 対応の依頼の範囲と時期。
6. デバッグ（gdb）を OEP の probe でも扱うか（U6）。

## 7. 確認済み事実

- UART の OEP のフレームは COBS + CRC-16/CCITT-FALSE、0x00 区切り（oep-core §3.1）。
- bind（口に何を流すか）は、今は USB の CDC の口だけが対象（oep-if-probe-config §1、「USB に関わる項目は任意の群」）。
- コンソールの受信は位置付きのストリームで、読んでも消えない。読み手が増えても取り合わない（probe-cdc-and-persistence §1）。
- 同じ接続で DM の mailbox を使う方式（dmseq、dmdata、SDI）は 1 つだけ。
- ch32rv は今、WCH-LinkE / WCH-Link / ブートローダーに対応し、OEP の probe には対応していない。monitor の source は
  uart / sdi / dmdata / dmseq / rtt。
- classic ESP32 の probe は、DTR と RTS を両方 on で開くと再起動せず、両方 off にして開くと再起動した（2026-09-29、pyserial）。
- 今の OEP の試験（`tests/manual/oep_*`）は、pytest に当たる Python のスクリプトが 1 つのセッションで書き込み・コンソール・
  キャプチャをすべて扱っている。書き込みは client の `ch32_flash`。

## 関連

[harness-requirements](harness-requirements.ja.md) / [harness-probe](harness-probe.ja.md) /
[harness-testing](harness-testing.ja.md) / [ch32rv-requests](ch32rv-requests.ja.md) /
[debug-output](debug-output.ja.md) / oep-spec の `probe-cdc-and-persistence.ja.md`、`session-and-exclusivity.ja.md`、
`oep-if-probe-config.ja.md`
