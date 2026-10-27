# スケッチの組み立て方

割り込み、待たない書き方、ライブラリの callback を、このコアでどう組み合わせるかの手引きです。
Blink と Serial は動かせた、次に「ボタンを押しながら LED を点滅させ、USB からのコマンドも受けたい」
というところから読む想定です。

このコアは RTOS を持ちません。仕事の流れは 1 本で、`loop()` が何度も回るだけです。その上で
次の 3 つを守ると、小さな CH32V003 でも機能の多い CH32X035 でも同じ形でプログラムが書けます。
ESP32 や Raspberry Pi Pico の RTOS に移ったときも、同じ考え方がそのまま使えます。

1. **割り込みは合図だけ。** 処理は `loop()` で行う。
2. **待たない。** 「今は無理」なら、状態を覚えて `loop()` に戻る。
3. **自分の関数は、自分が呼んだ場所で動く。** ライブラリの callback は `deliverEvents()` で受け取る。

各例の配線と API の使い方は [サンプルスケッチ案内](examples.ja.md) から参照できます。

判断の理由は [ADR-0016](adr/0016-execution-model.ja.md) にあります。

## 1 回の `loop()` で起きること

```text
setup()
  └ USB / Network / USB PD の begin() が、自分の「裏の仕事」を登録する

loop()                ← スケッチの仕事。deliverEvents() を呼ぶとここで callback が動く
  ↓ 戻る
裏の仕事を 1 回ずつ   ← USB の列挙と転送、IP スタックのタイマー、PD の応答
  ↓
loop() ...

delay(ms) の間も、裏の仕事は回り続ける（callback は動かない）
割り込み（ボタン、タイマー、UART の受信）は、どこにいても割り込んで、すぐ戻る
```

- 裏の仕事（system service）は、ライブラリの `begin()` が登録します。スケッチは `task()` や `maintain()`
  を書かなくても、USB はつながり、PD は電圧を保ちます。
- 裏の仕事が回るのは、`loop()` から戻ったときと `delay()` の中です。`loop()` の中で `while` を回して
  待ち続けると、その間 USB は止まります。
- スケッチの関数（callback）は、裏の仕事の中では呼ばれません。動くのは `loop()` の中で
  `deliverEvents()` を呼んだときだけです。

## 学ぶ順番

上から順に動かすと、このページの内容を一通り手で確かめられます。

| 段階 | 例 | 分かること |
|---|---|---|
| 1 | [Blink](../libraries/CH32RV/examples/Blink/Blink.ino) | `pinMode()`、`digitalWrite()`、`delay()` |
| 2 | [SerialEcho](../libraries/CH32RV/examples/SerialEcho/SerialEcho.ino) | UART の入出力 |
| 3 | [Timing](../libraries/CH32RV/examples/Timing/Timing.ino) | `millis()` の引き算で時間を測る。桁あふれに強い書き方 |
| 4 | [DeadlinePeriod](../libraries/CH32RVSyncKit/examples/DeadlinePeriod/README.ja.md) | `delay()` を使わずに、2 つの予定を同時に進める |
| 5 | [PinInterrupt](../libraries/CH32RV/examples/PinInterrupt/PinInterrupt.ino) | `attachInterrupt()` と、ISR が書く変数の読み方 |
| 6 | [QueueFromInterrupt](../libraries/CH32RVSyncKit/examples/QueueFromInterrupt/README.ja.md) | ISR から `loop()` へ、値を取りこぼさずに渡す |
| 7 | [CriticalSection](../libraries/CH32RVSyncKit/examples/CriticalSection/README.ja.md) | 割り込み禁止区間。何を中に入れ、何を入れないか |
| 8 | [SerialEvents](../libraries/CH32RVUsbDevice/examples/SerialEvents/README.ja.md) | USB の callback が `deliverEvents()` でだけ動くこと |
| 9 | [BasicWork](../libraries/CH32RVTaskKit/examples/BasicWork/README.ja.md) | 仕事を「起動条件つきの関数」に分ける |
| 10 | [ButtonBlinkConsole](../libraries/CH32RVTaskKit/examples/ButtonBlinkConsole/README.ja.md) | ボタン、点滅、コマンドを 1 つのスケッチにまとめる |

以下のコード片の `LED` と `BUTTON` は、自分の板の端子に置き換えてください（`const int LED = PA2;` など）。
Generic のボードは石の系列であって基板ではないので、`LED_BUILTIN` を持ちません。

## 1. `delay()` の代わりに時刻を比べる

`delay(500)` は「500 ms の間、ほかに何もしない」です。LED を点滅させながらボタンも見るには、
「前に切り替えた時刻から 500 ms 経ったか」を毎回確かめます。

```cpp
void loop() {
  static uint32_t last;
  if (millis() - last >= 500) {   // 引き算で比べる。49 日後の桁あふれでも正しい
    last += 500;
    digitalWrite(LED, !digitalRead(LED));
  }
  // ここでボタンやシリアルも見られる
}
```

同じことを CH32RVSyncKit の `Period` と `Deadline` で書くと、意図が名前に出ます。

```cpp
#include <CH32RVSyncKit.h>

CH32RVSyncKit::Period blink(500);      // 500 ms ごと
CH32RVSyncKit::Deadline timeout;       // 1 回だけの期限

void setup() { timeout.arm(3000); }

void loop() {
  if (blink.due()) {                   // 遅れても、たまった分をまとめて実行しない
    digitalWrite(LED, !digitalRead(LED));
  }
  if (timeout.expired()) {
    timeout.disarm();
    Serial.println("3 s");
  }
}
```

`if (millis() > next)` のような大小の比較は、桁あふれの瞬間に間違えます。必ず引き算で比べます。

## 2. 割り込みは合図だけ

割り込み（ISR）は、`loop()` がどこを実行していても、その途中に割り込んで動きます。ISR の中で
`Serial.print()` や `delay()` を呼ぶと止まったり、ほかの割り込みを遅らせたりします。ISR は
「何が起きたか」を記録してすぐに戻り、処理は `loop()` が行います。

一番簡単な形は `volatile` の変数です。

```cpp
volatile uint32_t presses;             // ISR が書く変数には volatile を付ける

void onPress() { presses++; }          // 数えるだけ

void loop() {
  static uint32_t shown;
  const uint32_t now = presses;        // 1 回だけ読んでから使う
  if (now != shown) {
    shown = now;
    Serial.println(now);
  }
}
```

押した時刻のように、値を順番どおり全部渡したいときは `Queue` を使います。満杯なら新しい値は断られ、
`trySend()` が `false` を返します。ISR の中で待つことはありません。

```cpp
CH32RVSyncKit::Queue<uint32_t, 8> times;

void onPress() { times.trySend(millis()); }

void loop() {
  uint32_t t;
  while (times.tryReceive(t)) {        // 溜まった分を全部取り出す
    Serial.println(t);
  }
}
```

回数だけでよいなら `Notify`、「起きた」を 1 回だけ伝えるなら `BinarySemaphore`、バイト列なら `Ring` と、
用途ごとに部品があります。一覧は [CH32RVSyncKit](../libraries/CH32RVSyncKit/README.ja.md) にあります。

## 3. 割り込み禁止は、コピーの間だけ

32 ビットの変数 1 つは、1 命令で読めるのでそのまま読めます。64 ビットの値や、組で意味を持つ 2 つの変数は、
読んでいる途中で ISR が書き換えると、半分だけ新しい値になります。読む間だけ割り込みを止めます。

```cpp
volatile uint32_t seconds, ticks;      // ISR が 2 つ同時に更新する

void loop() {
  uint32_t s, t;
  {
    CH32RVSyncKit::CriticalSection cs; // この { } の間だけ割り込みを止める
    s = seconds;
    t = ticks;
  }                                    // ここで元に戻る（入れ子にしても正しく戻る）
  Serial.println(s);                   // 表示は外で
}
```

Arduino の `noInterrupts()` / `interrupts()` も使えます。ただし入れ子にならないので、関数の中から
呼ぶ部品では `CriticalSection` のほうが安全です。

割り込みを止めている間は、USB、USB PD、UART、`millis()` の割り込みも全部待たされます。

- 中ではコピーだけを行う。計算と表示は外で行う。
- 中で `delay()`、`Serial.print()`、待つループを使わない（割り込みを待つ処理は、そこで止まります）。
- ISR の中では割り込みはもう止まっているので、ISR 側に `CriticalSection` は要らない。

CH32RVSyncKit の部品（`Queue` など）は、この短い禁止区間を内側で自分で取ります。部品を使っている限り、
スケッチで割り込みを止める必要はありません。

## 4. ライブラリの callback は `deliverEvents()` で受け取る

USB の `onRx()` や `onKeyboard()` のような callback は、`loop()` の中で `deliverEvents()` を呼んだときだけ動きます。

```cpp
#include <CH32RVUsbDevice.h>

void onData(size_t available) {        // 受信の通知
  while (SerialUSB.available()) {
    SerialUSB.write(SerialUSB.read());
  }
}

void setup() {
  SerialUSB.onRx(onData);
  SerialUSB.begin();
}

void loop() {
  deliverEvents();                     // onData() はここで動く。ほかの場所では動かない
  // 自分の仕事
}
```

- 通知 callback は `deliverEvents()` で受け取ります。読み出しが遅いとクラスの buffer が埋まって相手側で待ちが
  発生する場合があります。通知の保存方法はクラスによって異なり、無制限に保存されるわけではありません。
  `delay()` の中でも USB の転送は進みますが、通知 callback は動きません。
- 自分の関数が `loop()` の途中に割り込んで動くことはありません。グローバル変数を callback と `loop()` で
  共有しても、ISR のときのような禁止区間は要りません。
- 同じ種類の通知が、届く前に何度か起きると、1 回にまとまることがあります（受信の通知、線の状態など）。
  そのときは、その時点の最新の状態で 1 回呼ばれます。
- 戻り値は、動いた callback の数です。

例外が 1 つあります。USB の相手が答えを待っているもの（MSC の `onRead()` / `onWrite()`、CCID の
`onApdu()`、Vendor の `onControlRequest()`、映像の `onFillPayload()`、DFU の `onDetach()` など）は、相手を待たせられないので、USB の処理の中から
すぐに呼ばれます。これらは短く書き、`delay()` を入れないでください。どれが該当するかは各ライブラリの
README にあります。

## 5. 仕事が増えたら、関数に分ける

点滅、ボタン、コマンド入力と仕事が増えると、`loop()` は `if` の並びになります。CH32RVTaskKit は、
1 つの仕事を「関数 + いつ動かすか」の組にして、`loop()` を 1 行にします。

```cpp
#include <CH32RVTaskKit.h>

CH32RVTaskKit::Executor app;
CH32RVSyncKit::Period every500(500);

void blinkStep() { digitalWrite(LED, !digitalRead(LED)); }
CH32RVTaskKit::Work blink(blinkStep);

void setup() {
  pinMode(LED, OUTPUT);
  blink.wakeEvery(every500);           // 500 ms ごとに動かす
  app.attach(blink);
}

void loop() {
  app.runOnce();                       // 動かす条件がそろった仕事を、1 回ずつ動かす
}
```

- 仕事（`Work`）は、待たずに戻る普通の関数です。自分の stack を持たないので、途中で待つことは
  できません。続きがある仕事は、状態を変数に覚えて戻ります。
- 起動条件は、ISR からの `post()`、`Deadline`、`Period`、条件の関数（`wakeWhen()`）です。
- 仕事が動くのは `runOnce()` の中だけです。ISR や `delay()` の中では動きません。
- `WorkStats` / `RoundStats` を付けると、どの仕事が何回、何 µs 動いたかが分かります。

使わなくても困りません。`loop()` に `if` を並べた書き方と、できることは同じです。

## よくある間違い

| 書き方 | 起きること | 直し方 |
|---|---|---|
| ISR の中で `Serial.print()` | 止まる、ほかの割り込みが遅れる | ISR は `Queue` に入れるか数えるだけ。表示は `loop()` で |
| ISR と共有する変数に `volatile` が無い | `loop()` が古い値を使い続ける | `volatile` を付ける |
| 64 ビットの値をそのまま読む | ときどき桁の合わない値になる | `CriticalSection` の中でコピーする |
| `noInterrupts()` の中で `delay()` | 永久に戻らない（`millis()` が進まない） | 禁止区間ではコピーだけ |
| `while (!flag) {}` で待つ | 待つ間 USB が止まり、ホストから見えなくなる | `loop()` に戻り、次の回でもう一度見る |
| `if (millis() > next)` | 49 日後の桁あふれで誤動作 | `millis() - last >= interval` |
| callback を登録したのに動かない | `deliverEvents()` を呼んでいない | `loop()` で `deliverEvents()` を呼ぶ |
| provider（`onRead()` など）で長い処理 | ホストがタイムアウトする | すぐ答えられる形にする |

## ESP32 / Pico に移るとき

同じ考え方が、RTOS では次の部品になります。ESP32 では [ESP32SyncKit](https://github.com/tanakamasayuki/ESP32SyncKit)
を使うと、名前もほぼそのままです。

| このコア | ESP32（FreeRTOS） | 変わること |
|---|---|---|
| ISR → `Queue::trySend()` | `xQueueSendFromISR()` / ESP32SyncKit の `Queue` | 受ける側が `receive(timeout)` で待てるようになる |
| `Notify` / `BinarySemaphore` | タスク通知 / セマフォ | 待つ版（`take(timeout)`）が使える |
| `CriticalSection` | `portENTER_CRITICAL()` | 2 コアなので spinlock が付く |
| `deliverEvents()` | ライブラリのタスク、イベントループ | callback は別のタスクで動く。共有変数に Mutex が要る |
| `Work` + `runOnce()` | タスク（`xTaskCreate()`） | 自分の stack を持ち、途中で待てる |
| `Period` / `Deadline` | `vTaskDelayUntil()` / タイムアウト | タスクが眠って待つ |

小さなマイコンで「待たない」「割り込みは合図だけ」に慣れておくと、RTOS でも待ち時間の短い、
取りこぼしのないプログラムになります。RTOS では待てるようになる分、待っている間に何が止まるかを、
自分で考える場面が増えます。
