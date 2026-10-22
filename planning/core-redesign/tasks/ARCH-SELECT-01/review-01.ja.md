# 第三者レビュー回答 01：全体構成の選択案

2026-10-22。[レビュー依頼](review-request.ja.md)の様式に沿う。レビューであり、採用決定ではない。

```text
レビュー対象版／commit：7996108（proposal.ja.md 提案版1、usage.ja.md、review-request.ja.md）。
  数値の出典は proposal が示す 0a8909e 以前の result 文書を読んだ。archive の再実行や
  host 検査の再現はしていない。
レビュー者・独立性：同じ repository の別セッション。planning/core-redesign 以下の文書・実験
  コードの作成には関与していない。製品コア（USB デバイス／ホスト、PD、Ethernet、CPU Clock
  メニュー）の作成者であり、下の「観測事実」の多くはその製品コードと実機ベンチからの引用。
  提案の作成には独立だが、製品コードの前提には独立でない。
推す全体案：B（条件付き）。ただし B の中心は「system service の進行を runtime が引き受ける」
  部分であり、user work の予約は任意部品のまま、教材の基本は A の step 列にする。
  根拠は F6。A へ戻す条件は R1 の結果（下記）。
全体評価：条件付き妥当。構造確定を止める指摘が 2 件（F1、F2）、公開前に解決すべきものが
  5 件（F3、F4、F5、F8、F10）。いずれも新しい実験より契約の明文化で解ける。
```

読んだ資料：proposal、usage、review-request、decision-focus、design-draft、EXP-06 k0／x0／sleep-work、
EXP-03 wait／dependency／reentry-depth／two-stack／completion-optional、EXP-01 i2c-handoff。
製品コード：cores/arduino/wiring_time.c（delay、SysTick の SystemInit 再実行）、
libraries/CH32RVUsbPd（irq と poll の分担）、libraries/CH32RVUsbDevice（ch32rv_usb_task）、
libraries/Network（poll でのクロック復元）、tools/generate/generate.py（V003 の march）。

## 指摘

### F1 setup 中の delay で何が進むかが表の中で矛盾する

- 対象：G2、Q3、Q8。proposal「G2」の表、行「loop外／loop中のdelay(0)」と行「setup」。
- 重大度：**構造確定を止める**。
- 根拠：論理上の矛盾＋観測事実。
  - 表は「loop外／loop中の delay(0)」で system service を一巡進めるとし、「setup」では「自動dispatchはまだ開始しない。明示runは…可能」とする。setup は loop 外なので、setup 中の `delay()` が system を進めるかどうかが二通りに読める。
  - 製品コアの `delay()` は場所を問わず `ch32rv_usb_task` / `ch32rv_usb_host_task` / `ch32rv_net_task` を回す（wiring_time.c:340-350）。X035 の USB CDC では `while (!Serial) delay(10);` が setup 内で列挙を待つ、Arduino の最も普通の書き方。setup 中の delay で system が進まない契約だと、この一行が永久に戻らない。
- 最小の反例：
  ```cpp
  void setup() { usb.begin(); while (!usb.ready()) delay(10); }   // 列挙は system service の進行が要る
  ```
- 修正案：行を次のように分ける。「setup を含む loop 外の delay(0)／delay(n)：begin 済みの system service だけを進める。user の自動進行は setup が戻った後に始まる。明示 run は binding 済み対象に可能」。「constructor 中は不可」はそのまま。
- 影響：usage の X0（`pd.begin()` の後に待たない）は変わらない。G2 の「自動進行を使わない構成は hook を外せる」は system 側にも適用されるか明記が要る（USB を使わない構成で delay の中の 3 回の null 判定を残すかどうか。製品は weak 参照で解いている）。
- 追加検証：不要。契約の選択。

### F2 「同じ object を二つの進行元で動かさない」が、進行を伴う照会 API と両立しない

- 対象：G1、G3、Q2。proposal「推奨する責務と依存方向」末尾の太字、usage X0 の `runtime.attachSystem(pd.service())` と `controlStep` の `pd.tryStart` / `request.tryTake`。
- 重大度：**構造確定を止める**。
- 根拠：論理上の反例＋観測事実。
  - 製品の PD ドライバは「Every public call runs it（poll）, so a sketch waiting on ready() alone still reaches a contract」（CH32RVUsbPd.cpp:14-15）。`ready()` も `request()` も状態機械を進める。これは Arduino の `Serial.available()` 等と同じ流儀で、K0 の「少ない処理は直接つなげる」が成り立つ理由でもある。
  - 提案どおり `pd.service()` を system に binding したうえで、controlStep が `pd.tryStart()` / `request.tryTake()` を呼ぶと、同じ object が runtime（service）と利用者の呼出しの二つから進む。禁止事項に抵触する。
  - 逆に照会・try を「進めない純粋操作」にすると、binding しない K0 流の sketch は `input.service()` を loop で呼ばない限り何も進まず、usage の K0 直接例（`applyStep()` だけ）が成立しない。
- 最小の反例（提案の規則どおり書いた場合）：
  ```cpp
  runtime.attachSystem(pd.service());
  void controlStep() { if (pd.ready()) ... }   // ready() が進めるなら二重進行、進めないなら表現どおり
  ```
- 修正案：禁止の対象を「二つの進行元」から「再入」に変える。「object の進行は冪等で非再入。service() も公開呼出しも同じ guard を通り、どちらから進んでもよい。binding は『誰がいつ呼ぶか』だけを足す」。これなら K0 直接例も X0 binding 例も同じ object で成り立ち、二重保守（直接経路と work 経路の 2 実装）も要らない。再入 guard の費用（flag 1 個と分岐）は小さいが R2 で数える。
- 影響：G3 の「登録と状態確認の競合」は再入 guard の下で考えればよくなる。proposal の「強い反対理由」2 点目（両経路の保守費用）はこの修正で主に消える。
- 追加検証：不要。契約の選択。

### F3 止まらない backend の貸出に、解放へ至る経路が一つも書かれていない

- 対象：G3、Q4。proposal「G3」の「取消要求やtimeoutだけでは貸出を解放しない。止まらないbackendでは回収不能として保持し、成功したように続けない」。
- 重大度：**公開前に解決**。
- 根拠：観測事実＋論理。
  - 止まらない backend は実機で普通に起こる。I2C は slave が SDA を Low に保持すると master の STOP が出せず、9 clock の bus recovery か周辺機能の reset が要る。H417 の Ethernet では、デバッガの接続で ETH PLL が落ち、DMA が frame の途中で止まった（この repository の docs/network-stack.ja.md、driver は poll() でクロックを戻す）。
  - 提案の規則では、この状態の record と buffer は「回収不能として保持」され、取消も timeout も解放しない。V003 の 1 buffer 構成では新規受理が永久に Busy になる。抜け道は MCU のリセットだけで、「WDT は独立した任意の復旧手段」という位置づけと矛盾する。
- 最小の操作順：`start` 受理 → slave が SDA を保持 → `cancel`（受理されるが実停止しない）→ 保持のまま → 以後すべて Busy。
- 修正案：G3 に第三の状態遷移を足す。「backend の reset／再初期化：hardware を reset（RCC の peripheral reset、必要なら pin の回復手順）して静止を確認した時点で、その backend の全貸出を Terminal(Aborted) にし、buffer を返却する。世代番号は reset で進める。これは通常完了以外で貸出が解放される唯一の経路」。利用者が呼ぶ明示操作とし、自動では行わない。
- 影響：G5 の「時間・保存停止」（クロック不連続）と同じ通知経路を使える（F4）。i2c-handoff の `close_backend` は Queued／Running があれば拒否する設計なので、その上に「強制 reset」を別操作として置く。
- 追加検証（R4 の最小範囲）：X035 の I2C で、Running 中に RCC reset を掛けて DMA／IRQ が buffer に触れなくなることを観測する。触れなくなるなら reset を解放経路に採用。周辺が pad を駆動し続けるなら GPIO 側の回復を reset の前段に足す。

### F4 クロック領域の不連続は休止だけでなく、デバッガ接続で起こる

- 対象：G5、Q6。proposal「G5」の行「時間・保存停止」と「休止」。
- 重大度：**公開前に解決**。
- 根拠：観測事実。
  - WCH-LinkE は接続のたびに RCC を書き換え、元に戻さない。V203（CFGR0 0x0028000A→0x0034040A）、L103（millis が 8 倍速）、V006（HSI 24→PLL 48 MHz）で内側から測定済み（wiring_time.c:226-236）。製品コアは SysTick から `SystemInit()` をやり直し、USB／Ethernet の driver は自分の poll でクロックを確認し直す。
  - 提案は不連続を sleep の文脈でだけ扱う。実際にはベンチで最も頻繁に起こる不連続はこれで、期限の信頼性（「sleep後の経過が不明なら期限を成功扱いせず再設定を要求」）と同じ扱いが要る。
  - 今日、CPU Clock のボードメニューを入れた（9c8824a）。実行中に変える API は置かず取得だけなので、提案の「clock 変更」は「ビルド時の選択」と「外的要因による不連続」の二つだけでよい。
- 修正案：G5 に「clock domain changed」を一般の system 通知として置く（user へは配達しない）。到達条件は sleep 復帰、外部からの RCC 書換えの検出、backend reset（F3）。古い domain の期限は成功にしない。実行時のクロック変更 API は対象外と明記。
- 影響：Time（時刻源）と Port の境界に、検出の責務（SysTick の 1 register read）が入る。
- 追加検証：不要。

### F5 V003 には原子命令がなく、「ISR 共有に適合する」部品はすべて IRQ 禁止区間になる

- 対象：G4、Q5。proposal「G4」の「単独・ISR共有・core間の同期能力を区別する」、usage K0 の「ISRと競合しても更新を失わない取得」。
- 重大度：**公開前に解決**。
- 根拠：観測事実＋未測定の指摘。
  - V003 は `rv32ec_zicsr`（generate.py:73）。A 拡張がないので lr/sc も amo もなく、ISR と共有する更新は mstatus/csr の割り込み禁止でしか守れない。X035 以上（rv32imac）は amo が使える。H417 の core 間は HSEM。
  - k0-result は「通常処理の途中へ実IRQを入れた検査ではない」「8 byte copyのatomicityとwire上のACK／NACKは残る」と明記しており、ISR 共有部品の正しさと費用（禁止区間の長さ）は未検証。
  - 製品の PD は GoodCRC を IRQ から送る（tTransmit 内）。通常処理側の長い IRQ 禁止区間は、この種の IRQ の遅延になる。
- 修正案：G4 の「文脈別の同期」に、系列ごとの実装手段を名指しで書く（rv32ec：IRQ 禁止区間／rv32imac：amo か lr/sc／H417 core 間：HSEM）。ISR 共有の部品は、禁止区間の最大長を公開値にする。
- 追加検証（R3 に含める）：V003 で ISR 共有の Queue／Flag に実 IRQ を重ねて壊れないこと、禁止区間の長さを測る。長さが PD／USB の IRQ 遅延許容を超えるなら、共有部品の粒度を小さくする（8 byte copy を禁止区間の外に出す等）。

### F6 X0 の work 本体はすでに A の guard を持っている。B が足すものを限定して書くべき

- 対象：G1、Q1。usage の `controlStep`、proposal「G1」の「X0例はworkへ接続する」。
- 重大度：後段の改善（ただし教材の基本形に関わる）。
- 根拠：論理。
  - usage の `controlStep` は冒頭で `if (!connected || inFlight || !applyPending) return;` と、無条件に呼ばれても安全な形で書かれている。これは A の step そのもの。B はこれに「変化がないときは呼ばない」「周期で起こす」「delay 中に進める」を足すだけで、本体は一文字も変わらない。
  - B に固有の価値は user work ではなく、(i) ライブラリの system service を利用者が `.service()` を書かなくても進めること（製品コアはすでに weak 参照 3 本でこれを行っている）、(ii) 休止判定が「残っている work があるか」を知れること（sleep-work-result：「SでUser jobをdelayから進めなければ、そのjobを残した休止準備も進まない」）。
- 修正案：G1 の「X0例はworkへ接続する」を、「X0 の基本例は loop から 4 step を呼ぶ A 形＋system 自動進行。周期・起床の集約と休止が要るときに同じ step を work に載せる変形を示す」に変える。B の executor は任意部品のまま（提案と同じ）。
- 影響：proposal の「推奨の要点」の表現は変わるが、G2〜G5 の契約は変わらない。
- 追加検証（R1）：同じ X0 を A 形と B 形で書いた 2 枚を読者に渡し、実行場所・再投稿・満杯の予測を比べる。B 形の binding 一覧の方が予測を誤らせるなら教材は A 中心、誤らせないなら提案どおりでよい。

### F7 比較表は M profile だけで、system 自動進行を含む現実的な A の基準が見えない

- 対象：Q7。proposal「同等用途ロジックの費用」の表。
- 重大度：後段の改善。
- 根拠：観測事実の読み方。
  - 表は RV32E・LTO なし・明示 M。製品コアは delay の中で system を回しており、現実的な A は「A＋S」。k0-result は profile を含む全 24 組で R/A が一致したと書いているので S の行は存在する。M だけを見せると、B の増分のうち「system 進行の仕組み」の分が user work の費用に見える。
  - 「+24 B」は静的 RAM のみ。entry の frame は R/A 28 B、B 44 B で、呼出しごとの stack に 16 B 乗る。proposal は注記しているが、表の外にある。
- 修正案：表に S profile の行（R/A/B）と frame 列を足す。新しい測定は不要（既存 matrix から転記）。
- 影響：数値の結論（「数値だけなら A」）は変わらない見込み。

### F8 ライブラリが呼ぶ利用者 callback の実行許可を「user 配達」にすると、既存の普通の使い方が止まる

- 対象：G2、Q3。proposal「G2」の「ライブラリが利用者関数を呼ぶ部分もuser配達に分類し、実行許可と寿命を引き継ぐ」。
- 重大度：**公開前に解決**。
- 根拠：観測事実。
  - 製品の USB CDC は `ch32rv_usb_task`（delay／loop から）→ class → `_onRx(...)`（利用者の lambda）を呼ぶ（CH32RVUsbDeviceCdcSerial.cpp:310）。X035 の usb-peer 試験はこれで通っている。
  - 提案の分類だと、この lambda は「user 配達」で、user が自動参加していなければ delay 中には呼ばれない。`while (!Serial) delay(10);` の間に届いた受信の callback が setup を抜けるまで配達されない、という挙動になる。
- 修正案：「user 配達」の意味を二つに分ける。実行**許可**（いつ呼ばれるか）は配達する system service の参加に従う。callback **本体**の規則（dispatch しない、delay(0) で yield できない、長い処理は状態を残して返す）は user callback の規則に従う。寿命は登録した object の寿命に従う。
- 影響：G2 の表は変わらない。system service の文脈で user コードが走ることを教材で明示する必要がある（F1 の setup とも整合）。

### F10 system service の登録は begin() で自動にする案を比較対象に入れる

- 対象：G2、Q3。proposal「G2」の「systemの参加はdriverの有効化と接続で明示し、userの登録と自動参加は別操作にする」、usage X0 の `runtime.attachSystem(pd.service())`。
- 重大度：公開前に解決（利用者の優先事項に直接関わる）。
- 根拠：利用者の意向（2026-10-22 のレビュー時の対話）＋観測事実。
  - 利用者が B に求めている第一の価値は「呼び忘れると更新されない処理（PD の maintain 等）が初期化で自動的に登録されること」。user work の予約ではない。
  - 製品コアの USB はすでに `begin()` で weak 参照にタスクが入り、利用者は何も登録しない。PD だけが `maintain()` の呼出しを要求していて、そこが不満の出どころ。
  - 提案の明示登録（`attachSystem`）では、`attachSystem` の書き忘れが今の `maintain()` の呼び忘れと同じ結果になり、目的を半分しか果たさない。
- 修正案：system service は `begin()` が runtime へ登録することを既定にし、外す手段（`begin()` の引数、または `detachSystem()`）を別に置く。「何が裏で動くか」は setup を読むのではなく、runtime の一覧を照会できる API（診断）で見せる。user work の自動参加は提案どおり明示のまま。
- 影響：G2 の「自動進行を使わない構成はhookを外せる」は、ドライバを `begin()` しないか明示的に外すかの二通りになる。費用は変わらない（使わないドライバはリンクされない）。
- 追加検証：不要。

### F11 単一の dispatch 点を使って、work ごとの実行時間とアイドル率を任意の診断として出す

- 対象：G1、G4、Q1。proposal「G2」の「取り逃した回数を観測可能にする案」、design-draft の「診断：有効化した統計・状態／未使用時のログbuffer・計測tickを常設しない」。
- 重大度：後段の改善（ただし B を選ぶ理由の一つになるので、G1 の「提供する便利さ」に加えるべき）。
- 根拠：論理＋利用者の意向（FreeRTOS の run-time stats と同等のものが欲しい）。
  - 協調実行で実際に困るのは「どの callback が長いか」で、FreeRTOS の CPU 利用率よりも**work ごとの最長実行時間**が直接の答えになる。実行器が呼ぶ前後で時刻源（SysTick の CNT、`micros()` と同じ）を読めば、work ごとの累計・最大・回数、ready が無くて待っていた時間（アイドル）、1 巡の最長時間が取れる。
  - 費用は work 1 回あたり時刻読出し 2 回と加算、work ごとに 8〜12 B。有効化しなければ 0 B にできる（設計案の診断の方針どおり）。
  - A（loop の step 列）ではこの計測点が利用者のコードの中に散らばるので、同じ値を取るには利用者が各 step を自分で囲む必要がある。dispatch 点が一つにまとまることの具体的な利点で、Q1「B の便利さ」の抽象的でない根拠になる。
  - アイドル率は V006 の休止判断（どれだけ休めるか）にもそのまま使える。
- 限界：ISR の時間はその瞬間の work かアイドルに紛れる（FreeRTOS も同じ）。分けるには ISR 入口・出口の計測が別途要る。時刻源の無い最小構成では使えない。
- 修正案：G1 の提供する便利さに「任意の実行統計（work ごとの累計・最大・回数、アイドル時間、周期の取り逃し）」を加え、G4 の実行器に計測 hook の口を置く。常設にはしない。
- 追加検証：不要。費用は R2 の試作で計測 hook の有無を 1 条件足せば分かる。

### F9 今決めるものの区切り（Q8）

提案が挙げた 4 つの未確認（別 executor の非再入、system／user の実行枠、択一 binding と IRQ 通知の統合、optional 部品を外した残容量）は妥当。これに次を足す。

- 今決める（基礎契約）：F1（setup／loop 外の delay）、F2（進行の冪等・非再入、択一 binding の撤回）。
- 公開前：F3（backend reset による解放経路）、F4（clock domain changed の一般化）、F5（系列別の同期手段と禁止区間の公開値）、F8（配達の許可と本体規則の分離）、F10（system service の begin() での自動登録）。
- 後段：F6（教材の基本形。R1 の結果で）、F7（表の補強）、F11（実行統計の hook。G1 の便利さには今から加える）。

F2 を採ると、提案の「既存試作から確認が必要な差」3 点目「明示serviceとwork接続の択一binding」は確認項目から外れ、代わりに「再入 guard の費用と IRQ からの ready 消費の統合」になる。

## 指摘しなかった主要前提、レビューできなかった範囲

- 同意して指摘しなかった前提：core ごとに通常実行経路を一つ、callback からの再入禁止、独立 stack／RTOS を標準にしない（ADR-0006 と一致）、C を中心にしない、結果保持を正本にして通知を集約可能な起動要求にする、受理と成功を分ける、満杯を黙って上書きしない。いずれも製品コアの USB／Network／PD の経験と矛盾しない。
- Q5 の semaphore／mutex：「task 待機者を内蔵しない」「lock を持って callback 間を待たない」でよい。優先度継承や待機順を暗黙に持たせない点にも同意。必要な用途で書けない例は見つけなかった（F5 は手段の指定の問題で、公開範囲の問題ではない）。
- Q6 の外部 RTOS 境界：「一つの指定 task が core-local 進行を所有する adapter」でよい。追加の指摘なし。
- レビューできなかった範囲：H417 の transport と共有 executor の詳細、保存形式、教育効果（人による評価）、archive の再実行による数値の再現、読んだ 10 本以外の result 文書、PD／I2C の実機時間条件（R4 は資料照合の範囲で F3／F5 に限って提案した）。
- 製品コアの現状（delay が system を回す、照会が進行する、SystemInit 再実行）を観測事実として使ったが、それを新設計の制約にはしていない。指摘はいずれも「新設計の契約を書き切るための反例」として読んでほしい。
