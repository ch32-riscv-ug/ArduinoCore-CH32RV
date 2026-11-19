# Changelog / 変更履歴

Changes from 0.8.0 on. Write each user-facing change under `## Unreleased` as a pair of lines, `- (EN) ...` and
`- (JA) ...`; the release workflow moves them under the version and puts that section in the GitHub Release.
0.8.0 から後の変更です。利用者に見える変更を `## Unreleased` に `- (EN) ...` と `- (JA) ...` の組で書きます。リリースの
workflow がそれを版の節に移し、その節を GitHub の Release の本文にします。

## Unreleased
- (EN) PIOC (X035 / X033, V205, H41x): programs in the sketch's `.S` assembled by the core's clang (`<ch32rv_pioc.inc>`), loaded and driven by the new CH32RVPioc library (`begin()`, `command()`, `waitDone()`, `onDone()`, `loadSram()`, `setOpenDrain()`, `clockHz()`); examples PiocPulses, PiocWs2812, PiocUartTx, PiocUartRx, PiocSpiTx, PiocIrNec, PiocIrNecRx, PiocPulseIn and PiocOneWire.
- (JA) PIOC（X035 / X033、V205、H41x）: スケッチの `.S` に書いたプログラムをコアの clang で組み（`<ch32rv_pioc.inc>`）、新しい CH32RVPioc ライブラリで読み込んで動かす（`begin()`・`command()`・`waitDone()`・`onDone()`・`loadSram()`・`setOpenDrain()`・`clockHz()`）。例は PiocPulses・PiocWs2812・PiocUartTx・PiocUartRx・PiocSpiTx・PiocIrNec・PiocIrNecRx・PiocPulseIn・PiocOneWire。
- (EN) New libraries: CH32RVDac (triangle, noise, DMA-played samples and `playBoth()` on a timer's trigger), CH32RVEcdc (H41x's AES-128 / 192 / 256 and SM4, ECB and CTR), CH32RVGpha (H41x's graphics DMA: fill, converting copy, blend, palettes), Arduino_CAN (bxCAN), SoftPWM, CH32RVComparator and CH32RVOpamp on more families.
- (JA) 新しいライブラリ: CH32RVDac（タイマーのトリガで三角波・ノイズ・DMA のサンプル再生、`playBoth()`）、CH32RVEcdc（H41x の AES-128 / 192 / 256 と SM4、ECB と CTR）、CH32RVGpha（H41x の画像用の DMA: 塗りつぶし、形式の変換つきのコピー、合成、色の表）、Arduino_CAN（bxCAN）、SoftPWM。CH32RVComparator と CH32RVOpamp は対応の系列が増えた。
- (EN) ADC: CH32RVAdcStream on a timer or an external trigger (`beginTimer()`, `beginExternal()`), its analog watchdog, dual mode (`simultaneous()`, `beginInterleaved()`), and CH32RVAdcInjected (the injected group, with `simultaneous()` on two ADCs); arduino-esp32's `analogContinuous()`.
- (JA) ADC: CH32RVAdcStream のタイマー・外部トリガ（`beginTimer()`・`beginExternal()`）、アナログウォッチドッグ、デュアルモード（`simultaneous()`・`beginInterleaved()`）と、CH32RVAdcInjected（注入グループ。2 つの ADC の `simultaneous()` も）。arduino-esp32 の `analogContinuous()`。
- (EN) Serial: DMA buffers (`setRxBufferSize()` / `setTxBufferSize()`), `baudRate()`, IrDA, RS-485, multiprocessor (`setAddress()` / `writeAddress()` / `mute()`), the synchronous mode as an SPI controller (UsartSPI), and SoftwareSerial.
- (JA) Serial: DMA のバッファ（`setRxBufferSize()` / `setTxBufferSize()`）、`baudRate()`、IrDA、RS-485、マルチプロセッサ（`setAddress()` / `writeAddress()` / `mute()`）、SPI のコントローラとしての同期モード（UsartSPI）、SoftwareSerial。
- (EN) Timers: `analogWrite()` on every pad a timer channel reaches (remaps and CHxN), complementary PWM with asymmetric dead time, CH32RVPwmTimer / CH32RVPhaseShift / CH32RVSixStep, CH32RVPulseCounter, one-pulse, output compare, input capture and arduino-esp32's timer API.
- (JA) タイマー: タイマーのチャネルが届くすべての端子での `analogWrite()`（remap と CHxN も）、非対称のデッドタイムつきの相補 PWM、CH32RVPwmTimer / CH32RVPhaseShift / CH32RVSixStep、CH32RVPulseCounter、ワンパルス、出力比較、入力捕捉、arduino-esp32 のタイマーの API。
- (EN) SPI and Wire: `transferAsync()` by DMA, `setHwCs()`; Wire's 10-bit addresses, general call, controller and target at once, PEC and the automatic bus clear.
- (JA) SPI と Wire: DMA の `transferAsync()`、`setHwCs()`。Wire の 10 bit アドレス、General Call、マスターとスレーブの兼用、PEC、自動のバスクリア。
- (EN) USB: CH32RVUsbHost suspend / resume / remote wakeup, MSC with FatFS (CH32RVUsbHostMscFS), more host ports (V103, first-generation USBHS, split transactions); CH32RVUsbDevice DFU and the firmware drive; the Update library.
- (JA) USB: CH32RVUsbHost の suspend / resume / リモートウェイクアップ、FatFS の MSC（CH32RVUsbHostMscFS）、ホストの口の追加（V103、第 1 世代の USBHS、split トランザクション）。CH32RVUsbDevice の DFU とファームウェアのドライブ。Update ライブラリ。
- (EN) System: `setClockOutput()` (MCO), the window watchdog, HSI trim and RTC calibration, the RTC Clock menu (LSE), EXTI lines that are not pins (`attachExtiLine()` and friends), PVD on more families, `ch32rv_irq_set_priority()` and VTF slots (`ch32rv_irq_set_fast()`), HSEM / IPC and the V3F as a second core on H41x.
- (JA) システム: `setClockOutput()`（MCO）、ウィンドウウォッチドッグ、HSI の調整と RTC の校正、RTC Clock のメニュー（LSE）、端子でない EXTI の線（`attachExtiLine()` など）、PVD の対応の系列の追加、`ch32rv_irq_set_priority()` と VTF の枠（`ch32rv_irq_set_fast()`）、H41x の HSEM / IPC と 2 つ目のコアとしての V3F。
- (EN) CH32RVDma (memory-to-memory copy / fill, waiting or in the background with a callback), `ch32rv_irq_attach()` / `ch32rv_irq_detach()` (an interrupt handler attached at run time), the heap usable from both H41x cores, the Watchdog menu on H41x, and Stack Check's overflow caught by PMP at once where the core has it.
- (JA) CH32RVDma（メモリ間の写しと塗りつぶし。待つ形と、裏で動かしてコールバック）、`ch32rv_irq_attach()` / `ch32rv_irq_detach()`（割り込みのハンドラーを実行時に付ける）、H41x の両方のコアからのヒープ、H41x の Watchdog メニュー、PMP のある石での Stack Check のあふれの即時の検出。
- (EN) CH32RVComplementaryPwm keeps both outputs low from `begin()` to the first `write()`, which starts them on its duty; `hsemCount()` is 30 (channel 30 is the heap's lock).
- (JA) CH32RVComplementaryPwm は `begin()` から最初の `write()` まで両方 Low で、最初の `write()` がその duty で出し始める。`hsemCount()` は 30（チャネル 30 はヒープのロック）。
- (EN) Fixes: V003's ADC calibration (about 100 counts low), the USB host's resume timing, the RTC alarm after a stop on H417, H41x's 480K flash erase, SPI's NSS before the first transaction, a CDC / vendor receive ring smaller than a high-speed or SuperSpeed packet (it never received), quoted paths where a library name has a space, and more.
- (JA) 修正: V003 の ADC の校正（約 100 カウント低かった）、USB ホストの resume のタイミング、H417 の停止明けの RTC のアラーム、H41x の 480K の flash の消去、最初の転送の前の SPI の NSS、高速・SuperSpeed の 1 パケットより小さい CDC / ベンダーの受信の輪（何も受けなかった）、ほか。
