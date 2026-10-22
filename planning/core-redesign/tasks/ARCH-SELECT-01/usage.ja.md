# 選択案の利用者コードと状態の読み合わせ

2026-10-22、提案版4（レビュー04への修正提案）。[全体選択案](proposal.ja.md)のレビュー用。
**以下はAPI設計用の疑似C++で、コンパイル済みサンプルや新しい測定結果ではない。**
型名・関数名は仮。既存の実験API／goldenへそのまま置換する指示ではなく、利用者が持つ状態と接続の責務を示す。
実protocolのIRQ分担・時間成立は未検証。各APIの命令単位の実装を説明する資料ではない。
基本はA。通常通知とworkは明示呼出しだけで動く。
版4は、専用APIで登録したprotocol providerだけをsystem中で呼ぶ例外を提案する。
providerを使う場合はsetup／loopのdelay中にも利用者の関数が動く。採用は未決定で、全例でproviderが必要という意味ではない。
以下の基本例からBへの移行を要求しない。Bは末尾の応用比較として扱う。

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

K0の基本教材にはBを併記しない。この用途でworkを必要とする理由が出た場合だけ応用比較する。

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
    measurementDeadline.arm(clock.now(), MeasurementPeriod);
}
void loop() {
    if (startupFault()) return; // 成功済みdriverの停止・回収はfault方針で別途扱う
    inputStep();
    controlStep();
    measureStep();
    displayStep();
} // systemをAfterLoopでも進める。この例は利用者providerを登録せず、通常通知も自動配達しない
```

begin失敗は選んだ登録方式で起こり得る失敗を扱う。固定配列なら登録容量不足も含む。複数driverのbeginを一括transactionとはせず、先に成功したdriverの寿命も維持する。
enterStartupFaultはfault状態を保持する疑似名で、driverの即時破棄や全要求の成功を意味しない。
既定はsystem自動登録。依存除去・進行位置を選ぶ上級構成では初期化前にManualServiceを指定し、必要なdriverのserviceOnceを同じownerから順次呼べる。
そのserviceは通常通知をinline実行しない。専用登録したproviderがあれば通常service内で呼ぶ。非再入とIRQ同期は別々に満たす。

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
        return; // intentを保持。Aは次のloop、任意Bはavailableのwake等で再確認
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

## Aで単独のDeadlineを使う

Deadlineは時刻値と期限状態を保持する共通部品で、executor・中央timer表・callback登録を必要としない。
時刻源の実装費用は別にある。expiredの照会はdriverやuser処理を進めない。
次は「開始できた時点から次の周期を設定、遅れた分は追い掛けない」計測の例。固定位相が必要ならPeriodの位相・取り逃し規則を選ぶ。

```cpp
Deadline measurementDeadline;
Request measurementRequest;
MeasurementBuffer measurementBuffer; // 回収まで寿命を維持
bool measurementActive = false;

void measureStep() {
    Result result;
    if (measurementActive && measurementRequest.tryTake(result)) {
        measurementActive = false;
        publishMeasurementOrFailure(result);
    }
    auto now = clock.now(); // domain／有効性を含む疑似時刻値
    if (!measurementDeadline.validFor(now)) {
        markMeasurementStale();
        measurementDeadline.arm(now, MeasurementPeriod);
        return; // 時刻不明ならarmも未成立。古い期限を成功扱いしない
    }
    if (measurementActive || !measurementDeadline.expired(now)) return;

    auto r = bus.tryStart(measurementRequest, measurementBuffer);
    if (r == Accepted) {
        measurementActive = true;
        measurementDeadline.arm(now, MeasurementPeriod);
    } else if (r != Busy) {
        publishMeasurementStartFailure(r);
        measurementDeadline.arm(now, RetryInterval);
    }
    // Busyなら期限を残し、次のloopで再試行。同じ呼出し内でspinしない
}
```

不連続後の再設定はこの用途の方針。処理中要求の取消・復旧・回収はI/O契約で別に扱い、期限再設定で貸出を解除しない。
同じ期限部品で入力のpollingや再試行も書ける。共通Periodを使っても実行回数やbackoffの方針は用途が選ぶ。

計測は期限→start→return→完了時take→公開の状態機械。
表示は最新snapshotを借用中bufferとは別に保持し、一回の呼出しで回収または一つの転送単位を進める。
転送中にviewが変わってもbufferを書き換えず、必要な次回描画を保持する。
長い計算は途中stateとcursorを持ち、一単位進めて返る。Aは次のloopで続け、Bなら次巡へ再投稿する。callback中でdelay(0)を繰り返す形にはしない。

## setupのsystem進行と利用者callback

```cpp
void setup() {
    if (!usb.begin().ok()) { reportStartupFailure(); return; }
    while (!usb.ready()) {
        if (usb.failed() || startupDeadlineExpired()) { reportStartupFailure(); return; }
        delay(10); // systemを進める。readyは観測だけで汎用pollを呼ばない
    }
    // 受信callbackを登録する場合も、呼ぶのは明示の配達APIからだけ
}
```

列挙などの内部進行を通常の通知callbackに依存させない。providerを要する応答操作は次節の別契約で扱う。setup中の受信は有限bufferに保持し、容量超過の扱いを明示する。
setup中に受信データを処理したい用途は、明示read／take、または初期化済みuser配達の明示runを使う。
system serviceを登録したことだけを根拠に、受信lambdaをsetup中のdelayから呼ぶことは推奨しない。
この違いは既存Arduinoコードの無変更移植より、実行位置の明示を優先する選択として再レビューする。

`while (!usb.ready()) {}` は通常serviceを一度も進めない。通常側の列挙処理が要る構成では待ち続ける。
IRQだけで完了する操作まで必ず停止すると一般化しないが、照会の繰返しを汎用の進行手段にはしない。
setupを抜けた後も、受信をread／takeするか、libraryの明示配達APIを呼ばなければuser処理は進まない。

通知callbackを使う場合の基本例は、共通の配達入口を一行だけ呼ぶ形にする。

```cpp
void setup() {
    initializeApplicationState();
    if (!usb.onReceive(onRx).ok()) { enterStartupFault(); return; }
    // 通知登録の成功に、共通の配達元への接続成功を含む。その場では呼ばない
    if (!usb.begin().ok()) { enterStartupFault(); return; }
}
void loop() {
    if (startupFault()) return;
    runtime.deliverUserOnce(); // 登録済み通常通知を有限に配達。Bのexecutorは不要
    controlStep();
    delay(0);                 // この配達を代行しない
}
```

onRxは短く処理し、配達・runへ再入しない。複数のlibraryへ通知を登録しても配達呼出しは一つ。
callbackを使わずread／takeするだけなら配達登録も共通一覧も不要。個別配達APIを残す場合も同じ通知状態を消費し、共通配達と二重に取り出さない。
一括配達を呼ばなければ通常通知は進まず、保持容量の制約が残る。callback登録解除は配達参照がなくなってから行う。

## 選択が必要な例外：応答を作るprovider

これは版3にはなかった例外。相手への返答に必要な短い操作を、利用者が専用APIで委任した場合だけsystem中で呼ぶ。
以下は事前に準備したreportを応答へコピーする設計例で、現在のUSB APIや全protocolの対応を示すものではない。

```cpp
ReportSnapshot report; // begin前に有効にし、provider解除・参照終了まで保持

ProviderResult provideReport(ReplyBuffer& out) {
    return out.copyBounded(report); // 長さ検査を含む有限コピー。準備不足・失敗も返す
}
void setup() {
    initializeReport(report);
    if (!usb.hid.setSystemProvider(provideReport).ok()) {
        enterStartupFault(); return;
    }
    if (!usb.begin().ok()) { enterStartupFault(); return; }
    while (!usb.ready()) {
        if (usb.failed() || startupDeadlineExpired()) { enterStartupFault(); return; }
        delay(10); // 要求が来ればproviderが呼ばれ得る。通常通知は呼ばない
    }
}
void loop() {
    if (startupFault()) return;
    updateReportWithoutYield(report); // 途中でdelayせず、整った状態を公開する
    delay(10);                       // ここでproviderからreportが参照され得る
}
```

要求がなければproviderを呼ぶ必要はない。providerの登録そのものやbegin途中では呼ばない。
上の例は単一通常ownerとコピー型応答を仮定する。IRQ／他coreもreportを書く場合や借用型応答なら、別の同期・寿命契約が要る。
MSCの媒体provider等でも実行文脈は同じだが、block操作や書込み完了が短く終わるとは限らない。
その場合は対応する遅延応答を使うか、未準備／失敗を返す。Pendingの汎用提供や、返った時点での転送buffer解放を仮定しない。
providerからrun／配達を呼ばず、delay(0)で他処理を進めようとせず、完了までspinしない。
provider用stateの更新中や必要なlockを保持中にdelayしない。通常通知をproviderからinlineで呼ばない。

providerを登録しない案では、内部backend／事前公開データで応答するか、対応機能の明示request／response処理を利用者が回す。
「相手が待っている」という理由だけで任意のcallbackを自動実行する許可にはしない。
通常処理が戻らなければproviderも呼ばれず、いずれの案も相手のtimeout内の応答を一般には保証しない。

## 応用比較：同じstepを任意のBへ接続する

基本のA例で困らなければ追加しない。以下は同じ用途状態を使い、起動条件を集約する便益と接続負担を比較する資料。
同じstepの無条件呼出しと併用する指示ではない。実PDの必須処理をuser workへ遅延できるとは仮定しない。

```cpp
Executor<4> app;
Work control{controlStep}, measure{measureStep};
Work display{displayStep}, input{inputStep};

void setup() {
    // board／driver／用途状態の初期化はA例と同じ。失敗時はrunしない
    initializeLikeA();
    if (startupFault()) return;

    app.attach(control); // 成功で初回ready。ただしその場では呼ばない
    app.attach(measure);
    app.attach(display);
    app.attach(input);

    inputDevice.wakeOnChanged(input);
    commands.wakeOnReadable(control);
    pd.wakeOnConnectionChanged(control);
    request.wakeOnTerminal(control); // requestの結果はそのownerだけがtakeする
    pd.wakeOnAvailable(control);     // 起動のhint。資源予約や回収権ではない
    measurementRequest.wakeOnTerminal(measure);
    displayRequest.wakeOnTerminal(display);
    bus.wakeOnAvailable(measure, display); // 複数へのhint。tryStartのBusyは残る
    view.wakeOnChanged(display);
    app.wakeOnDeadline(measure, measurementDeadline); // Aと同じ期限。接続は一操作
    // polling入力ならその周期もここへ接続する
    // 全attach／wake接続の成否を確認し、失敗なら開始しない
}
void loop() {
    if (startupFault()) return;
    app.runOnce();       // user workが動く明示境界
    computeOneChunk();
    delay(0);            // system進行。appや通常通知は呼ばない（provider登録があれば例外）
}
```

疑似コードでは接続結果の個別処理を省略した。満杯・未対応接続を成功とみなしてよいという仕様ではない。
実行開始前に接続を完了し、失敗した接続は解除・参照回収してから対象を破棄する。
attachの初回readyは初期postを減らすが、以後の起動条件の不足を補うものではない。

wake接続は条件確認と通知登録の競合を扱い、その場でuserを呼ばない。ISRも状態更新と起動要求だけを行う。
通知を複数へ送れることと、要求結果のtake権が一人に属することは別。wakeOnTerminalも結果の正本を消費しない。
この例でもstep内の状態確認は残る。期限の接続はAと同じDeadlineを参照し、armによる再設定とdomain無効も再確認する契約とする。
期限を消費・再設定するのはstepであり、接続側が先に期限を変更しない。新しい条件や別の期限を追加する場合はB側の起動接続も見直す必要がある。
接続を容易にする具体API・storage方式は公開前に選ぶ。いま全種類のbind実装を先行させない。

Bをさらに評価するなら、休止が必要な用途でAと同じ停止・wake条件を用意して比較する。
readyが空でも、進行中I/O、未回収結果、次期限、wake競合を無視してWFIへ入らない。
Aでもこれらを明示的に集約できるため、Bの比較対象は集約の手間と追加費用になる。

## 任意の診断で読み取れるもの

診断を有効にした構成では、登録済みsystem／userと実行方針を照会し、service／workごとの回数・経過時間累計・観測最大を確認する。
「どの短い処理が実際には長いか」を調べる入口にする。計測点を持たない明示stepは自動集計されず、必要なら同じ任意の計測口で囲む。
callbackの計測には途中のISR時間も含まれる。観測最大は時間保証ではない。
readyなしの巡が多くてもloopが計算中ならCPUは休んでいない。空の巡、計測外時間、実休止時間は別に表示する。
診断API名・counter幅・費用は未確定。診断なしの構成へ時刻源や統計領域を常設しない境界だけを先に選ぶ。

## 同じX0をA／B／Cで選ぶ場合の差

| 項目 | 推奨A | 任意B | C |
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
| setup中のdelay | begin成功で登録済みsystemを進め、専用登録providerは呼ばれ得る。ManualServiceは対象外。通常通知／workは明示run・配達まで進まない |
| MSC等のproviderを専用登録し、beginが成功 | 要求処理の一部として通常system中で呼ばれ得る。通知用の一括配達を待つ必要はないが、応答時刻は保証しない |
| 通常通知を登録したが一括配達を呼ばない | 通常通知は配達されない。systemや登録済みproviderの許可は変わらず、buffer満杯は機能の規則に従う |
| 固定配列方式のbeginで登録枠が不足 | beginが明示失敗する。成功したように見せて内部進行だけ停止する挙動にしない |
| loopの長い計算途中でdelay(0) | systemが進み、登録済みproviderは呼ばれ得る。driver状態・受信buffer・providerが扱うstateは変わり得る。通常通知は配達しない |
| readyを照会するだけのspin | 通常serviceを進めない。通常側の完了処理が必要なら待ち続ける |
| control callback内でdelay(0) | 他workは動かない。正常に戻った後の巡を待つ |
| 同じworkを実行中にIRQがpost | 現在のcallbackは再入せず、次巡のreadyが残る |
| 表示がしばらく進まない | viewの旧状態は置換される。制御の完了結果やbufferは表示待ちにしない。ただし表示がbusを所有中なら他のbus要求はその解放を待つ |
| command queueが満杯 | 新commandを拒否。既存commandは保持し、入力側が表示・再試行等を選ぶ |
| 切断後もbackendが止まらない | validを落とし、旧bufferは貸出中のまま。明示復旧で静止確認→Aborted等の結果回収ができた場合だけ再利用。timeout／reset命令だけでは返却しない |
| 外部要因でclockの経過が不明になる | 関係する期限を無効として利用側へ状態公開する。device resetとtime domain変更を同一視しない |
| userのrun／配達を呼ばない | 通常user処理は進まない。その処理の回収を待つ休止準備も進まない可能性がある |
| timerの期限を何回も過ぎる | 既定案では最新1回へ集約して起動。過去callbackを無制限に連続実行しない |

説明を読んだ専門家の正答だけで、初学者への教育効果を証明した扱いにはしない。
