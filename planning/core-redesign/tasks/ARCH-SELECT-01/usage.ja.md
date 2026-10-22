# 選択案の利用者コードと状態の読み合わせ

2026-10-22、提案版1。[全体選択案](proposal.ja.md)のレビュー用。
**以下はAPI設計用の疑似C++で、コンパイル済みサンプルや新しい測定結果ではない。**
型名・関数名は仮。既存の実験API／goldenへそのまま置換する指示ではなく、利用者が持つ状態と接続の責務を示す。
実protocolのIRQ分担・時間成立は未検証。各APIの命令単位の実装を説明する資料ではない。

## K0：少ない処理は直接つなげる

コアが用意する入力frontendは、ISRとの同期を含むdelta取得と容量付き設定受理を担当する。
利用者は設定の適用と公開値を担当する。I2Cの読出しsnapshotを一貫させるのはfrontendの契約。
書込みを受理したことと、通常処理で適用したことを分ける。物理ACKの条件はslave backendが別途満たす必要がある。

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

同じ処理をBへ接続する場合は、進行元を次のように置き換える。両方を同時には使わない。

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

接続を一か所で宣言する例。実PDの必須処理をuser callbackへ遅延できるという仮定は置かない。

```cpp
Executor<4> app;
Work control{controlStep}, measure{measureStep};
Work display{displayStep}, input{inputStep};
Queue<Command, 4, InputSharing> commands;

void setup() {
    board.begin();
    pd.begin(); bus.begin(); inputDevice.begin();

    runtime.attachSystem(pd.service());  // 必要な通常側進行。任意のuser関数を呼ばない
    runtime.attachSystem(bus.service());
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
} // setup完了後にruntimeの自動進行を有効化

void loop() {
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

計測は期限→start→return→完了時take→公開の状態機械。
表示は最新snapshotを借用中bufferとは別に保持し、一回の呼出しで回収または一つの転送単位を進める。
転送中にviewが変わってもbufferを書き換えず、必要な次回描画を保持する。
長い計算は途中stateとcursorを持ち、一単位進めて再投稿する。callback中でdelay(0)を繰り返す形にはしない。

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
| loopの長い計算途中でdelay(0) | 明示参加workが動き、view等が変わり得る。呼出し前に共有する用途stateを一貫させる |
| control callback内でdelay(0) | 他workは動かない。正常に戻った後の巡を待つ |
| 同じworkを実行中にIRQがpost | 現在のcallbackは再入せず、次巡のreadyが残る |
| 表示がしばらく進まない | viewの旧状態は置換される。制御の完了結果やbufferは表示待ちにしない。ただし表示がbusを所有中なら他のbus要求はその解放を待つ |
| command queueが満杯 | 新commandを拒否。既存commandは保持し、入力側が表示・再試行等を選ぶ |
| 切断後もbackendが止まらない | validを落とし、旧bufferは貸出中のまま。timeoutだけで再利用しない |
| user workを自動参加から外す | 明示runまで進まない。そのworkの回収を待つ休止準備も進まない可能性がある |
| timerの期限を何回も過ぎる | 既定案では最新1回へ集約して起動。過去callbackを無制限に連続実行しない |

説明を読んだ専門家の正答だけで、初学者への教育効果を証明した扱いにはしない。
