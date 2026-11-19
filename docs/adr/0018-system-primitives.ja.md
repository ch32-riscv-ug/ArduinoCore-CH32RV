# ADR-0018: 実行時の割り込み、DMA、2 コアのヒープ、PMP の形

- Status: Accepted

2026-11-19 に実験した機能（ユーザー: 全部実験してから導入を検討）を、ほかのコアの使い方と比べて入れた形です
（2026-11-20、ユーザー: API もいろいろ使い方を検討してから推奨の形で入れる）。

## 決定

1. **割り込みのハンドラーは、実行時に `ch32rv_irq_attach(irqn, fn, arg)` / `ch32rv_irq_detach(irqn)` で付ける。
   ベクタ表は RAM に写さない。** リンクされたハンドラーが無いベクタは Default_Handler に来るので、crt0 の
   Default_Handler は `ch32rv_irq_unhandled` へ飛ぶ。何も付けないスケッチではそれが今までの無限ループ（weak）で、
   付けるスケッチでは ch32rv_irq_attach.c の振り分け（mcause の番号で表を引き、`fn(arg)` を呼ぶ）に置き換わる。
   リンクされたハンドラーがあるベクタ（表の値が Default_Handler でない）は false を返し、そのハンドラーが残る。
   枠は `CH32RV_IRQ_ATTACH_SLOTS`（8）。
2. **DMA の公開 API は CH32RVDma ライブラリのクラス。** `begin()` が、ほかが持っていない（`ch32rv_dma_claim()`）うえに
   割り込みを付けられる（1）チャネルを最後から探して取る。メモリ間の `copy()` / `fill()`（待つ）と
   `copyAsync()` / `fillAsync()`（裏で動かす）、`onDone(cb, arg)`、`busy()` / `wait()` / `abort()`。65535 単位を超える
   転送は割り込みから次の分を始める。全チャネルの表は variant の `CH32RV_DMA_CHANNELS`（生成）。
3. **H41x の 2 つのコアのヒープは、CPU Core = V5F のビルドで malloc の入口を `--wrap` で包み、ロックの中で呼ぶ。**
   ロックは、そのコアの割り込みを止めて HSEM のチャネル 30 を取る（入れ子は数えるだけ）。V3F を起こさないスケッチでは
   `ch32rv_cores_shared` が定数 0 になり、ロックは消える。`hsemCount()` は 30 になる。
4. **PMP は、Stack Check メニューの「Reset on overflow」の中で使う。** PMP のある核では、起動時（マシンモードの間）に
   予約したスタックの底の 64 B をロックした禁止の区域にし、そこへの読み書きの例外（mcause 5 / 7）を、今までの
   SysTick の確認と同じ PANIC のリセットにする。PMP の有無は CSR の読み戻しで決める。PMP を直接さわる公開 API は
   作らない。
5. **H41x の Watchdog メニューは、ほかの系列と同じ形で付ける。** LOADCODE の起動の経路（RAM に移ったあと）から
   `ch32rv_iwdg_check` を呼び、オプションバイトの書き込みでは書くたびに fence を入れる（ADR-0017）。

## 理由

- **1: 表を写さない方が軽く、どの系列でも動く。** 比べた形: mbed の `NVIC_SetVector()` と arduino-pico の
  `irq_set_exclusive_handler()` は表を RAM に置く。ESP-IDF の `esp_intr_alloc()` は引数つきの登録。STM32duino は weak の
  ハンドラーだけで実行時の登録が無い。表を RAM に写す形は実験で X035・V203・H417 で動いたが、1 KB 境界の RAM（V30x で 512 B
  ほど）を取り、ユーザーモードで動く系列では ecall で mtvec を書く必要があり、V00x は mtvec の基底の下位を捨てるので移せない。
  Default_Handler からの振り分けは、RAM を表の 8 枠（96 B）しか使わず、ecall も要らず、V00x でも動く。代わりに入るのに数命令
  多くかかるが、速さが要る割り込みは VTF（`ch32rv_irq_set_fast()`）がある。引数つき（`fn(arg)`）にしたのは ESP-IDF と同じく、
  1 つの関数でクラスの複数の実体を扱えるようにするため（CH32RVDma が使う）。
- **2: クラスの形は、実験で関数 1 つの形（`dmaMemcpy()`）より 520 B 大きかったが、裏で動かす形とコールバックと
  チャネルを持ち続けることは、関数の形では書けない。** 比べた形: Teensy の `DMAChannel` と Adafruit_ZeroDMA はクラス、
  pico-sdk は C の関数の組（取る・設定・始める・待つ）、ESP-IDF の `esp_async_memcpy()` は非同期とコールバック。
  Arduino のライブラリとして使う形はクラスが自然で、使わないスケッチには何も入らない。周辺への転送は要求の線が
  系列ごとに違い（決まったチャネル、H41x の DMAMUX）、各ライブラリ（Serial の DMA バッファ、ADC、DAC、SD_MMC）が
  それぞれの形で持っているので、公開のクラスはメモリ間から始める。
- **3: arduino-pico も malloc を `--wrap` してロックする。** picolibc にはロックの差し替え口（`__retarget_lock_*`）が無い。
  割り込みを止めるのは ESP-IDF のヒープと同じで、同じコアの割り込み処理の中の malloc も守れる。実験と試験
  （selfapi::test_cores_heap）で、ロックなしは壊れて V3F が止まり、ロックありは壊れ 0。V3F を使うスケッチで +396 B。
- **4: PMP の使い道で Arduino の利用者に効くのは、スタックのあふれを捕まえることだけ。** 0 番地はコードが走る領域
  （フラッシュの別名）なので、ヌルポインタの検出には使えない。arduino-pico（コア 1 のスタックの守り）や Zephyr
  （ハードのスタックの守り）も、使い道はこれ。既存のメニューの中に入れるので、新しい API もメニューも増えない。
  ロックするので、割り込み処理（マシンモード）のあふれも捕まる。PMP は QingKe V4B（V20x）と V3B（V205）、V2（V00x）に
  無く、そこでは今までの SysTick の確認だけで動く。
- **5: 実験で、h417qeu6-pico の V5F・V3F の両方から書き換えと戻しが通り、オプションバイトは元と 1 バイトも違わなかった。**

## 実装

- 1: `cores/arduino/ch32rv_irq_attach.c`、`ch32rv_priority.h`、`crt0_ch32.S` の Default_Handler。試験 selfapi::test_irq_attach。
- 2: `libraries/CH32RVDma`、`tools/generate/generate.py` の `CH32RV_DMA_CHANNELS`。試験 selfapi::test_dma。
- 3: `cores/arduino/ch32rv_heap_lock.c`、`generate.py` の `HEAP_WRAP_FLAGS`。試験 selfapi::test_cores_heap。
- 4: `cores/arduino/ch32rv_stack.c`（`ch32rv_stack_guard()`、HardFault_Handler）、`crt0_ch32.S`。2026-11-20 に X035・L103・
  V407・H417 で、再帰でスタックを使い切ると PANIC のリセットになり、PMP の無い V203 は SysTick の確認でそうなった（手で確認）。
- 5: `crt0_ch32.S`、`ch32rv_option_user.c`、`generate.py` の `IWDG_SERIES`。
