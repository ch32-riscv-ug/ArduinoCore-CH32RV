# タイマー基盤の再設計

文書状態: 合意済み方針・基盤実装中。TIM資源APIとPWM/tone/Servo移行は実装済みで、
SysTick/deferred timerと実機競合試験は未完了。

文書基準日: 2026-09-19

## 1. 目的と範囲

この文書は、時間計測、遅延実行、PWM、`tone()`、Servo、公開タイマー資源APIが
同じハードウェア資源をどう共有するかを定義する。特にTIMが2本しかない
CH32V003でも、競合が偶然のレジスタ上書きにならず、予測可能に動くことを目的とする。

コア内の機能だけで所有権を閉じない。外部ライブラリも同じTIM状態管理へ参加し、
空き・能力・現在の取得状態を確認して、安全な取得または明示的なtakeoverを選べることを
要件とする。

今回の範囲には次を含めない。

- UIAP版の`HardwareTimer`実装の移植
- EEPROM対応
- 利用者向け周期callback APIの最終的な名前とシグネチャの確定
- RTOSを前提とした同期、排他、コールバック実行

本コアはベアメタルのシングルスレッドを基本とする。ただし割込みハンドラは通常処理へ
割り込むため、「シングルスレッド」は排他や実行コンテキストを考えなくてよいという意味
ではない。

## 2. 確認済みの現状

### 2.1 Arduino時間基盤

`cores/arduino/wiring_time.c`はQingKe SysTickを1 kHzで割り込ませ、
`ch32_millis_counter`を1 msごとに加算している。

- `millis()`はこの32 bitカウンタを返す
- `micros()`はmillisカウンタと現在のSysTickカウントを合成する
- `delay()`はmillisカウンタを待ちながら`yield()`を呼ぶ
- `delayMicroseconds()`は`micros()`によるbusy wait
- TIM1、TIM2などの汎用タイマーは消費しない

したがって、分解能1 msでよいタイマーのためにTIMを追加消費する必要はない。

現状の`main()`は`loop()`の後に`serialEventRun()`を呼ぶが、`yield()`や
ソフトウェアタイマーのdispatchは呼ばない。`yield()`は`delay()`からだけ呼ばれ、さらに
sketchがweak定義を置換できる。このため、将来のdeferred callbackを`yield()`だけへ
依存させることはできない。

### 2.2 TIM利用者

現在は共通の所有権管理がなく、各機能がTIMレジスタを直接設定する。

| 機能 | TIMの使い方 | 現在の競合処理 |
|---|---|---|
| `analogWrite()` | pinに対応するchannelをPWM出力。PSC/ARRはTIM全体で共有 | 自分のstarted bitしか見ない |
| `tone()` | variant固定TIMのupdate割込みで任意pinをtoggle | TIMを再設定。以前の設定を認識しない |
| Servo | variant固定TIMのupdate割込みで複数pinを順次駆動 | TIMを再設定。以前の設定を認識しない |

CH32V003では次の割当になっている。

| TIM | PWM pad | 既定の排他的利用者 |
|---|---|---|
| TIM1 | PD2、PA1、PC3、PC4 | Servo |
| TIM2 | PD4、PD3、PC0、PD7 | `tone()` |

この「共有」は管理された共有ではない。たとえば`tone()`がTIM2のPSC/ARRを変更すると、
TIM2のPWM padにも異なる波形が出る可能性がある。終了時にもPWM設定は復元されない。

## 3. 実行コンテキストを三つに分ける

「ハードウェアタイマー」を一種類のAPIとして扱わず、分解能とcallbackの実行場所を
明示する。

### 3.1 SysTick ISR timer

分解能1 ms。既存SysTick割込みで期限を判定し、期限に達したcallbackを同じ割込み
コンテキストで直接呼ぶ。

- TIMを消費しない
- `loop()`の実行時間による遅延を受けない
- callbackから使える機能はISR-safeなものに限る
- callbackはブロックしてはならず、1 ms tickより十分短く終える必要がある
- 同時期限のcallbackは決められた順番で直列実行される

これはハードウェアSysTickを時刻源とする割込みタイマーであり、シングルスレッドの
通常処理へ割り込んで実行される。callbackから通常処理と共有する値には`volatile`だけで
なく、値の幅に応じたcritical sectionが必要になる。

最初は固定長slotを線形走査する案を第一候補とする。たとえば8 slotなら1 kHzでの
走査コストは小さく、期限heapや動的確保を導入するより故障解析が容易である。実測で
負荷が問題になった場合だけdelta listなどを検討する。

なお、現在のSysTickハンドラはカウンタを手動でゼロへ戻す。callback時間が長い場合の
tick欠落を正しく評価するため、提供前にauto-reloadの有無、割込み遅延時の経過tick数、
最悪実行時間をseriesごとに検証する。

### 3.2 TIM ISR timer

1 ms未満、正確な周期、output compare/input captureなど、TIM固有機能が必要な用途。
callbackはTIM割込みコンテキストで実行する。

- 分解能と周期はTIMクロック、prescaler、counter幅で決まる
- TIM全体またはchannelを取得する
- callbackにはSysTick ISR timerと同じISR制約がある
- PWM、capture、compareと基準カウンタ設定を共有できる場合がある

### 3.3 Deferred software timer

SysTickでは期限到達を記録するだけにし、通常コンテキストでcallbackを実行する。

- `loop()`の実行時間だけ遅延し得る
- callbackから通常のArduino APIを利用できる
- callbackの遅延は許容するが、期限を失わない
- 同じcallbackを滞留回数だけ実行するか、一回へまとめるかを選べる必要がある

dispatch位置は未決定である。少なくとも`main()`のloop境界が必要で、`yield()`だけには
依存できない。`delay()`中にも進めるなら、core内部dispatchを`delay()`の待ちループから
直接呼び、利用者がoverride可能な`yield()`とは分離する案が有力である。

```text
SysTick 1 kHz
  |- millisを加算
  |- SysTick ISR timer: 期限到達callbackを実行
  `- deferred timer: pendingを記録

通常コンテキスト
  |- setup/loop境界でpending callbackをdispatch
  `- delay待機中もcore内部dispatch（要決定）
```

## 4. TIMを管理する単位

TIMは一つの排他資源ではない。TIM全体に共通する資源とchannelごとの資源を分ける。

### 4.1 TIM全体の状態

- counter clockとclock domain
- prescaler、ARR/top、counter幅
- up/down/center-alignedなどのcount mode
- update eventとupdate割込み
- one-pulse、master/slave、triggerなど
- advanced timerのBDTR/MOEなど

TIM全体の設定が異なる要求は同時に成立しない。

### 4.2 channelごとの状態

- CH1からCH4のowner
- PWM/output compare/input capture mode
- compare/capture値とchannel割込み
- polarity、output enable
- padへのroute

base設定が互換なら、異なるchannelのPWMやcompare/captureは同じTIMを共有できる。

### 4.3 IRQの状態

seriesによってupdateとcapture/compareのvectorが共通の場合と分離される場合がある。
IRQ番号をTIM番号から推測せず、device dataから生成する。dispatcherはstatus flagを読み、
登録された内部handlerへ振り分ける。callbackは関数ポインタとcontext pointerで保持し、
`std::function`やheap allocationは要求しない。

## 5. 内部の所有権モデル案

最低限、各TIMについて次を保持する。base設定を複数利用者が共有できるため、
`base_owner`一つではなく、固定数のleaseとchannelごとのownerを管理する。

```cpp
struct TimerState {
    TimerBaseConfig base;
    TimerLease base_leases[MAX_TIMER_LEASES];
    uint32_t base_generation;
    TimerChannelState channel[4];
};
```

`base_generation`はTIM全体を再設定するたびに増やす。各channelにもgenerationを持たせる。
各利用者は取得対象のgenerationをleaseへ保持し、一致しなければ自分の設定が失われたと
判断する。これにより、`analogWrite()`のstarted bitだけが残り、再初期化されない故障を
防ぐ。

新しい要求が現在のprescaler、ARR、count modeと互換なら、base leaseを追加し、対象channel
だけを取得できる。他channelのPWMやcaptureは継続する。base設定の変更が必要ならTIM全体の
takeoverとなり、全base leaseと全channel leaseを無効化する。baseは互換だが対象channelだけが
競合する場合は、そのchannelだけをtakeoverし、他channelには影響させない。

取得要求は少なくとも次を表現する。

- TIM全体か、特定channelか
- 必要なbase設定または許容範囲
- IRQ種別
- 既存設定と互換な場合だけ共有するか
- 競合時に失敗するか、既存ownerを退去させるか

TIMのクロック、幅、channel数、IRQ topology、advanced/general/basic種別、pin routeは
生成されたcapabilityとし、`switch (timer)`を各機能へ重複させない。

## 6. 競合規則の提案

### 6.1 Arduino標準APIはlast caller wins

`analogWrite()`、`tone()`、Servoのように失敗を十分表現できない標準APIは、空き資源が
なければ既存ownerを退去させ、要求された機能を動かす。これはAVR系でも知られている
「tone/Servoを使うと一部のPWMへ影響する」というモデルに近い。

ただし「壊しつつ動く」は、未管理のレジスタ混在を許す意味ではない。退去処理は次の
順番で行う。

1. 対象TIMの割込みとcounterを停止する
2. 旧ownerの`quiesce` hookを実行する
3. 旧channel出力をdisableし、必要な出力pinを安全なlevelへ戻す
4. TIM全体を既知状態へ初期化する
5. generationを進める
6. 新ownerの設定で開始する

旧機能は自動復元しない。利用者が旧APIをもう一度呼べば、その呼出しが新しいlast
callerとなりTIMを再取得する。設定stackを復元する方式は、途中のpinMode変更や多重取得で
意味が曖昧になるため、初期設計には採用しない。

具体的な無効化規則は次を候補とする。

| 退去させられる機能 | 処理案 |
|---|---|
| PWM | channel outputをdisable。次の`analogWrite()`でbaseから再初期化 |
| `tone()` | IRQを止め、tone pinをLOWにし、再生中状態を解除 |
| Servo | 全servo pinをLOWにし、対象slotをdetach扱いにする |
| 明示的TIM API | lost callbackまたは状態照会で喪失を通知 |

Servoを外部要因でdetach扱いにすることが既存sketchへ与える影響は実機実装前に再検討する。
少なくとも`attached()`がtrueのまま波形だけ止まる状態は避ける。

### 6.2 公開TIM資源APIは取得方法を明示する

外部ライブラリもコアと同じTIM状態管理へ参加できる公開APIを提供する。UIAP版
`HardwareTimer`互換classではなく、TIM資源の能力照会、取得、解放、喪失検出を行う
小さなAPIを先に定義する。

取得規則を暗黙のoptionの組合せにせず、操作を二つに分ける案を第一候補とする。

- `tryAcquire`: 既存ownerと互換なら共有し、競合するなら何も変更せず失敗を返す
- `takeover`: 既存ownerを規定の手順で退去させ、常に新しいownerになる

標準Arduino APIは、戻り値で資源競合を表現できないため`takeover`を使う。外部ライブラリは
通常`tryAcquire`を使い、その機能に「既存出力を止めても動作開始する」という明確な意味が
ある場合だけ`takeover`を選ぶ。

初期設計では「このownerだけはtakeoverできない」というhard lockを追加しない。lockされた
TIMに対して`analogWrite()`や`tone()`を呼んでも標準APIには失敗を通知する方法がなく、
呼出しが黙って無効になるためである。全ownerが同じlast caller wins規則に従う方が単純で、
標準APIの挙動も一貫する。

取得成功時にはownerだけでなくgenerationを含むlease/tokenを返す。解放、設定変更、IRQ登録は
有効なleaseを持つ呼出しだけが行える。takeover後に古いlibraryが`release()`しても、新ownerの
TIMを止めてはならない。libraryはleaseの有効性を照会し、他機能に奪われたことを検出できる。

公開APIに必要な最小機能は次とする。

- TIM一覧とcapabilityの照会
- 特定TIM、任意の適合TIM、特定channelの取得
- `tryAcquire`と`takeover`
- leaseが現在も有効かの確認
- lease所有者による設定、IRQ callback登録、開始、停止
- lease所有者による解放
- takeover時に旧ownerのIRQ/channelを確実に無効化するcore側処理

APIの形を検討するための概念例を次に示す。名前と型は確定仕様ではない。

```cpp
TimerLease timerTryAcquire(const TimerRequest&, const TimerOwner&);
TimerLease timerTakeover(const TimerRequest&, const TimerOwner&);
bool       timerLeaseValid(const TimerLease&);
void       timerRelease(TimerLease&);
```

ownerは必要なら同期的な`quiesce` hookを登録できる。takeover側はTIMと対象IRQを止め、leaseを
無効化した後、新ownerのhardwareを開始する前にこのhookを一度呼ぶ。外部libraryがTIMの
channel以外のGPIOも駆動している場合、そこで安全なlevelへ戻し、library自身のrunning状態を
解除できる。PWM、tone、Servoも同じ仕組みを使う。

`quiesce`は資源管理APIへ再入してはならず、待機や通常のArduino callbackを実行しない短い
lifecycle hookに限定する。一般的なイベント通知callbackではない。後から通常コンテキストで
喪失を知るには`timerLeaseValid()`を使用する。これ以上の非同期通知機構は、必要性を示す
library例ができるまで追加しない。

これにより、標準Arduino sketchは「呼んだ機能が動く」を優先し、資源管理を行う高度な
sketchは競合を検出できる。

### 6.3 空きTIMの動的選択

pinに依存しない`tone()`とServoは、候補TIMのうち空いているものを先に使い、なければ
variantの優先TIMをtakeoverする案が有力である。CH32V003なら候補順は次のようにできる。

```text
Servo: TIM1 -> TIM2
tone:  TIM2 -> TIM1
```

一方、PWMはpin routeによりTIM/channelが決まるため自由には移動できない。

動的選択に必要な複数TIMのupdate vector handlerはcore側へ生成し、tone/Servoは初回実装から
「優先TIMを丁寧に取得、任意の空きTIM、最後に優先TIMをtakeover」の順で選ぶ。flash増加、
advanced timerの分割vector、実機上の競合は引き続き評価する。

## 7. 公開APIの二つの役割

公開APIは次の二つを分ける。

1. libraryがTIM状態管理へ参加するための資源API
2. sketchが周期callbackやcapture/compareを使うための利用者向けタイマーAPI

資源APIは外部library対応のため初期設計に含める。利用者向けclass APIは、資源管理層へ
既存のPWM、tone、Servoを移し、複数seriesで競合規則を検証した後に形を決める。

UIAP版`HardwareTimer`と同じclassを互換目的で持ち込まない。

その後、公開APIが必要なら次を比較する。

- 主要Arduino coreに近いclass API
- 小さなCH32固有API
- 1 msのSysTick timerとTIM timerを別classにするか
- ISR callbackとdeferred callbackを型または明示optionで分けるか

公開APIではcallbackがどのコンテキストで呼ばれるかを名前、型、文書の少なくとも二つで
判別できることを要件とする。

## 8. 検証項目

実装する場合、最低限次をhost test、compile test、HILで確認する。

- `millis()`/`micros()`の精度とwraparound
- SysTick ISR callbackの周期、jitter、最悪実行時間
- deferred callbackの遅延、順序、cancel/reschedule
- callbackが同じtimerを停止・再登録する場合
- PWM channel間の共有と、TIM単位の周波数変更
- PWM -> tone -> PWM再呼出し
- PWM -> Servo -> PWM再呼出し
- toneとServoの候補TIM選択とtakeover
- 16 bit/32 bit TIM、advanced/general/basic TIM
- 共通vectorと分割vector
- takeover中に割込みが発生する境界条件
- takeover後に古いleaseから停止・解放しても新ownerへ影響しないこと
- 外部libraryを模したownerとArduino標準API間の相互takeover
- 競合後に出力pinへ意図しないpulseが出ないこと
- CH32V003でのflash/RAM増加

## 9. 実装済みの決定と未決定事項

実装済みの資源APIは`CH32Timer.h`のC APIとし、固定長状態だけを使用する。device dataから
TIM種別、counter幅、channel数、register base、clock、update IRQをvariantへ生成する。
`tryAcquire`、`takeover`、generation付きlease、同期的quiesce、状態照会を公開し、hard lockは
設けない。PWMはchannel lease、tone/Servoはwhole-TIM leaseを取得する。

残る未決定事項は次のとおり。

1. SysTick timerのslot数と、未使用時にコード/RAMを完全に除去する方法
2. periodic callbackが遅延した場合にcatch-upするか、一回へまとめるか
3. deferred dispatchを`main()`、`delay()`、明示`poll()`のどこから呼ぶか
4. Servoがtakeoverされた場合はslotをinactiveにして`attached()`をfalseにする実装の実機確認
5. `analogWriteFrequency()`を公開するか。公開する場合はTIM上の全PWM channelへ影響する
6. 利用者向けtimer APIをC API、C++ API、または薄いC++ wrapper付きC APIのどれにするか
7. 利用者による直接レジスタ操作は管理対象外と明記するか、状態再同期手段を持つか

資源管理層は先行して検証を進め、SysTick/deferred APIは上記を決定してから実装する。

## 10. 利用者へ推奨する使い分けと完成後の文書

通常のsketchでは、まず`loop()`内で`millis()`との差分を確認するnon-blockingな処理を
推奨する。

```cpp
void loop()
{
    static uint32_t previous;
    const uint32_t now = millis();

    if ((uint32_t)(now - previous) >= 1000u) {
        previous += 1000u;
        // 1秒ごとの通常処理
    }
}
```

1 ms以上の周期処理には、多くの場合次のいずれかが使える。

- `loop()`内の`millis()`差分
- 状態機械
- deferred software timer
- peripheral自身の完了割込み

TIMを使うhardware timerは、sub-ms周期、低jitter、正確なedge、input capture、output compare、
通常処理が長くても期限を守る必要がある場合など、hardware timerが本当に必要な用途へ
限定して推奨する。単に一定時間後に通常処理を呼ぶためだけにTIMを占有しない。

タイマー実装と実機検証が完成した段階で、設計文書とは別に利用者向けガイドを作成し、
少なくとも次を詳しく説明する。

- `millis()`を使った推奨パターンとwraparound-safeな比較
- ISR callbackとdeferred callbackの違い
- ISR内で禁止・非推奨となるArduino API
- TIM/channelの選択とpin route
- PWM、tone、Servo、外部libraryの競合表
- last caller winsとtakeover後の状態
- library作者が公開資源APIへ参加する方法
- CH32V003のようにTIMが少ないdeviceでの設計例
- 問題解析のためのowner/lease状態表示方法
