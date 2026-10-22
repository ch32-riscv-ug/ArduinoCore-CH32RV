# 選択案の利用者コードと状態の読み合わせ

2026-10-22、提案版2（レビュー01への修正提案）。[全体選択案](proposal.ja.md)のレビュー用。
**以下はAPI設計用の疑似C++で、コンパイル済みサンプルや新しい測定結果ではない。**
型名・関数名は仮。既存の実験API／goldenへそのまま置換する指示ではなく、利用者が持つ状態と接続の責務を示す。
実protocolのIRQ分担・時間成立は未検証。各APIの命令単位の実装を説明する資料ではない。

## K0：少ない処理は直接つなげる

コアが用意する入力frontendは、ISRとの同期を含むdelta取得と容量付き設定受理を担当する。
利用者は設定の適用と公開値を担当する。I2Cの読出しsnapshotを一貫させるのはfrontendの契約。
書込みを受理したことと、通常処理で適用したことを分ける。物理ACKの条件はslave backendが別途満たす必要がある。
このK0 frontendは、入力の取得・受信・読出し応答をIRQ／hardwareで進め、通常側は回収と公開だけを行う模型を前提とする。
全driverに通常serviceが不要という意味ではない。必要なbackendではbeginがruntimeへ登録する。ManualService構成では明示serviceが必要。
照会は状態を読み、tryはその操作だけを行い、背後で汎用pollを呼ばない案。共有の同期方式は別途検証する。

```cpp
KnobFrontend<1> input;             // 設定の保持は1件。ISR共有に適合する実装を選ぶ
KnobState state;

void applyStep() {
    state.add(input.takeDelta());  // ISRと競合しても更新を失わない取得
    Setting next;
    if (input.tryTakeSetting(next)) state.apply(next); // この回は最大1件
    input.publishSnapshot(state); // 読出し中のsnapshotを壊さない公開
}

void setup() {
    input.begin();                // この例の通常側の進行元はloopだけ
}
void loop() {
    applyStep();
    // 別の短い処理
}
```

この構成ではexecutor・timer・event busを参照しない。不要なcode／RAMが落ちることは最終buildで確認する。
利用者が毎回delta取得等を行う費用と、Bへ接続する費用は同じものではない。

同じ処理をBへ接続する例では、無駄な確認を増やさないようloopの呼出しを置き換える。別箇所から順次呼ぶこと自体を禁止する規則ではなく、同時実行・再入を禁止する。

```cpp
Work applyWork{applyStep};
Executor<1> app;
void setup() {
    input.begin();
    app.attach(applyWork, ExplicitOnly);
    input.bindChanged(applyWork); // 登録前後の状態も再確認する接続。生pointer保存だけではない
    applyWork.post();             // 初期snapshotも作る
}
void loop() {
    app.runOnce();
}
```

workを登録しただけではloop外／delayで呼ばれない。登録寿命と解除の規則は直接経路にはなかった追加概念。
K0ではBが必ず有利とは推奨しない。第三者には「この小用途にBを要求していないか」を確認してもらう。

## X0：制御・計測・表示・入力の進行をまとめる

利用者が持つ状態を先に固定する。

| 状態 | ownerと寿命 | 意味 |
|---|---|---|
| command queue | 入力→制御。固定容量 | 全件必要な操作。満杯では拒否を入力側へ返す |
| desired／apply intent | 制御。常設の用途state | 編集中の値と、確定操作で要求された値を区別 |
| session／in-flight session | 制御 | 接続変更を跨いだ入力・完了を新接続へ適用しない |
| request＋transfer buffer | 各I/Oのowner | start受理から停止・結果回収・参照終了まで保持 |
| confirmed／valid／stale | 制御・計測の状態 | 最後の成功値と、現在信頼できるかを区別 |
| view snapshot／revision | 用途の公開状態 | 表示用の最新状態。表示の遅れで制御完了を保持しない |
| rendering snapshot＋chunk | 表示 | 送信中bufferと描画途中のsnapshotを保持。最新viewへの更新は安全な境界で行う |

基本例はA＋system進行。下のcontrolStepと同じ処理を、利用者が決めた順に呼ぶ。
measureStepは期限とactive要求、displayStepはdirty／転送中、inputStepは入力の有無を確認し、することがなければ戻る。
これらの確認はBへ接続した場合にも必要で、通知が来たことだけで状態を決めない。

```cpp
void setup() {
    board.begin();
    // begin成功には、必要なsystem serviceの自動登録成功を含む
    if (!pd.begin().ok() || !bus.begin().ok() || !inputDevice.begin().ok()) {
        enterStartupFault(); return;
    }
    measurementDeadline.arm(MeasurementPeriod);
}
void loop() {
    if (startupFault()) return; // 成功済みdriverの停止・回収はfault方針で別途扱う
    inputStep();
    controlStep();
    measureStep();
    displayStep();
} // 有効化済みsystemをAfterLoop hookでも進める。user自動配達は登録していない
```

begin失敗は登録容量不足も含めて扱う。複数driverのbeginを一括transactionとはせず、先に成功したdriverの寿命も維持する。
enterStartupFaultはfault状態を保持する疑似名で、driverの即時破棄や全要求の成功を意味しない。
明示進行だけの構成なら初期化前にManualServiceを指定し、必要なdriverのserviceOnceを同じownerから順次呼べる。
そのserviceはuser callbackをinline実行せず、非再入とIRQ同期を別々に満たす。

```cpp
// 手動進行の代替構成。上の既定構成との併用例ではない
PdDriver<ManualService> pd;
void setup() {
    if (!pd.begin().ok()) { enterStartupFault(); return; }
}
void loop() {
    if (startupFault()) return;
    pd.serviceOnce(); // delay／AfterLoopからはこのdriverを進めない
    controlStep();
}
```

型引数は自動経路をbuildから外す意図を示す疑似API。除去できることは未測定。

次は同じstepをBへ接続する変形。上のloopと併用せず、比較して選ぶ。
接続を一か所で宣言する例。実PDの必須処理をuser callbackへ遅延できるという仮定は置かない。

```cpp
Executor<4> app;
Work control{controlStep}, measure{measureStep};
Work display{displayStep}, input{inputStep};
Queue<Command, 4, InputSharing> commands;

void setup() {
    board.begin();
    if (!pd.begin().ok() || !bus.begin().ok() || !inputDevice.begin().ok()) {
        enterStartupFault(); return;
    } // 必要なsystemだけ自動登録。user callbackの実行許可にはしない
    app.attach(control, AutoEligible);
    app.attach(measure, AutoEligible);
    app.attach(display, AutoEligible);
    app.attach(input, AutoEligible);
    runtime.attach(app);                 // 自動進行の対象一覧へ明示的に参加

    inputDevice.bindChanged(input);
    commands.bindReadable(control);
    pd.bindConnectionChanged(control);
    pd.bindCompletion(control);
    pd.bindAvailable(control);
    bus.bindCompletionToOwner();         // 完了した要求のownerを起こす固定接続
    bus.bindAvailable(measure, display);  // Busy後に状態を再確認する機会
    view.bindChanged(display);           // payload全件の配送ではなく、最新状態の確認
    periods.bind(measure, MeasurementPeriod);
    runtime.attachDeadlines(periods);      // 通常進行時に期限を確認。timerから直接配達しない
    // polling式の入力機器なら、その周期起動もここで宣言する

    input.post(); control.post(); measure.post(); display.post();
} // systemはbegin成功後のsetup内delayでも進む。user自動進行はsetup完了後

void loop() {
    if (startupFault()) return;
    computeOneChunk();
    delay(0);                            // systemと参加workが動く可能性がある境界
} // loop外にも有限の進行機会
```

bindingは条件確認と通知登録を競合に対して整合させる契約。callbackをその場で呼ぶ購読APIではない。
ISRは適合する共有経路で状態・queueを更新して起動を要求するだけで、ここに登録したworkを実行しない。
同一要求の完了はowner一つへ接続し、汎用fan-outで複数ownerにtakeさせない。
このsetup記述の量もBの費用。より短く書けるかより、変更時に接続漏れを見つけられるかをレビューする。

制御の処理単位は次のようになる。`tryTake`の成功は実停止・回収可能性の契約を含む。

```cpp
void controlStep() {
    reconcileConnection(); // detachなら新規受理停止、validを落とし、active要求をcancel

    Result result;
    if (inFlight && request.tryTake(result)) {
        inFlight = false;  // この時点までrequest/bufferは再利用しない
        if (result.ok() && connected && requestSession == session)
            publishConfirmed(result);
        else
            publishInvalidOrStale(result); // 旧接続の成功でvalidへ戻さない
    }

    consumeCommandsBounded(); // 古いsessionは破棄。desiredとapply intentを更新
    if (!connected || inFlight || !applyPending) return;

    auto r = pd.tryStart(request, buffer, requestedValue);
    if (r == Accepted) {
        requestSession = session;
        inFlight = true;
        applyPending = false;
    } else if (r == Busy) {
        return; // intentを保持。available通知で再確認し、同じcallbackでspinしない
    } else {
        applyPending = false;
        publishStartFailure(r);
    }
}
```

この例は一回のcommand消費件数や「複数applyをどう扱うか」を最終固定していない。
それらは用途の受理・保持契約として明示する必要がある。無条件に最新applyだけへ上書きしてよいという共通規則ではない。
再接続時も未回収の旧要求を先に回収し、自動再送の可否は用途側で決める。coreがPPS値を勝手に再適用しない。

復旧を要求する場合も、このcontrolStepの回収処理を使う。reset操作自体がinFlightをfalseへ変更してはならない。
backendがDMA／IRQ等の静止を確認して未終端要求をAbortedにし、tryTakeが成功してから貸出を解く。
復旧に失敗すればfaultと未回収を保持する。device世代が変わっても旧結果のtake経路は残す。

計測は期限→start→return→完了時take→公開の状態機械。
表示は最新snapshotを借用中bufferとは別に保持し、一回の呼出しで回収または一つの転送単位を進める。
転送中にviewが変わってもbufferを書き換えず、必要な次回描画を保持する。
長い計算は途中stateとcursorを持ち、一単位進めて再投稿する。callback中でdelay(0)を繰り返す形にはしない。

## setupのsystem進行と利用者callback

```cpp
void setup() {
    if (!usb.begin().ok()) { reportStartupFailure(); return; }
    while (!usb.ready()) {
        if (usb.failed() || startupDeadlineExpired()) { reportStartupFailure(); return; }
        delay(10); // systemを進める。readyは観測だけで汎用pollを呼ばない
    }
    // 受信callbackが必要なら配達方針と寿命を別に登録する
}
```

列挙などのsystem成立を利用者callbackに依存させない。setup中の受信は有限bufferに保持し、容量超過の扱いを明示する。
setup中に受信データを処理したい用途は、明示read／take、または初期化済みuser配達の明示runを使う。
system serviceを登録したことだけを根拠に、受信lambdaをsetup中のdelayから呼ぶことは推奨しない。
この違いは既存Arduinoコードの無変更移植より、実行許可の明示を優先する選択として再レビューする。

## 任意の診断で読み取れるもの

診断を有効にした構成では、登録済みsystem／userと実行方針を照会し、service／workごとの回数・経過時間累計・観測最大を確認する。
「どの短い処理が実際には長いか」を調べる入口にする。計測点を持たない明示stepは自動集計されず、必要なら同じ任意の計測口で囲む。
callbackの計測には途中のISR時間も含まれる。観測最大は時間保証ではない。
readyなしの巡が多くてもloopが計算中ならCPUは休んでいない。空の巡、計測外時間、実休止時間は別に表示する。
診断API名・counter幅・費用は未確定。診断なしの構成へ時刻源や統計領域を常設しない境界だけを先に選ぶ。

## 同じX0をA／Cで選ぶ場合の差

| 項目 | A | 推奨B | C |
|---|---|---|---|
| 起動条件 | loopで各stepを確認する、または用途側がflag／時刻を集める | 接続済み通知・期限がworkをreadyにする | eventの宛先・型・mailboxを定めてhandlerへ渡す |
| 保持する用途state | 上表のstateは必要 | 同左。workはstateの代わりではない | 同左。eventに詰めてもactive bufferや結果寿命は残る |
| 再試行 | loopの次の確認 | available／期限または次巡へ再投稿 | 再試行eventまたは状態通知 |
| 全件command | queueから有限件数回収 | queueから有限件数回収 | typed mailboxから有限件数回収 |
| 最新表示 | 最新状態をstepが見る | 最新状態＋集約起動 | 状態通知にするか、payloadをどう置換するかを選ぶ |

Aの意味上の弱点を作ってBを有利に比較しない。Aでも同じ所有・失敗処理と自動system serviceは実現できる。
Cでも状態通知を使える。比較対象は実装可能性ではなく、何を標準の組み立て方にするか。

## レビューで予測してもらう挙動

| 状況 | 推奨案で起きること |
|---|---|
| setup中のdelay | begin成功で自動登録されたsystemだけ進む。ManualServiceは対象外。user自動callbackは保留、明示runなら指定対象を進められる |
| beginの登録枠が不足 | beginが明示失敗する。成功したように見せて内部進行だけ停止する挙動にしない |
| loopの長い計算途中でdelay(0) | 明示参加workが動き、view等が変わり得る。呼出し前に共有する用途stateを一貫させる |
| control callback内でdelay(0) | 他workは動かない。正常に戻った後の巡を待つ |
| 同じworkを実行中にIRQがpost | 現在のcallbackは再入せず、次巡のreadyが残る |
| 表示がしばらく進まない | viewの旧状態は置換される。制御の完了結果やbufferは表示待ちにしない。ただし表示がbusを所有中なら他のbus要求はその解放を待つ |
| command queueが満杯 | 新commandを拒否。既存commandは保持し、入力側が表示・再試行等を選ぶ |
| 切断後もbackendが止まらない | validを落とし、旧bufferは貸出中のまま。明示復旧で静止確認→Aborted等の結果回収ができた場合だけ再利用。timeout／reset命令だけでは返却しない |
| 外部要因でclockの経過が不明になる | 関係する期限を無効として利用側へ状態公開する。device resetとtime domain変更を同一視しない |
| user workを自動参加から外す | 明示runまで進まない。そのworkの回収を待つ休止準備も進まない可能性がある |
| timerの期限を何回も過ぎる | 既定案では最新1回へ集約して起動。過去callbackを無制限に連続実行しない |

説明を読んだ専門家の正答だけで、初学者への教育効果を証明した扱いにはしない。
