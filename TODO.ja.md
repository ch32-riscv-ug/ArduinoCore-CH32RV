# TODO

決まったことは ADR や各文書に移し、ここからは消します。

この TODO は個別の検討事項と予備調査を保持します。

クラス・主要動作モードの追加候補と、現行で対象外の機能の採否検討は
[追加機能の採否検討](FEATURE_CANDIDATES.ja.md) にまとめます。


## 未実装の機能（一番先にやる。2026-11-14 ユーザー）

機能は基本すべて作る。作らないのは特殊なもの（今は BLE だけ）で、ユーザーが決める。実装が無い・実機が無いを理由に外さない。
一覧と優先の案・試験の種類は [docs/unimplemented.ja.md](docs/unimplemented.ja.md)（184 行: 優先 1 が 47、2 が 126、範囲の判断が
10、除外 1）。確かめる機材が無いものも先に実装し、試験と配線の案を用意して「配線待ち」にする。

- [x] 2026-11-18 ユーザーの優先: HTTP / MQTT（外部ライブラリでよい）、USB ホストの MSC のマウント、LSE、SPI の DMA、有名な
  画像ライブラリが使えるか。CAN は優先低め。全部入れた: 画像ライブラリ（Adafruit GFX + ST77xx / ILI9341、SSD1306、U8g2、
  LovyanGFX、TFT_eSPI、LVGL）のビルドを通すため A0〜 / SDA / MOSI などを定数に、wiring_private.h、Arduino.h に ltoa・<cstdlib>・
  <initializer_list>（f663eb77）。SPI のバッファ転送の CPU ループと transferAsync（DMA、d7ea1a51、spi::test_spi_block）。LSE
  （RTC Clock メニュー、7b313e9a）。CH32RVUsbHostMscFS（0548a945、usb-peer-host-fs）。eth-libs（PubSubClient など、V407 / V208）。
- [ ] Adafruit NeoPixel は `ARDUINO_ARCH_CH32RV` の送信コードが無く `#error Architecture not supported`。動かすには Adafruit への
  寄稿（外に出す作業なのでユーザーに聞く）。
- [x] FS の FS_SMALL_FLASH は、小さい系列に加えて、選んだ品の書き込みの上限（`CH32RV_FLASH_SIZE`）が 64 KB 以下でも
  小さい設定になる（L103、V103、V203C8 など。libraries/FS/src/FSConfig.h。2026-11-20 に済んでいたのを確かめた）。
- [x] LSE: 水晶の無い板で LSI に戻る道は実機で見ていない（ベンチの V103 EVT・V203・nanoCH32H417 は全部水晶あり）。
  → 2026-11-20: selfsys の rtc が LSE か LSI かを返すようにした。常駐ベンチの v205rct6-pico と h417qeu6-pico は LSE が
  起動せず LSI に戻り、selfsys::test_rtc（3 秒でホストの時計と 10 % 以内）が通る（3.045 / 3.052 秒）。X305 / X315
  には LSE が無い（RTC_CTLRL の RTCSEL は LSI か HSE ÷512 だけ、CH32X315RM 6.3.2）。HSE ÷512 は休止で止まり 39062.5 Hz で割り切れない
  ので作らない。
- [x] SoftSPI: H417 の読み取りが 1 ビットずれる（2026-11-14 から）は、DUT ではなくプローブの spi-target が H417 の遅い SCK の縁を
  2 回数えていた。[oep] の試作（SCK にヒステリシス＋パッドのグリッチフィルタ、0.0.58+ef3d173.sckhysf）で 3 回とも全部 PASS
  （ヒステリシスだけは 3 回中 2 回）。正式な版は [oep] が入れる。V003 の softspi の peripheral の 1 ビットは、その後 3 回続けて
  再現しなかった。SoftSPI の試験の線は、SCK と CS を隣り合わせない選び方にした（a67c0bf0）。
- [ ] 優先の案をユーザーと決める。範囲の判断が要るもの（RTOS の port、EVT / SPL 互換、ベンダ IDE 互換、CH32F、無線 SoC
  を BLE の除外に含めるか、汎用ドライバの同梱、EVT のバイナリのライブラリ相当）はユーザーに聞く。
- [x] 文書の「対象外」「不要そう」「入れない」「対応しない」を未実装に直す（2026-11-18: network-stack、upload-routes、oep-workflow、
  project-scope（範囲の判断待ち）、peripheral-plan の C（後回し）、ADR-0017 の CFGCANM、FEATURE_CANDIDATES の注記。filesystem の
  littlefs / SPIFFS / 内蔵 Flash の「入れない」はユーザーの決定なので残した。ほかの該当は動詞の「入れない」など別の意味）（ただし、ユーザーが決めたもの — EEPROM.h と Preferences、
  内蔵フラッシュのファイルシステム、実行中のクロック変更、PIOC の保留 — はそのまま。2026-11-17）（peripheral-plan の C、software-peripherals、
  network-stack、filesystem、FEATURE_CANDIDATES、ADR-0017、upload-routes、usb.ja.md、CH32RVUsbHost の README など。
  docs/unimplemented.ja.md の元の棚卸しの 15 章）。peripheral-support の表は直した（2026-11-14）。
- [x] tools/generate/peripheral_matrix.py の STATUS が文書の手直しより古かった（2026-11-18）: 文書の表から STATUS を書き直し、
  HSE の欄（V4x7・M030・X3x5・H41x）を CELLS に、EVT の新しい GUI のフォルダを IGNORED に足した。生成すると
  docs/peripheral-support.ja.md と同じになる。文書の RCC の行の H41x の 200/50・25/25 MHz（外した項目）も直した。
- [x] coverage.toml に未実装の機能の項目を足す（`missing` の系列で「未実装」と出す）。2026-11-18: 周辺ブロックの単位で 17 項目
  （I3C・SWPMI・LPTIM・FSMC/FMC・SDMMC・DVP・LTDC・GPHA・SerDes・ECDC・DFSDM・SAI・PSRAM・OPA・CMP・PMP・DMA の公開 API）を足し、
  既存の CAN・I2S・QSPI に missing を入れた（I2S・QSPI は series に）。系列はクロックの名前から、OPA・CMP・PMP・DMA は
  未実装の一覧の系列を also で。周辺の中の機能（ADC の注入など）は未実装の一覧に任せる。

## テスト計画

計画は [tests/TEST_PLAN.ja.md](tests/TEST_PLAN.ja.md)、常設ベンチの案は [tests/BENCH.ja.md](tests/BENCH.ja.md)。

- [ ] **TST-01 現状の照合**: 計画・coverage・library 実装・runner・実行 artifact の不整合を一覧化する。
  各契約を再利用/作り直し/対象外候補に分け、理由を書く。文書上の実機確認と今回の未測定を混同しない。
  2026-10-29: test-coverage の「sleep/RTC API 未実装」「CDC/HID 等の sketch USB class 未実装」を直した。
  2026-11-14: 照合した（計 77 件）。直したもの: coverage の項目を 1 問 1 項目に（uart・spi・wire・softwire・synckit・
  selfsys を関数ごと、uart2 と route の二重数えをやめ、reset-reasons を足す）、system の watchdog と sleep を別の関数に、
  wire の取引の関数を test_wire_repeated_start に、coverage.py は系列ごとに最新の実行を出して前の最良を後ろに書く
  （回帰が見える）、DUT が「無い」と答えたスキップは `n/a:` で分母から外す、pytest の前に落ちた case は全項目の ✗、
  自己検査（selfcheck・selfbus・selfsys）は別の表。place.py は同じ相手の 2 つ目の口を not_chosen に書く。
  TEST_PLAN・tests/README・docs/test-coverage の古い記述（2026-11-05 の実機待ちの表、「新 suite では未整備」の列など）。
  残り:
  - [ ] USB の項目に controller（full_speed …）と対向（S3 / P4 / Pico / PC）の軸。junit に record_property で残し、
    coverage の欄を分ける。同じ相手の口が複数ある記録で、口ごとに case を流す。→ 2026-11-20: coverage.py に「USB, by controller
    and peer」の表を足した（places.json の記録から、case が流れた口の `<controller> -> <peer>`、ハブと SS だけの線も）。残り:
    同じ相手の口が複数ある記録で口ごとに case を流すこと（今は最初の口だけ流し、ほかは skip と書く。そういう記録の台はまだ無い）。
  - [ ] 計画にあって試験の無い契約: USB のコアとの共存（何との共存かを決めてから）。STALL の解除と相手のリセット後の
    再通信は 2026-11-20 に usb-ss-iso::test_usb_vendor_halt・test_usb_vendor_after_reset（PC の libusb、H417 の SS）を足して
    通った。FS / HS の DCD は PC につながる口が無く未確認（ピアの Pico / P4 にはホストの CLEAR_FEATURE とポートのリセットの
    口を足す必要がある）。Wire のデータの NACK・
    timeout・バスの回復・clock stretch は試験があり（wire / softwire の test_wire_data_nack ほか）、2026-11-20 に X035・V205 で
    wire・softwire とも、H417 で softwire が通った（H417 のハードの Wire は上の潰れたクロックの件）。
    → pulseIn / pulseInLong（signals::test_pulse_in）と shiftIn / shiftOut（softspi::test_shift）は 2026-11-20 に足して、
    X035・V205・H417 で通った。
  - [x] TEST_PLAN の「ベンチの X035C8T6 は I2C が使えない」と X035 の wire ✓ の食い違い（どの台で通ったか）、V205 の
    periph の serial_routes で通した route を結果から確かめる。→ 2026-11-20: TEST_PLAN にその記述はもう無い。V205 の
    serial_routes は通っていて、record_property の routes に通した組が残る（2026-11-19 の実行: PA6/PA7 で USART1 route 2・
    USART7 route 3、PA2/PA3 で USART2 route 2 など）。
- [ ] **TST-05 機材台帳と配線**、**TST-06 初回測定**（実機が要る）。

### フルテスト（2026-11-20、f90df6d3、H417 を除く 14 台）で残ったもの

直したもの: M030 の全 case がビルドで落ちた（既定の口の無い石の SDA / SCL、b7088f5c）、V003 の periph / selfcheck が 16 KB を
超えた（ecdd198b）、X315 の CCID の期待値（72377c2c）、V407 の PB1（板で PA7 とつながる。defect `pad:PB1`、`pad:` を
port_lines で除くように）。流し直して通ったもの（1 回だけの失敗）: V003 uart（IrDA / multiprocessor）、V006 selfapi の VTF
（81 対 80）、V205 / X035 signals、X035 softwire、V203RB usb-peer-host-input。

- [ ] **L103 の ADC が 0 V で約 1550 を読む**（l103c8t6-pico）: selfsys の adc_pads で、DUT 自身が出力 Low にした端子が全部
  1523〜1587（PA4 は 1190）、High は 4095。selfapi の millivolts（2069 mV at 1、0 at 0）・opamp も同じ原因に見える。2 回とも
  再現。ADC の CTLR1 / CTLR2・ADCPRE・校正の値を実機で読む。
  → 2026-11-21 の実機の調べ（scratchpad のスケッチ）: コードの退行ではない。同じ EVT の板で adc_pads が通った 11-19 の版
  （ad1e2cd）のコアでも、今は同じ形で落ちる。プローブは端子を駆動していない（浮かせて 0、プルアップで 1）。TSVREFE・BUFEN・
  CFG の FIFO_EN（EVT の例は入れる）・CPU 48 MHz・ADCPRE /2〜/8 のどれでも直らない。読みは「直前の変換の値＋今の入力」のように
  見える（Low の読み = 直前の ch17 の値、ch18 の VDDA/2 ≒ ch17 の値＋2048、High は 4095 で頭打ち）。ADC を切り入れして校正し直すと、
  0・11・2048・4095 に張り付く。CFG（0x50）は 0。端子をアナログのモードにして analogRead() すれば Low は 0 と読めるが、
  analogReadVref() は 237〜3041 とばらつく。11-19 から 11-20 のあいだに、板かチップの状態が変わったとみる。
  → 同じ日の続き: 足されているのは「直前の値」ではなく、校正（CAL）が書く一定のオフセットだった。TSVREFE を切った ch17（0 V）も
  約 1500 と読み、全チャンネルが「入力＋約 1500」。CAL をかけ直すたびにオフセットが変わり（1558 → 1175 → 1330）、RSTCAL だけでは
  消えない。ハブ 7 の port 3（3-2.3 の 3、プローブごと）を切り入れしても同じ。このチップの ADC の校正が壊れたとみる。
  コアは直さない。別の L103 で、同じ試験が通るかを確かめる（ユーザーにチップの交換を相談）。
- [x] **V103 の uart multiprocessor**: addressed / mute の区間で DUT が 1 バイトも受けない（2 回とも）。V103 では初めて流した。
  → 2026-11-21 も同じ（3 回目）。idle の区間（RWU だけ、WAKE 0）は通るので、受信と mute() は動き、アドレスで起きない。
  WCH の例（USART_MultiProcessorCommunication）の順の UE を立ててから RWU も試したが同じ（戻した）。次は DUT の CTLR1 /
  CTLR2 / STATR を muted のまま OEP で読み、アドレスの枠（8O1、9 ビット目 1）が来たときに RWU が落ちるかを見る。
  → 直した（2026-11-21）: setAddress() のあとの CTLR1 が 282E で、9 ビット語の M が立っていなかった（X035 は 382E）。V103 の
  USART は、TE と RE を両方 0 にした書き込みのあとでしか M を変えない（UE だけ落としても変わらない）。RWU も UE を立てた書き込みの
  次でないと立たない。モードの start() で TE / RE も落とし、RWU は別に書くように。V103・X035・H417 の uart が全部通る。
- [x] **V407 の uart-sync**: mode 1 / 2 で DUT の読んだ値が 1 ビットずれる（71bfc4 → e37f89）。mode 0 / 3 は通る。
  → 2026-11-21: port_lines が `pad:PB1` を除くようにしたあと、uart-sync・software-serial とも通った（PB1 が PA7 とつながる板のため）。
- [ ] **V407 の analog**: DAC と ADC の dual が ±50 カウントほど揺れて「hold steady」で落ちる。H417 の PA4 / PA5 に線を
  付けたときと同じ形（プローブの線の雑音か）。
  → 原因が分かった（2026-11-21、scratchpad の dac407）: ベンチの線どうしのクロストーク。DAC1 2048・DAC2 1024 を出して同じ端子を
  500 回読むと、DMSeq のモニターがつながっている間は ±80（1942..2154）、モニターをつながず RAM にためた間は ±4（2046..2054）。
  DUT が SerialDMSeq.begin() を呼んでいても、モニターの問い合わせ（SWIO、PA13 / PA14 の線）が無ければ静か。モニターを止めたまま
  プローブが隣の PA2 の線に 1 MHz の PWM を出すと ±300（1750..2342）。DAC の出力バッファの有無（BOFF）、プローブ側の線の
  プル（入力・プルダウン・プルアップ）では変わらない。Vrefint（±3）と線の無い PA3（±7）は静か。signals の PA1 の約 330 µs ごとの
  雑音も、同じ問い合わせの周期とみられる。直し方の案: (1) 測る間だけモニターの問い合わせを止める仕組みを [oep] /
  pytest-embedded の側に頼む、(2) DUT が測り終えてから答える形はすでにそうなので、問い合わせの間隔を測定より長くする。
  H417 の PA4 / PA5 の揺れ（下の項目）も同じとみる。
- [x] **V407 の signals**（pwm_timer・phase_shift が各 1〜2 NG）: PB1 を外した版（2026-11-21）でも残る。PA2 / PA1 で
  「窓の中で 2 回以上変わる」「rest が 0 のままでない」。analog の揺れと同じく線の雑音か、取り込みで切り分ける。software-serial は通った。
  → 直した（2026-11-21）: PA1 は DUT が駆動していない間、約 330 µs ごとに約 200 µs の細かい動き（浮いた線が拾う雑音。DUT が
  駆動すれば gpio は通る）。sweep() で動かす線にプローブのプルダウンをかけた（プローブの GPIO を試験が自分で持つ
  timer_trigger・complementary の break は gpio_taken で除く）。V407・X035・V006・H417 の signals が通る。
- [x] **UIAPduino の hid**: ブートローダーの USB（ハブ 3-2.4.2）が「setup address に応答しない」で列挙できない（11-20 19:31
  から 2 回とも。17:55 には列挙できていた）。プローブの線は PD3 / PD4 に無い。
  → 2026-11-21: 原因は oep_client の uiapduino.enter_bootloader() が NRST のあと 0.3 秒待ってから PREPARE_BOOT を走らせる
  こと。その間に書き込み済みのスケッチが setup() の resetReason() で PINRSTF を消し（0.18 秒では立っていて、0.48 秒では
  消えていた）、ブートローダーがすぐアプリへ移る。ホストは D- の固定プルアップの先のアプリに当たり、-32 / -71 のあと
  口を power cycle する（カーネルのログでは 10-30 からほぼ全滅、それ以前は通っていた）。run_hardware は同じ部品を待ちなしで
  呼び、b803 の検出をデバイス番号付きに（同じ口に出直したブートローダーを見落としていた）。hid が通る。[oep] が
  oep-client-python a713e5b で待ちを外した（未リリース）。出たら enter_bootloader に戻して確かめる。
- [x] **V003F4P6 の uart-dma**: 1 Mbaud の受信で 4096 のうち 3912 バイト・誤り 32（2 回目。1 回目は別の所で止まった）。
  → 2026-11-21: 誤り 32 はバッファあふれで、線は正しい（WireSkein ok）。1 バイトごとの available() が毎回 DMA の旗・USART の
  誤りまで見ていて、読み手が 1 Mbaud に追いつかなかった。available() は CNTR だけを読み、半分・終わりの点を過ぎたときだけ
  全部見るように。V003 で 2 回とも通り、X035・V006・H417 の uart-dma も通る。
- [ ] **M030**: run_hardware を `--env-file tests/.env` なしで起動し、defect が効かないまま logic が PA7（板が保持している
  端子）に PWM を数秒出した（2026-11-21）。基板の確認待ち。正しい記録で流し直すかは確認の後に決める。
- [ ] **1 台も走らない case**: pd（pd の記録の台が無い。機材が要る）。usb-peer 系は V103・V203C8T6・V307 に USB の線が無い。
  usb-peer-media は RP2040 の相手では走らず V407 だけ。

H417 の 2 台（2026-11-21、646c1824、h417qeu6-pico と nanoH417）: PASS 51・FAIL 7・SKIP 1。wire（既知の潰れたクロック）、
analog（PA4 / PA5 の線の揺れ、DAC2 の幅）、signals::test_complementary（break の resume()、上の既知）、usb-peer-firmware
（UF2 が 141 ブロック中 0、下の既知の項目の 4 回目）は既知のまま。新しく見えたもの:

- [x] **periph でプローブ e8f60ae84dc2 が再起動した**（01:28、test_adc_stream）: [oep] の調べで task-wdt。0.0.59+32bbd37 の
  PWM が、止めた時刻を 32 ビットの micros() で比べており、止めて 36 分以上たって使い直すと最大 36 分空回りする。[oep] が
  64 ビットに直した版（0.0.59+cca98b1）を 11 台に焼いた（2026-11-21）。そのあと periph は通った。
- [ ] **nanoH417 の sd のカードが応答しなくなった**: 全体の実行では SDIO の 6 件が通り、最後の test_sd_spi（SoftSPI）で
  「カードが応答しない」。その後は SDIO も 400 kHz・1 ビットのカードの初期化から通らない（内蔵 LinkE の口を 3 秒・10 秒切って
  も同じ。VIO18 は PWR_CTLR=0x0E00 で 3.3 V）。11-17 は同じ順で全部通った。カードの抜き差しをユーザーに頼む。
- [ ] **H417 の usb-ss-iso**: test_usb_ss_iso_out[5] が libusb_submit_transfer -1、そのあと CH32RV.restart() で DUT が USB から
  消えて戻らない（カーネルにも再接続が無い）。11-20 は restart 10 回とも SS で戻った。→ 2026-11-21 の流し直しでは restart・
  unplug とも通り、in[4]・out[5] の submit -1 だけが残った（下の「USBSS の等時 Mult>0」と同じ形）。restart で戻らないのは 1 回だけ。
- [x] **H417 の signals::test_six_step**: begin() 直後の全相オフで PA7（TIM1_CH1N）に細いパルスが 8 縁（High の割合 0.0）。
  隣の PA6（break）の線か、止めた相の出力か。
  → 2026-11-21: 3 回中 2 回（流し直しの 1 回は通った）。2 回目の取り込みでは PA7 のパルスが 10 MS/s の 1 サンプル（0.1 µs）で
  2 つ、約 600 µs 離れて出た。begin() は 6 本を数 µs でつなぐので間隔が合わない。同じ取り込みの別の区間で PE11 と PE13 にも
  同時刻の 0.1 µs が出ており、線か取り込みのひげに見える。次は 100 MS/s で幅を測る。
  → 50 MS/s で 2 回（6 本ではプローブの流しがあふれて途中まで）: PA7 のパルスは「high 0.0 µs」、つまり 20 ns の 1 サンプルに
  満たない。asymmetric_dead_time の PE13 も 50 MS/s で同じ 1 サンプル未満のひげ。DUT の出力ではなく数 ns の電気的なひげ。
  level / becomes が WireSkein で一定の幅より短いひげを除けるか（I2C の 50 ns の除外と同じ考え）を [wireskein] に相談する。
  同じ 2 回では、asymmetric_dead_time と phase_shift の「stopped」も落ちた: 止める命令が DUT で効くまで 4〜18 ms 遅れ、その間の
  波形が区間に入った（触っていないクラス。プローブは再起動していない）。1 回は ready() でモニターがつながらなかった。

### 常駐ベンチの全台試験（2026-11-13）で残ったもの

全台を流して見つかったものの多くは直した（コア: H417 の Wire が I2C1 を取って動かなかった、SS の Video の記述子の長さ、
V20x の standby が RTC で起きない、HardwareSerial の end() が端子を AF のまま残す。試験: WCH-Link の台の OEP の線の case、
デバッグの端子、隣の線のプル、SoftSPI の速さ、logic の burst、standby の端子の幅、Pico の HID の待ち）。残りは次のとおり。

  → 2026-11-21: ひげは 20 ns 未満で DUT の出力ではない（6 本を 50 MS/s で取ると P4 の流しが溢れて測れない）。[wireskein] の
  勧めで、判定でなく取り込みでプローブのグリッチフィルタ（P4 / P4X は 25 ns だけ）をかける。sweep() と ws_capture に
  glitch_filter_ns を足し、six_step だけ 25 ns。H417・V006・V205・X315 の signals が全部通る（WireSkein の記録に
  glitch_filter_ns 25、欠落 0）。
- [ ] **I2C のクロックの試験と文書（2026-11-14、ユーザー: WireSkein の精度が上がってから測り直して決める）**: 保証は「設定より
  速くならない」（ハードは CCR の切り上げ、SoftWire は半周期を詰めない。どちらも済み）。コアの上限の扱いは案 A（頼まれたとおり
  設定し、頭打ちにしない。ユーザー）。試験の案（速さの段 20k〜3.4M、測った SCL ≤ 頼んだ速さ × 1.03、上限の 5 分の 1 以下では
  90 % 以上、上限は試験の中で一番速く頼んだ SCL、取り込み 80 MS/s）は scratchpad の wire-clock-test.patch。今の WireSkein の
  scl_hz は跳ねを含む縁から数えるので、跳ねのある線では速く出る（[wireskein] に依頼済み）。測った値（線込み）: ハードは
  1 MHz まで取引が正しく、1.4 MHz 以上で壊れる台がある（設定の High が 250 ns 前後を下回ると。Fm+ の最小 260 ns とほぼ一致）。
  試した案 C（1 MHz で頭打ち＋High 260 ns 以上）は 6 台とも 3.4 MHz まで頼んで取引が正しかった（採らない）。SoftWire の上限は
  V003 約 102 kHz、V006 79 kHz、X035 66〜73 kHz、V103 172 kHz、V205 364 kHz、V203 537 kHz。
  → 6-2 の P4 を、取り込みに入力ヒステリシスが入る版（0.0.56+c9b12b2）にしたあと（2026-11-14 夜）: SoftWire の「速い」の測り違いは
  6 か所から 2 か所に減り、SoftWire の取引は 6 回とも正しい。ハードの Wire は 20〜100 kHz でまだ 5 か所「速い」と出て（WireSkein の
  scl_hz 待ち）、ハードの取引は 20 kHz でも 6 回中 4 回崩れる（H417 の保持時間 170〜200 ns と P4 の受け手の競り）。
- [ ] **常駐ベンチの I2C の線は、規格の立ち上がりを満たさない（2026-11-14）**: どの台も SCL / SDA を引き上げるのはプローブの
  入力プルアップ（P4、約 45 kΩ）だけで、立ち上がりが数 µs（規格は 1 µs 以内）。P4 の入力の High は VDD の 0.75 倍からで、
  DUT（シュミット）より高いので、ゆっくり上がる SDA を DUT は High、取り込みは Low と読み、縁の跳ね（50〜150 ns）も細かく
  取るほど写る。取り込みの基準どおり wire を 100 MS/s にすると、X035・V205 でも WireSkein の解析だけが崩れる（DUT の答えは
  正しい）。配線は変えない（ユーザー）。試験を「速さ」（100 kHz で DUT の出す SCL の周波数・High / Low の長さ・戻り値）と
  「取引」（線が落ち着く遅いクロックで、Sr を含む読み書きを WireSkein で復号。ひげの許容はベンチの跳ねの幅で決める）に
  分けた（2026-11-14 夜）: wire.output-only は 100 kHz で DUT の SCL の周波数と NACK、wire.repeated-start は 20 kHz で取引。
  取り込みは復号の T に対して 5 MS/s（25 倍）、ひげの判定は未検査（WireSkein が spike_max_s より短いパルスを捨ててから
  読むようになったら 100 MS/s に上げる。[wireskein] に依頼済み）。X035・V205・V203 の wire、X035・V205 の softwire が PASS。
  6-2（H417）は [oep] が使用中で未確認。H417 の短い保持時間（170〜200 ns）と P4 の競りは、遅くしても残る見込み。
  → WireSkein が I2C を規格の入力どおりに読むようになった（50 ns 未満のパルスを除き、遅い縁の何度ものまたぎを 1 つの縁と
  して読み、注意 slow_edge。[wireskein] main、未リリース）。リリースされたら tests の wireskein を上げ、wire を 100 MS/s に戻して
  全台で確かめる。6-2 の core-20261114T104917Z-ba291801 の 0x13 だけは、SCL の High がプローブのしきい値に届いていないとみられ、
  読めない（[wireskein] の tests/data/i2c_bench）。
  → クロックの試験（2026-11-14 夜、ユーザー: 速さを変えてハードとソフトのずれを見る）: wire.output-only が 20 / 50 / 100 / 400 kHz
  で SCL を測る。X035・V205・V003 の wire / softwire、V203 の softwire が PASS。V203 の wire だけ、SCL の縁の 50〜100 ns の跳ねを
  今の WireSkein が周期に数えて 22〜24 % 高く測る（新しい WireSkein の slow_edge の読みで直る見込み、リリース待ち）。
  → 2026-11-15: wireskein 0.0.28 に上げ、wire.output-only を 20 kHz〜3.4 MHz のはしご・100 MS/s にして 7 台で測り直した。
  クロックの判定（頼んだより速くない、上限の 5 分の 1 以下で 10 % 以内）は Wire・SoftWire とも全部通る（V203 の 22〜24 % も
  消えた）。3.4 MHz を頼むとハードの Wire は 6 台で転送が 5。ユーザーの判断: 絶対に動かない値だけエラー、設定できて動くか
  分からない値は今のまま。試験は 400 kHz まで成功を求め、それより上は記録だけ（925875b6）。残り: 6-2（H417）の取引は
  潰れたクロックで落ちる（上の項目）。
- [ ] **6-2（H417、PB11 / PB10、100 kHz）の wire が落ちる**: 2026-11-18 に、I2C の入力クロックを範囲内（HCLK 50 MHz、V5F 200 MHz、
  boards.txt を一時的に書き換え）にしても、試験の I2C を 100 kHz にしても、同じ 7 本が落ち、潰れたクロックも同じ割合で出た
  （範囲外の 100 MHz や速さは原因ではない）。CPU Clock の項目は EVT に無いので足さない（ユーザー）。Wire の README に注意を書いた
  （affad345）。外付けのプルアップが無いと、これ以上は切り分けられない。以下はそれまでの記録: DUT の I2C マスターは正常（2026-11-14 の見直し）。プローブの
  入力プルアップだけで、相手のいないアドレスへの送信を FREQ 60 / 50 / 40・SPEED 最速で計 1500 回、すべて正しく NACK（2）。
  落ちているのは受け手のプローブの側で、プルアップの弱い遅い立ち上がりに 50〜150 ns の跳ねが乗り（160 MS/s で確認）、P4 の
  i2c-target がそれを余分なクロックと数えて、Sr のあとのアドレスを取り違え、違うバイトを返して線を放さない（DUT は 5、
  preload_tx が unavailable）。2 MS/s の取り込みは 4.5 µs の SCL のパルスを落とすことがあり、「8 クロックで STOP」と見えた
  のはそれ（DUT の誤りではなかった）。配線は変えられない（ユーザー）。P4 の I2C の SCL / SDA のデジタルフィルタと、取り込みの
  glitch_filter を [oep] に依頼（P4 の内蔵プルアップは 45 kΩ の 1 種類、基板に切り替えの抵抗は無い、ファームでの押し上げは
  衝突の危険でやらない）。
  → 2026-11-14 夜: 375 ns のフィルタでも直らず、[oep] の結論は「線の立ち上がりが遅すぎる」（45 kΩ で規格の 1 µs を満たすには
  約 26 pF 以下が要る）。この台の I2C を 20 kHz に下げても同じ（遅い縁の雑音は残る）。配線を変えずに直す手は尽きた。直すなら
  SCL / SDA に 2.2〜4.7 kΩ の外付けのプルアップ。それまでは 6-2 の wire は既知の失敗（記録の defects に入れるかはユーザーと決める）。
  → 2026-11-14 深夜、[oep] と切り分けた: (1) 崩れたあと DUT が 5 を返し続けたのは、相手が読み出しの途中の 0 を SDA に握った
  ままだったため。コアで自動のバスクリアを入れて直した（18e19c42）。(2) 崩れのきっかけは H417 のハードの I2C が作る「潰れた
  クロック」: SCL の High が 20〜80 ns だけで、前後の Low は普通の半周期（20 kHz で 25.5 µs）。1 取引あたり約 1 割、1 を送る
  ビット（その前の Low で DUT が SDA を放したもの）に多い。RTR（2 / 61 / 63）と FREQ（4 / 8 / 30 / 60）では変わらない。
  V203・V205・X035 の取り込みには無く、H417 だけ（台は別）。P4 の target はフィルタ 0〜15・ヒステリシスのどれでも合わせられ
  ない（0 では跳ねを余分に数え、7 以上では潰れたクロックを捨てる）。I2C の規格（標準モードの High は 4 µs 以上）を外れる
  ので、H417 の石の性質として扱う。残り: 規格どおりのプルアップの台（6-2 は配線を変えない）で潰れが出るかを測る（出なければ
  遅い立ち上がりとの組み合わせ）、Wire の README に書くか決める、WCH への報告。6-2 の DUT は FREQ の診断版のまま（次の流しで
  書き直す）。
- [x] **H417 の CH32RVAdcStream で、2 本目が 1 本目につられる（2026-11-14）**: ADC ではなく線。PA7（プローブが保持）を 1 本だけ
  流しても 4096 回中 13〜22 回が 1/4〜3/4 の間で、数は PC0 の方形波の縁の数に見合う（6-2 のハーネスの結合、SPI の CS と同じ
  種類）。2 本で流しても外れるのは 2048 回中 3〜15 回だけで、電荷の持ち越しではない（サンプル時間は 239.5 サイクルで効いて
  いる、SW_PRE も関係なし）。試験は 2 本目の最小・最大で判定していたので 1 回で落ちていた。スケッチが 2 本目の 3/4 より上・
  1/4 より下の回数を返し、外れが波の縁の数（2×立ち上がり＋2）以下なら通す。6-2 で 2 回、V003・V205・V203 も PASS（外れ 0）。
- [x] **V205 EVT の PA10 と PB3 で受けられない**: periph の経路で、プローブが送ったバイトは線に見えるのに DUT が受けない。
  基板ではなく、前の経路の端子が AF のまま残っていたもの。HardwareSerial の end() が端子を入力に戻すようにした e2384071 の
  あとの 5 回（2026-11-13〜14）は、PA6/PA10・PA9/PA10・PB2/PB3 を含む全経路が PASS。
- [x] **WCH-Link の台の uart で、Link の UART が DUT の Serial につながっていない**: 記録に `link_uart` を足した（RECORDS 4.8）。
  nano は回路図で LinkE の UART3 が SB3 / SB4 から PA10 / PA9 なので `{tx: pa9, rx: pa10}`（試験が Serial をその端子で開く）、
  M030 は TX だけ（ユーザー）で `{tx: pb6}`（DUT が送る向きだけ）、X315 は DUT の PD4 から何も届かず `{}`（skip）。nano と M030 は
  PASS（2026-11-14）。
- [ ] **プローブ（P4X）の I2C の相手が 1 ビット早く送る**: 5-1 / 5-2 の wire・softwire で、Sr のあとの読みの 3 バイト目などが
  左に 1 ビット寄る（[oep] の解析）。ファームの確かめはユーザーの確認待ち（[oep]）。
- [ ] **standby から起きた DUT のコンソールが届かない（OEP のプローブ側）**: 5-1 V203 の deepSleep のあと、プローブ
  （e8f60ae84e28、0.0.55+20b7418）の console のストリームは開いたまま（is_open = true）で書き込みも受け付け、切れたことを
  知らせないので、ch32rv がつなぎ直さず黙る（[ch32rv] の計装版の trace で確認、2026-11-13）。5-1 の system はこれで落ちる。
  V205（30eda0e31108）では通る。起床のリセットで DM もリセットされ、DMCONTROL の dmactive が 0 のまま（モニターの接続を
  借りて読んで確かめた: DMCONTROL 0x00000000、DMSTATUS 0x00000c82）、プローブはそれを見ずに DATA0 を読み続けている。
  [oep] が「poll で dmactive = 0 を見たら立て直す」修正をユーザーの了承待ちで用意している。ch32rv 側は、書き込みが no_resource で断られたら
  つなぎ直すよう直した（以前は 'transfer-failed 0x0a' で終わっていた）。
- [x] **ch32rv の次の版を同梱する**: 試作版で確かめた 3 件（OEP の flash の attached-under-reset、H417 の RTT を core 1 で読む、
  stub で書く前の reset-halt = 走っている Ethernet の DMA で書き込みが壊れる件）が出たら上げ、全台を流す。
  → 2026-11-14: 同梱は 0.31.3。H417 の RTT を core 1 で読む件（3f3d0af）と stub の前の reset-halt（5ae82c1）は入っている。
  OEP の flash の attached-under-reset も同じ 3f3d0af で入っている（[ch32rv]: 普通の attach に答えないと port の NRST を
  握ったまま attach し直し、warning attached-under-reset。`--connect-under-reset` も OEP の flash で使える）。

## 文書

- [x] **英訳の対をそろえる**（[oep] からの依頼、2026-11-13）: `python3 tools/docs/pairdiff.py .` のずれは 114 対 → 10 対
  （2026-11-20）。残るのは、`<バイト数>`・`<口>` のように例の中の仮の名前を日本語に訳したもの（読み手のための訳で、
  そのままにする）と、英語を「日本語の計画が正本」とした要約の tests/TEST_PLAN.md。直す中で、Servo の日本語の timer の説明が
  古いままだった、README の Watchdog の行に H41x が無かった、などの中身のずれも直した。
- [x] H417 の 2 コア（V5F）をどう使うか: スケッチは既定で V5F（CPU Core メニュー、418d1a5c）。V3F は起こすだけで、
  受け渡し用の語で関数を受け取れる（5c42a9b5）。調査と測定は docs/h417-cores-design.ja.md。

## CH32H41x の V5F と V3F（2026-11-11 から）

- [x] ベンチの H417 で、試験一式を V5F（既定）と V3F で流した（2026-11-13）。LinkE で走る case は、下の DMSeq の件のほかは両方とも PASS
  （uart は `usart1=fix_pb6_pb7`）。見つかったもの: V5F で flash に書いたページが消えたままだった（V5F が書き込みを溜めていた。
  データを書くたびと PG_STRT の前に fence）。synckit のスケッチはタイマーの分周を F_CPU で計算していた（V5F では HCLK の 4 倍）。
  LinkE の台でも gpio の case を選んでいた（OEP の治具が要る）。peer の case はまだ（H417 の USB の相手がベンチに無い）。
- [x] UHSIF の FX3 由来の機能を確かめた（2026-11-14、docs/h417-uhsif.ja.md の「FX3 由来の機能の確認」）。幅 8 / 16 / 32、データと
  制御線の比較器（トリガー、変化だけ積む）は動く。端子とビットの自由な対応、直列の入出力、CRC、繰り返し回数、DDR は無いか効かない。
  CH32RVUhsif に `setBusWidth()`、`setDataMatch()`、`setDataChange()`、`setControlMatch()` と例 UhsifTrigger を入れた。
- [x] H41x の Clock Source（25 MHz の水晶、既定）。HSI の約 1% のずれが UHSIF や HSADC の速さに出ていた（[oep] の依頼、2026-11-14）。
- [ ] **H417 / H416 の USBSS のホスト（フルスペック、2026-11-14 ユーザー）**: ハードはホストにも対応（RM 27 章「USB3.0 SuperSpeed
  Host/Device Controller」、EVT の USBSS/HOST に 3 つの例: セクタの読み書き、ハブ越し、ハブ越しの USB メモリ）。CH32RVUsbHost に
  USBSS の HCD を足す: 制御・バルク・割り込み・等時、ハブ越し、USB 2.0 の機器（同じ口の 2.0 の線）の扱い。EVT のドライバは
  ソース（ch32h417_usbss_host.c 約 300 行、列挙とハブの ch32h417_enum.c 約 2400 行。`.a` は FAT の libRV3UFI だけ）。試す相手の
  配線が要る。
  決めたこと（2026-11-14 ユーザー）: SS のホストは SS だけ。同じ口の USB 2.0 の線は USBHS（SWD と同じ PB8 / PB9）で、別の
  コントローラーとして扱う（常駐ベンチでは HS は使わない見込み）。ストリーム（UAS）と 1 間隔に複数 burst の等時（Mult>0）は
  実機で試してから決める（ホストのレジスタに Stream ID の欄が見当たらない）。対向は H417 どうしの直結（片方が DCD）。
  設計の下調べ（EVT と RM、エージェント）: エンジンは TX / RX が 1 組で、端点の切り替え・ERDY 待ち・割り込みと等時の間隔は
  ソフトが組む。EVT にホストの等時の本体は無い。ERDY にデバイスのアドレスが出ないので、ハブの先の SS の機器が複数だと
  どれの ERDY か決まらない。スタックの直す所: 先にハブがあるときの速さを status の bit 9 / 10 で決めている（SS ハブでは
  bit 9 は PORT_POWER）、mps0 が uint8_t、クラスの受け口が 512 で SS の 1024 バイトのパケットがあふれる、SS ハブ（記述子
  0x2A、SET_HUB_DEPTH、ルート文字列、変化のビット）。DCD の USBSS の割り込みハンドラーは USBHS2 と同じくコアのフックへ移す
  （ホストと両方入れるとリンクでぶつかる）。段: 0 共通のヘッダーとフック → 1 リンクと接続 → 2 制御と列挙 → 3 バルクと
  ERDY → 4 burst → 5 割り込み → 6 ハブ → 7 等時 → 8 warm reset と再列挙。
  段 0 は済み（0773d83d、ビルドだけ。SS デバイスの実機の確認は次に 6-2 で）。HCD の本体は下書き中。範囲はストリーム・
  Mult>0・U1〜U3 も作る（2026-11-14 ユーザー: 基本はすべて作る）。対向の配線は下の「USB の試験の配線」。
  → 2026-11-20 初めて実機で流した（h417qeu6-h417: DUT 640eb63c が SS のデバイス、相手 A94B8F062EE9 の nano が SS のホスト）。
  usb-peer は DUT が READY を返さず落ちる。原因は電源: ホストが SS を始めると DUT が POR/PDR でリセットされる（RCC が丸ごと
  既定に戻り、RSTSCKR（H417 は 0x28）が 0x88000000 = PORRSTF + LPWRRSTF、RAM は残る）。DUT の SS を使わないスケッチ
  （runtime）ではホストを動かしても落ちない。落ちる前に DCD の割り込みは無い（足跡で確かめた）。両側の SS の PHY が動き出す
  ときに、ホストの SS の口の VBUS から取っている DUT の電源が足りない見込み。リセット後は V5F が 0 番地のまま起きず（V3F が
  起こせていない）、SWIO の端末も黙る。ホストの側は動き続け、`dev` は 0 台。→ DUT を別に給電（VBUS を切った A–A の線）
  してもらってから続ける。HCD の中身はまだ何も試せていない。
  → 2026-11-20 続き: 別給電で DUT は落ちなくなった。DUT が SS.Inactive に入ったまま、ホストは自分では Inactive を見ず
  Polling の失敗（→ Disabled）を繰り返していたので、学習に 3 回失敗するごとに warm reset を出すようにした（DUT は
  RxDetect に戻る。H417 同士のリンクはまだ上がらない）。ホストの切り分けに USB3 メモリ（16 GB）を A94B の SS の口に挿し、
  電源を入れた直後なら: リンク（0.36 秒）・LMP・列挙・SET_SEL・U1/U2 の許可・GET_MAX_LUN・MSC の BOT（TEST UNIT READY、
  READ CAPACITY のデータと CSW、NRDY → ERDY）・文字列の記述子まで 0.59 秒で通った。直したこと: 転送を終える
  パケットの EOB で流量制御に入っていた（メモリは CSW に毎回 EOB を付け、次のデータで ERDY を送らないので、毎回
  ERDY_RETRY_MS の 100 ms 待っていた）。分かった制約:
  - WCH-LinkE で接続すると（端末・read・halt）H417 の HSE が切られ、HCD は USBSS のクロックを入れ直してリンクを張り直す。
    メモリはそこで失われる。端末は SS を始める前につなぐ。
  - メモリは電源投入から短い間にホストと学習できないと SS をあきらめる（USB 2.0 に落ちる見込み）。nano の SS の口は VBUS が
    直結で USB 2.0 の線も切ってあるので、抜き挿し（または板ごと電源を切る）しか戻す手がない。usb.begin() を 0〜1 秒
    遅らせた短いスケッチではつながらず、相手用スケッチ（peer_ch32rv）では 3 回ともつながった。差はまだ分からない。
  - メモリを挿すとホストの nano がリセットされる（突入電流で 5 V が落ちる）。
  未確認: U1/U2/U3 からの復帰（LINK_TX_UX_EXIT で戻るか）、大きな読み書きと速さ、burst、割り込み・等時、ハブ、
  H417 同士。次は PPPS の USB3 ハブ（口ごとに VBUS を切れる）をホストとメモリの間に入れると、電源の順番を試験で
  決められる（上の「USB の試験の配線」の部品）。
  計画（2026-11-20）: A 見る手段と電源の順番（ホストは OEP の口の H417 にする: OEP の接続では HSE は切れない、測った。
  足跡を試験のスケッチ側の仕組みにして .noinit に置く。口ごとに VBUS を切れる USB3 ハブで相手の電源を後から入れ、学習の
  通る時間の幅を測る）→ B USB3 メモリ相手のバルクの読み書き・burst（BURST_MAX を上げる）・U1/U2/U3 の復帰・抜き挿しと
  warm reset → C H417 同士（リンク、usb-peer の各 case を SS で、割り込み、usb-ss-iso の相手で等時）→ D SS ハブ越し、
  UAS は BOT で動くことだけ、試験のハーネス（記録の usb.super_speed で usb-peer-host-* を SS で）。2026-11-20 の終わりに
  メモリは OEP 側の H417（640eb63c）の SS の口へ移した。この板は別電源で、メモリの電源を切れないので、挿したまま
  始めても SS で見えない（確認用のスケッチで RxDetect のまま）。
  → 2026-11-20 夜、SS を H417 同士の直結に戻した（ホストは OEP 側の 640eb63c、デバイスは A94B の相手用スケッチ）。
  usb-peer-host は列挙できず。ホストの足跡: 191 ms で U0・LMP（PORT_CFG_RES まで）の後、1 ms ほどで U0 と Recovery を
  何度も行き来して SS.Inactive、warm reset で約 87 ms ごとに繰り返す。ホストの de-emphasis（LINK_CFG[9:8]）を変えると:
  0x100（今の 3.5 dB）と 0x0 は Inactive に落ちる、0x200 と 0x300 は U0 を保ち、列挙・MSC の INQUIRY（CH32RV RAM disk）・
  容量・先頭ブロックの読み出しまで通る（64 ブロックの読み出しで STALL、未調査）。ただし Recovery が 1 ms に数回続く。
  デバイス側の de-emphasis（0x100 / 0x200 / 0x300）と、ホストが U1/U2 を受け入れるかどうかでは Recovery の数は変わらない
  （誤りによる Recovery）。PHY の設定は EVT のホストと同じ。メモリ相手ではホストに Recovery は無く、PC 相手ではデバイスに
  無いので、H417 同士の直結（A–A のケーブル、両方の PHY）の組み合わせの問題。次: ケーブルを替える、受信の等化
  （LINK_RX_EQ_EN と PHY の 0x03 / 0x0D / 0x15）を振る、ホストの既定の de-emphasis をメモリ相手でも 0x300 で確かめてから
  決める。実験の切り替えは外した（コードは元のまま）。
- [ ] **USB の試験の配線（2026-11-14 ユーザー: すぐには配線できない。USB のケーブルは入れ替えやすく、OEP の端子の線が高い）**。
  網羅の軸・相手の役割・3 台の H417 のつなぎ方の案は tests/TEST_PLAN.ja.md の「USB の網羅の軸」。要るもの:
  - H417 の 3 台目（A、LinkE で OEP に相乗り）。A の SS → USB3 の PPPS ハブ → C（6-1 の nano）の SS と USB3 メモリ（UAS）。
    A の FS → USB2 の PPPS ハブ → C の FS、Pico、LS の機器。B（6-2）の SS と FS は PC のハブのまま。
  - 部品: 外部給電の USB3 の PPPS ハブ（uhubctl で口ごとに切れるもの）、USB2 の PPPS ハブ（マルチ TT とシングル TT）、
    VBUS を切った A–A の USB3 ケーブル（nano と EVB の SS の口は Type-A で、1 番ピンは基板の VBUS に直結。両端の 5 V が
    ぶつかる）、USB3 メモリ（UAS 対応）、USB2 メモリ、LS のキーボードかマウス。
  - HS は V407 で（USBHS1 のホスト → マルチ TT のハブ → USBHS2 のデバイス・P4・Pico・LS）。H417 の HS は SWD と同じ線なので
    常駐しない。
- [ ] USB の試験の仕組み（配線の前にできる）:
  - 記録の usb に CH32RV の相手（`{to: ch32rv, port: <相手のアドレス>}`）。run_hardware が両方のスケッチを作り、相手を
    その台のプローブで書く（Pico の相手と同じ形）。2026-11-15: 記録（`controller` も）・place・run_hardware・相手の
    スケッチ 5 つ（usb_peer/*/peer_ch32rv）を入れた。ビルド（H417 の SS / FS、X035）と単体試験だけで、実機は配線待ち。
    相手のライブラリに無いもの（ホストの isochronous と NCM）の case は飛ばす（tests/peer/usb/README.ja.md）。
    同じ OEP プローブの 2 つの口に DUT と相手を置いたときのモニター 2 つは未確認。
    2026-11-15: 相手の試作で見つかった穴を実装した。Pico・P4・PC が相手の回帰は V205・V407・H417 で 20 件通った（CH32RV どうしは配線待ち）。ホストの長い構成記述子（窓で読み直す）と
    切らない文字列、デバイスの MSC の複数 LUN と `setSense()`、HID の `enableOwnInterface()`。usb-peer-host-storage は
    CH32RV の相手でも流す。実機で最初に見るもの: 窓の読み直し（制御転送の途中のパケットを捨てる。USBFS・USBHS・USBSS の
    HCD で）、usb_contracts の 302 バイトの構成のインタフェースが全部クラスに付くこと、2 LUN の GET_MAX_LUN と
    lba 13 の 02/3A/00、host_input の 2 つ目の HID（ライブラリのキーボード）。
  - junit に controller・相手・ハブを残し、coverage の USB の項目をその軸で分ける（TST-01 の残りと同じ）。
  - PC の libusb のベンダーの試験（Python、pyusb）: ストリーム、等時の Mult・任意のパケット数、burst、0 長、STALL と
    CLEAR_FEATURE、サスペンドとリモートウェイクアップ、LPM / U1 / U2（sysfs）。B（6-2 の SS）で今すぐ流せる分から。
    2026-11-17: 等時の Mult は usb-ss-iso（tests/instrumented/usb/test_usb_ss_iso.py、スケッチ usb_ss_iso）を入れた
    （DCD は 1 間隔を 1 chain、記述子は superSpeedBurst()）。ビルドだけで、6-2 で未実行。
    2026-11-20: 同じスケッチにベンダーのバルクを足し、STALL と CLEAR_FEATURE（test_usb_vendor_halt）、ポートのリセット後の
    再通信（test_usb_vendor_after_reset）、パケットの前後・burst・0 長（test_usb_vendor_bulk_sizes: 1〜65536 バイトの往復、
    64 KiB は 10 ms）を入れ、H417 の SS で通った。残り: ストリーム、サスペンドとリモートウェイクアップ・LPM / U1 / U2
    （sysfs への書き込みに root が要る）。
  - デバイスの試験用のスケッチに、ストリーム・Mult・burst の窓・STALL を起こす命令を持つベンダーの機能を足す。
    → 2026-11-20: usb_ss_iso にベンダーのバルクと STALL の要求を足した（Mult は等時の代替設定）。残りはストリーム。
  - ハブの抜き差しを PPPS の口の電源で起こす道具（今の power の case の uhubctl を、相手の機器にも使う）。
    → 2026-11-20: usb-ss-iso::test_usb_vendor_after_unplug（DUT の親のハブの口を uhubctl で切り入れ、3 回とも SS で 4.7 秒で戻り
    往復が通る、H417）。ピアの機器（Pico・P4）の側は、今の配線ではハブを通らないので未。
- [ ] L103 は今ベンチに無い（2026-11-18、ユーザー: 後日 OEP で配線し直す）。その間はビルドだけ。配線ができたら、今日のコアの
  変更（RTC のアラームの割り込みでの読み直し、休止、`Serial.baudRate()`）を selfsys・system で確かめる（ほかの 9 系列は通った）。
- [x] **H417 の SPI の CPHA 0 が 1 ビットずれて NG（2026-11-15）**: DUT は正しく、WireSkein の照合の誤り（MOSI が送る側の縁の
  1 サンプル前に変わって見え、変化から逆の CPHA と読んでいた）。wireskein 0.0.29 で直り、H417 の output_only・contract が通る
  （2026-11-18、core-20261118T035825Z-e13f21a7）。
- [x] wireskein 0.0.29 の spi slave の照合が verify.py:624 で `KeyError: 'cpol'`（V203 の test_spi_slave）。0.0.29 の退行（区間が
  CS の選択の途中から始まると CPOL を線から読めず落ちた）で、0.0.31 で直った。V203・H417 とも spi の 4 つが通る（2026-11-18、
  core-20261118T044931Z-ac03a565）。同じ回の V203・H417 の取り込みの落ちは、ホストの負荷（ロードアベレージ 11〜22）でプローブの
  送り待ちがあふれたもの。
- [x] **V103 の RTC のアラームが約 1 秒遅かった**（2026-11-18）: 2 秒が 2.6〜2.9 s。レジスタを直接読む試しで、V103 も ALRF は値の
  1 カウント後に立つ（ALR = CNT で約 1 s、+1 で約 2 s）こと、立った直後の CNT はまだ ALR の値を読むことが分かった。ほかの系列と
  同じく 1 引いて書き、アラームの割り込みでは常に RTC を読み直す（ch32rv_rtc_resync。休止中だけだった）。1 引いただけのときは
  割り込みが古い CNT で「まだ」と見て過ぎた値を書き直し、コールバックが来なかった。V103 と V203 で system・selfsys が通る
  （V103: コールバック 1.50 s、lightSleep 2 s が 1.87 s。core-20261118T004143Z-d6fc23b3）。
- [x] **V103 の休止（lightSleep）**（2026-11-18）: 1 秒未満の端数を待つ普通の WFI が、V103（QingKe V3A）では SysTick で
  終わらなかった（WFI を 1 回だけ実行する試しで戻らず、IWDG でリセット）。V103 だけ端数は起きたまま待つ。selfsys・selfexti・
  system が v103r8t6-evt で通る（core-20261118T000528Z-337cb4a4、core-20261118T002526Z-687c498c）。system の deepSleep は、
  スタンバイの起床を CH32RV_RESET_DEEPSLEEP（ESP_RST_DEEPSLEEP にならう）で返すようにして判定した: スタンバイが残すリセットの
  印は V307 が電源投入、V203 が起床だけ、V103 が無しで、試験は電源投入の印で見分けていた（V103 で 2.07 s を 0.3 s 以内と
  判定していた）。途中で v103r8t6-evt が答えなくなったのは、code flash を消して HSI のままの V103 の答えの遅れ（[oep] が
  b532f9a で直した）。
- [ ] H417（6-2）の uart.formats の 115200 7O1 で、DUT が何も受けなかったことが 1 回（2026-11-17、core-20261116T223121Z-dc183e78。
  流し直しは通った）。
- [ ] V407 と P4（ホスト）の usb-peer で、DUT の `end()` のあと P4 が「S3 DISCONNECTED」を出さないことが 3 回に 1 回
  （2026-11-17、core-20261116T225416Z-463912b5。P4 のログは「USB HOST: EP command error: ESP_ERR_INVALID_STATE」）。HID の既定を
  1 ms にした直後の回。EspUsbHost の切断の扱いか DUT か切り分ける。2026-11-18 に 3 回流して 3 回とも通った
  （core-20261117T230649Z-d1480d04、…231209Z-c23c13b9、…232050Z-0ad7806c）。再現しないので、次に出たら P4 と DUT の両方の
  ログを取る。
- [x] ch32-device-data の V103 の remap は 1af6898（2026-11-17）で揃い、lock を上げて TIMER_ROUTE_FIXES の V103 の行を
  外した（TIM2 は補正と同じ 12 通り。TIM1 の値 1 の PA8〜PA11、TIM3 の値 2 の PB0・PB1、USART3 の値 1 の CTS・RTS が
  増えた）。v103r8t6-evt の signals（pwm_route を含む）は通った（2026-11-17、core-20261117T050059Z-0a322fc7）。
- [x] H417 で setAlarm() を設定したまま lightSleep() すると戻らなかった（2026-11-17）。stop から起きた直後の RTC の
  読みは RSF が立つまで古い（RM「special read-write」）。RTCAlarm の割り込みが古いカウンタでスケッチのアラームを
  「まだ早い」と見て、休止が同じアラームを過ぎたカウントに書き直し、二度と鳴らなかった。休止中にスケッチの
  アラームがあるときは、割り込みの中で ch32rv_rtc_resync() してから比べる。試しのスケッチ B〜H（タイマーだけ・
  +60 s は起き、+2 s は戻らない）で絞った。selfexti は H417・V203・V205・V407、H417 の selfsys が通る
  （core-20261117T112525Z-2256aada、core-20261117T112730Z-023d555f）。
- [ ] suspend・resume・リモートウェイクアップの試験をピアで作る（2026-11-17、ユーザー: 実転送以外の試験はすべてピア）。
  PC 相手の usb-power はやめた（power/control に root が要り、udev のルールが要った）。ホストの側は済んだ（2026-11-18）:
  CH32RVUsbHost に `suspend()` / `resume()` / `suspended()` / `onRemoteWakeup()`、HCD の portSuspend を USBFS（3 つの
  配置）・USBHS2・USBHS1 に足した。USBFS の resume の K は BASE_CTRL の SYS_CTRL 11（ホストモードで K を出す。01 は SE0）。
  SOF の再開だけ・低速の bit・GPIO では RP2040 が起きなかった（V205 で測った）。usb-peer-host-power が V205（USBFS と
  Pico）・V407（USBHS2 と P4）で通る。USBHS1（V30x）は RM の TX_BUS_SUSPEND / TX_BUS_RESUME の読みでビルドだけ
  （「software で 10 ms 上げる」の読みが推定）。残り: デバイスの側（DUT の CH32RVUsbDevice が眠らされる。DUT 側は
  tests/sketch_support/usb_power）を usb-peer-power の case にする。眠らせるピアは CH32RV（CH32RVUsbHost）しかない
  （Pico の TinyUSB と EspUsbHost にはホストの suspend が無い）ので、CH32RV どうしの対向の配線（TEST_PLAN「USB の網羅の軸」）を待つ。
  2026-11-18 に [EspUsbHost] / [EspUsbDevice] に確かめた: EspUsbHost は arduino-esp32 3.3.x（ESP-IDF 5.5.5）の公開 API に root port の
  suspend が無く、esp-usb 1.1 / 1.2（IDF 6 系、arduino-cli では差し替えられない）で可能になる。ユーザー: いまのところ EspUsb 側では
  対応しない。EspUsbDevice（デバイス）は suspended() / remoteWakeup() があり、usb-peer-host-power で使っている（コールバックは無い）。
- [ ] USBSS の等時 Mult>0（3a26cf6f）の usb-ss-iso: Mult 0・1・2（bMaxBurst 1）の IN と OUT の 6 つは H417 で通った
  （2026-11-17、core-20261117T113755Z-a6e137fd、どの間隔もそろった）ので、Mult>0 はハードで動く。48 パケットの代替設定は
  usbfs の ENOMEM（1 回 8 MiB）で、試験を 1 回 2 MiB に直した。残り: 同じ試験が通る回と全部落ちる回がある。落ちる回は
  最初の代替設定の切り替え（SET_INTERFACE）が "Other error" になり、以後の転送が status 1 / submit -1 で続けて落ちる
  （core-20261117T113920Z-40375c22）。通った回は直前のイメージが USB を使わないもの、落ちた回は USB を使うもの
  だった。DCD の SET_INTERFACE と等時のエンドポイントの開き直しを、再列挙の直後に追う（usbmon は root が要る）。
  → 2026-11-20: SET_INTERFACE の失敗は 3 回とも出なかった。代わりに IN の Mult>0（alt 2〜4）で、間隔の 2〜4 % が一部の
  パケットだけで来て落ちる（in[2] の部分 8〜58 / 512、errors 13〜144。in[1] は部分 0 で通る）。11-17 に通った版（7e9ccc6）の
  コアに今の試験で流しても、1 回目は in[2]・in[3] が通り、2 回目は in[2] の部分 14 で落ちたので、コードの退行ではなく回ごとの
  ばらつき（二分探索では決まらない）。DUT の in_submitted が間隔の数より少ない（505 / 512）ので、DUT が次の間隔を出しそびれて
  いる見込み。→ 同日、スケッチの iso に IN の完了の間隔と、完了から次の submit までの時間を足して測った: submit し直しは
  最長 3〜10 µs で間に合っている。部分の間隔の数（14〜16）と、完了の間隔が 1.5 間隔を超えた数（14〜16、最長 246〜424 µs）が
  そろうので、チェーンが 1 間隔で出切らずに次の間隔へまたがっている（ソフトの遅れではない。Mult 0 では起きない）。PC とは
  SS のハブ 2 段を通る。残り: usbmon（root）でホストが各間隔に何パケット求めたかを見る、ハブを通さない口で比べる（配線）。
  → 同日さらに、部分の間隔とその前後のパケットの頭（通し番号と何番目か）を記録して確かめた: 部分の間隔はどれも、前の間隔が
  ホストの誤り（status 1）で、その間隔に同じ番号の最初のバーストが入り、残りが次の間隔に来たもの（Mult が出されていない形
  ではない）。試験はこれを iso_in_spilled として部分から外した。外すと部分は 0。残るのはホストが見る転送の誤りそのもので、
  Mult 0 でも 800 間隔に 35〜149 と回ごとに大きく変わる（同時に別の台の試験でホストの USB が混んでいた回が多い）。次は
  usbmon（root）で誤りの種類を見る、ハブを通さない口で比べる（配線）。
  → 同日の夕方に V5F で HSE と HSI を交互に 2 回ずつ流すと、部分の間隔はどれも 0〜3（in[2] 0〜2 / 512）で、昼の 8〜15 より
  ずっと少なかった。時計の源ではなく、時間帯（ホストの負荷やハブの先の他の機器）で変わっている。
- [x] ch32-device-data cb02bca / 3c9ed13 の取り込み（lock は 3c9ed13）。pvd_levels.csv は生成器の load_pvd に、pinout.csv の
  condition の列は SDIO の経路の一般の規則にした（SDIO_ROUTE_FIXES をやめた）。2026-11-18 に残りの表をコアと突き合わせた:
  adc_triggers.csv は ch32rv_adc_trigger.h と値がすべて一致（値 6 の TKEY / OPA はコアが使わない）。exti_lines.csv は
  V407 の線 18 = USBHS1・20 = USBHS2 で確定し、両方のベクタを有効にする回避を外した（selfexti の swi で 18・20 とも 1 回
  ずつ、core-20261117T221818Z-d3fa53ac）。ほかは一致。V20x / V30x の線 18 は [data] が 68be61e で足し、一致した。
  interrupts.csv は生成器の tools/generate/interrupts/interrupts.csv と slot ごとの handler が一致（差は D8W・D8C の条件
  つきの行だけ。V30x D8 の slot 58 も USBWakeUp で同じ）。
- [x] EXTI の線の表（internal/ch32rv_exti_line.h）と ADC のトリガの表（internal/ch32rv_adc_trigger.h）を device-data から
  生成する。値は一致しているので、残りは device-data の名前（USBHS1_WAKEUP など）とコアの名前（CH32RV_EXTI_*、
  ベクタ名、試す順番）の対応表。
  → 2026-11-20: EXTI は手書きのまま、tests/unit/test_exti_lines.py で device-data と線と源が一致することを毎回確かめる形にした。
  生成にすると、5 つの系列の別名（USBHS1・USBPD0・USB・USBFSOTG・LPTIM1・OSCCAL、ベクタの大文字小文字）と、V103 の線 18
  （CH32F103 の USBD）、V307 の線 20（die で違う）の除外が、生成器の中の別の層になるため。V407 の線 18 / 20 の conflict は
  device-data の中の根拠の話で、採った値（実測）は同じ。
  → ADC の通常グループのタイマーの源は生成にした（2026-11-19、variant の CH32RV_ADC_TIMER_SOURCES。F1 型の TIM1 を試す順だけ
  CC1→CC3 に変わった。V203・V205・H417・X035・V006 で periph が通る）。端子の源と EXTI の線は手書きのまま（EXTI は V407 の
  線 18 が device-data でまだ conflict）。
- [x] CH32V003 の VDD の測定が約 35 % 高かった（3.3 V の板で 4448〜4464 mV）。ADC の初期化が CTLR1 = 0 と書き、
  CALVOLSELECT（26:25、リセット値 01 = VDDA × 2/4、00 は無効）を消した状態で自己校正していたため、校正が約 100
  カウント低く残っていた（Vrefint 277 / 1023、VDDA/2 が 410。直して 378・511・767、VDD 3256 mV）。CTLR1 を書く
  所はすべて CH32RV_ADC_CTLR1_RESET（V003 は 1 << 25）を入れる。selfapi・selfexti・periph が V003 で通る
  （2026-11-17、core-20261117T113312Z-1e9ba3eb）。見逃した試験の側も直した: periph の test_adc が、プローブが High に
  した端子を満点で読むとき、DUT の VDD（Vrefint から）が 3.3 V × 1.2 を超えないことを見る（V003 3248、V203 3334、
  H417 3350 mV で通る、core-20261117T114148Z-8b102885）。
- [x] CH32X315 の ADC: コアの初期化が CTLR2 を丸ごと書き、CAL_AUTO（ビット 6、リセット値 1 = ハードの自動校正）を
  0 にしていた。EVT の ADC_Init は残す（CTLR2_CLEAR_Mask）ので、V003 の CTLR1 と同じく CH32RV_ADC_CTLR2_RESET で保つ
  （2026-11-18）。読みは変わらない: 0 のとき VDD 3199〜3218 mV、1 で 3212 mV、自分で High にした端子 3204〜3208 mV
  （selfapi、core-20261117T222121Z-64fc1f32）。精度そのものは外の電圧源の手作業の試験（ADC の精度はマニュアル）。
- [x] H41x の flash の 480K モード（DBMODE が立っていない）では消去の単位が 4 KB なのに、`ch32rv_flash_erase()` は
  8 KB 刻みで進み、半分を消し残していた（2026-11-17、コードを読んで見つけた）。単位はどちらのモードでも 8 KB にし、
  480K では 4 KB のセクタを 2 つ消す（erase_one）。`ch32rv_flash_erase_size()` は 8 KB を返す（480K で 4 KB を返して
  いたので、DFU の書き込みが 8 KB 揃えの確認で断られていた）。範囲の上限はモードの大きさ（`ch32rv_flash_size()`）。
  960K の H417 と V203 で flash が通る（2026-11-18、core-20261117T220801Z-8ac2f076）。480K は試していない（オプション
  バイトの書き換えになるので常駐の石ではしない）。
- [x] H41x の 480K モードでは、リンカースクリプトの FLASH が 960K のままで、末尾に置く `.ch32rv_store`
  （CH32RV_FLASH_STORE）が flash の外になる。480K の石を対象にするなら、メニューかリンク時に FLASH の長さを分ける。
  → 済み（2026-11-19 に確認）: DBMODE は読み出し専用で石が決める（RM 1 章・46 章）。480K の部品は H416RDU6 だけで、その variant は
  もう ch32h416_480k_896k.ld（FLASH 491520）を使う。960K の部品が 480K で動くことは無い。
- [x] H417 の CPU Clock 200（V5F 200 / V3F 50 MHz、HPRE で 1/2 の EVT に無い組）で、window watchdog の早い給餌のリセットが
  原因のフラグを残さなかった（2026-11-17）。EVT に無い組は出さないことにし（ユーザー）、200 と 25（PLL 無し）の項目を
  H415〜H417 のメニューから外した。H41x の CPU Clock は EVT の 400 / 100 だけ。
- [x] uart の試験は、DUT の実際のボーレートを `f_cpu / round(f_cpu / baud)` で出していた（BRR を数えるのは USART の
  APB のクロック）。コアに `Serial.baudRate()`（arduino-esp32 と同じ。USART のクロック / BRR）を足し、試験のスケッチが
  ucfg・uhd・dbegin の答えに入れ、ホストはそれを使う。H417（PCLK 100 MHz、F_CPU 400 MHz）と V203 で uart・uart-dma が
  通る（2026-11-18、core-20261117T221139Z-0d30325f）。
- [ ] V205 の signals::test_timer_sync が「区間の端しだいで判定が変わる」で未判定になった（2026-11-20）。WireSkein が直した
  （区間の両端の、組を探す窓の中の相手のいない縁を数えない。wireskein aab7eb7、0.0.33 から）。0.0.33 が出たら上げて V205 の
  signals を流し直す。
- [x] X035 の softwire の wire_slave「read 4」の NG は、WireSkein 0.0.28 の解読の不具合（2026-11-17、
  core-20261117T034033Z-6f14bd0b）。X035 の SoftWire target は SDA を SCL の立ち上がりの約 1.5 µs 前に変える（規格の
  setup の内）。窓に読み出しが 1 つだけだと、縁を立ち下がりと見誤って 0 件になる。[wireskein] が main で直した
  （未リリース）。リリースが出たら上げ、X035 の softwire を流し直す。
  → 2026-11-20 に 0.0.32 で流し直すと、解読ではなく DUT の側で落ちていた（プローブのマスターが読んで何も来ない、2 回とも）。
  二分探索で 8ca5bfef（USB PD の CC の端子の Rd を切る処理を ch32rv_gpio_set_config() に入れた）から。X035 は開放ドレインが
  無く、SoftWire の target は 1 ビットごとに設定の nibble を書き換えるので、その経路に入った CC の確認で応答が遅れた。
  ch32rv_gpio_write_config()（nibble と X035 の USB の端子の errata だけ）を分け、ch32rv_gpio_od_write() はそれを使うようにした
  （CC の Rd は pinMode() の最初の設定で切れている）。X035 の softwire の wire_slave が 2 回とも通り、X035・V205・H417 の
  softwire・wire・gpio も通る（H417 のハードの Wire は既知の潰れたクロック）。
- [x] V006 の signals::test_timer_trigger: `CH32RVPwmTimer.startOn()` で待っている間、出力（PC0、TIM2）が Low でなく High のまま
  （2026-11-20、ほかの X035・V205・H417・L103 は通る。トリガーの後の 20 kHz は正しい）。試したが効かなかったもの: 待つ間の
  カウンタを 0 でなく自動再読み込みの値にする（中央揃えの pwm_timer が崩れた）、出力の参照を強制の Low にしてから PWM モード 1 に
  戻す。次は待っている間の CHCTLR・CCER・CNT・端子の設定を DUT から読んで、どこが High を出しているかを見る。
  → 同日、待っている間のレジスタをプローブで読んで直した: V006 の止まったタイマーは、出力を最後のクロックのときの値のまま
  持ち、CNT を動かしても強制 Low にしても止まったままでは変わらない（UG のときの CNT 0 < 比較値で High）。restartCounter() で
  強制 Low にしてカウンタを数カウント走らせ、止めて PWM モード 1 に戻す。V006 で timer_trigger・pwm_timer・phase_shift・six_step、
  X035・V205・H417 でタイマーの 4 件が通る。
- [ ] H417（h417qeu6-pico）に 9 本（PA4・PA5・PB0・PB1・PD15・PE9・PE11・PE13・PF8）を足して流した（2026-11-20）。
  - analog の test_dac・test_dac_both・test_adc_dual・test_adc_injected_dual が、値が揺れて落ちる（2〜7 %、2 回とも）。
    PA4 / PA5（DAC1 / DAC2）にプローブの線がついてから。DAC の出力バッファ（BOFF=0、有効）が線の容量を駆動して
    発振している見込み。確かめ方: 線の側のプローブの端子の設定（入力・プルなし）を確かめ、DAC を 1 つの値に止めて
    取り込みで波形を見る。直らない配線なら、DAC の自己試験は線の無い DAC の端子で、とする。
  - signals の test_complementary（break）: PA6 を TIM1_BKIN にして Low → High に戻した後、resume() で MOE が立たない
    （RESUME 0。BRKSTATE は latched 1・端子 1）。CH3N の PB1 に線がついて初めて走った組（TIM1 CH3 PE13 / CH3N PB1 /
    break PA6、route 1）。H417 の TIM1 には AUX の BK_SEL（BKIN1 / BKIN2 / 両方、DFSDM）があり、BKIN2 は PA5 / PE6 /
    PA12。次は失敗した状態で TIM1 の AUX・BDTR・INTFR を OEP で読み、BKIN が何を見ているかを決める。
    → 直した（2026-11-21）: resume() は効いていた（後で BDTR=9c00、MOE 1。AUX=0 で BKIN だけ）。MOE を書いた直後の読み返しが
    古い値で、resume() が 0 を返していた（H417 400 MHz、1 回読み直すと 1。break を保ったままだと 10 万回読んでも 0）。読み返しを
    最大 64 回に。h417qeu6-pico の test_complementary が通る。
  - periph の test_hsadc（PF8）: プローブの PWM の始まりの過渡（約 10 周期が別の duty）で落ちていた。PWM を区間の前に
    始めて通る（add84fc9）。プローブの側は [oep] に伝えた。
  - wire の 7 件は前からの「潰れたクロック」（外付けのプルアップの台で測り直す）。software-serial と spi は 1 回目だけ
    落ちた（spi は裏のフルテストと重なって取り込みが欠けた回も）。
- [ ] V208 はベンチから外し、今後も置かない（2026-11-20 ユーザー）。BLE は対応しない。ほかの周辺は V20x の V203C8T6（OEP）と
  V203RBT6（Ethernet、4-3）で見る。V208 だけのもので、実機で確かめられずビルドだけになるもの: (1) Clock Source の HSE
  （32 MHz の水晶から作る PLL、boards.txt の CH32V208.build.clock_hse_defines）、(2) variants/CH32V208 の型番ごと
  （CBU6・GBU6・RBT6・WBU6）の既定の端子。
- [x] V006 の signals の test_pwm_timer（中央揃え）が、コードの配置しだいで落ちる（2026-11-20）。SixStep の止めた相を
  直した（下の行）だけで、触っていない CH32RVPwmTimer の試験が 3 回とも落ち、戻すと 3 回とも通る。落ち方は、pwmt の直後に
  PC0 に 10〜15 µs の余分なパルス、約 3 周期の空白、write() の値が頂点で入った半分のパルス（許容は 1 つ）。直前は端揃えの
  pwmt と pwmtoff。試して効かなかったもの: connect() で比較値 0 をプリロードなしで書く、begin() で restartCounter() を呼んで
  から開始（どちらも戻した）。次は pwmt の各段（begin・attach・write）の直後に CNT・CCR・CTLR1・CCER をプローブで読み、
  余分なパルスがどの段で出るかを取り込みのマーカーで区切って見る。同じ試験の test_timer_sync（PC0 / PB1 のずれ 200 ns、
  許容 ±200 ns の境目で判定保留）は前から。
  → 直した（2026-11-21）: V006 では begin() 128 µs・attach() 113 µs・write() 12 µs かかり、カウンタが走ってから値が入るまで約
  150 µs。始まりの余分なパルスは、止まったタイマーの出力が前の使用（端揃え）の High を保ったまま端子を AF につないだもの
  （UG で比較値 0 を入れるだけでは 10.9 µs 残った）。begin() は UG で比較値 0 を入れ、カウンタを 1 つ進めてから端子をつなぐ。
  test_timer_sync: 10 MS/s では V006 の 120〜140 ns が 200 ns に量子化されて境目だった → 50 MS/s（±200 ns の 10 / T）。端子を
  後でつなぐと X035 の timer_sync が落ちた: startWith() は走っている主タイマーを 0 から始め直すので、High の途中なら切れた
  パルスが出る（位相しだい）。スケッチの pwmsync を begin・begin・startWith・write の順に、ヘッダーに「走っている PWM は
  切れる」を足した。V006・X035・V205 の signals が全部通る。
- [x] CH32RVSixStep の止めた相が浮いていた: CCxE と CCxNE を両方 0 にすると、OSSR=1 でも出力は無効（端子は浮く）。止めた相は
  強制非アクティブで CCxE だけ立て、CHxN も OSSR で Low に駆動する。begin() の直後も同じ。V205（PC3〜PC8 の線を足した
  2026-11-20）で止めた相の端子に雑音のパルスと High のままが出て見つかった。V205 と V006 で six_step が通る。
- [x] periph の test_serial_routes: 経路の端子が 16 本を超えると 1 回の取り込みに入らない（プローブが unsupported で断る）。
  端子が 16 本に収まるまとまりごとに取り込む。V205 に PC3〜PC8 を足して 19 本になって見つかった。V205 で通る。
- [ ] X035 の softspi が 5 回中 2 回、1 ビット化ける（2026-11-20、PA13 の線を足した日）: shiftOut / shiftIn の LSBFIRST が
  モード 3 と読まれた、SPI のモード 2 LSBFIRST で 1 ビット違い。残り 3 回は通った。線を足す前の頻度と比べる。
- [x] H417 の usb-peer-firmware（Pico は FS の口、2026-11-20 に初めて流した）が 3 回とも別の所で落ちる: DFU の 12 ブロック目の
  DNLOAD / GETSTATUS、UF2 の書き込みが 141 ブロック中 0、DFU の st。usb-peer・usb-peer-classes・usb-peer-net・-fs・-input と
  ホストの側の 5 case は通る。フラッシュを自分で書き換えている間（H41x は RAM から走って flash を消す・書く）の FS の制御転送の
  答えが遅れる・抜ける見込み。次は DUT の側で、DNLOAD ごとの消去・書き込みの時間と、その間の USB の割り込みの遅れを測る。
  → 2026-11-21: UF2 の 0 / 141 は、ドライブが無かったため（FW の RAM セクタ 0・ブロック 0）。staging 920 KB の 2 倍を
  表す FAT が、スケッチの 4 KB に入らず、begin() が false だった。begin() は FAT で表せる最大の領域で立てるように
  （4 KB で 679 KB、ほかの系列の配置は同じ）。試験はドライブの大きさを先に確かめる。H417 で DFU・UF2・.bin・family 違い
  まで通った。DFU の 12 ブロック目の失敗は今回は出ていない。
- [x] X315 の usb-peer / usb-peer-host（6-3、Pico とつないだ、2026-11-20）が、どちらの向きでも列挙しない。DUT の USBHS（PD6 /
  PD7）は有効でプルアップを出し、UTMI のクロックも入っている（HSE・HSI とも）。→ 2026-11-20: ユーザーが Pico の線を Type-C から
  USB-A のコネクタに替え、usb-peer（デバイス）・usb-peer-host（ホスト）とも通った（436afff4）。X315 の第 2 世代 USBHS の初の実機確認。
- [ ] V205 の usb-peer-host-input が 3 回に 1 回、Pico の答えが「PD KEY 0」で落ちる（2026-11-15、core-20261114T151528Z-87219abd。
  流し直し 2 回は通った）。どの手順の何を待っているかを見て、DUT と Pico のどちらかを切り分ける。2026-11-18 にログを
  読んだ: キー 47 の押下は DUT に届き、離した報告を Pico が 2 秒送れなかった（DUT のホストが割り込み IN を取りに来ない）。
  直前に LED の SET_REPORT。同じ日に 3 回流して 3 回とも通った（core-20261117T235358Z-cc0546ec ほか 2 つ、USBFS の HCD の
  suspend の変更の後）。次に出たら DUT の割り込み IN の再開を追う。
- [ ] USB の未実装（docs/unimplemented.ja.md の 6・7 章）のうち、試験の相手に要るもの: デバイスの LS（FS の DCD の低速）、
  USBSS デバイスのストリーム（Mult>0 は 2026-11-17 に実装、ビルドのみ）、LPM（L1）。V103 と第 1 世代 USBHS のホスト、HS ハブの先の split、H41x の USBHS の
  ホストは 2026-11-17 に実装した（ビルドのみ）。配線待ち: V103 の USB の口 → Pico、V307 の USBHS（HSE の板）→ P4、V407 の
  USBHS1 → USB2 の PPPS ハブ（マルチ TT とシングル TT）→ Pico・P4（記録の `hub`）、H417 の USBHS → P4（SWD が外れるので
  単発、起動待ちのイメージで）。表は tests/TEST_PLAN.ja.md の「ホストのコントローラーの配線待ち」。
- [ ] UHSIF 200 MS/s を PC へ流す余裕を広げる。V5F の試作では 120 秒で崩れ 11 か所、ITCM に置いた既定の形では 30 秒で
  91 か所（USB は 407 MB/s まで、必要 400 MB/s）。USB の割り込みと送り出しの処理の置き場所、リングの深さ、ITCM に入れる順番。
- [x] CH32RVLogicCapture に、PC へ流すための深いリング（2〜16 個の塊）の API: `startRing()` / `readBlock()` / `blockOverruns()` /
  `samplesWritten()`（2026-11-20、GPIO のポート。V203・X035・H417 で確かめた）。UHSIF の経路は 200 MS/s の調査（上）と合わせる。
- [ ] H417 の HSADC（高速 ADC）を常駐ベンチで試す配線を足す（2026-11-14 ユーザー、あとで）。試験は periph の hs-adc（VDDIO の
  PF8〜PF10 に、プローブのラベル付きの線が要る。PC0〜PC3 は VIO18 なので使わない）。
- [ ] 常駐ベンチに USB PD の配線を足す（2026-11-14 ユーザー）。CH32RV のボード 2 台の CC の線どうしと GND をつなぐ（または両方の
  USB-C を C-to-C でつなぐ。そのときは VBUS もつながるので、ソース側が 5 V を出さない作りにする）。組み合わせは確認の薄い系列から
  （V205 PA0 / PA1、H417 PB3 / PB4、L103 PB6 / PB7、X035 PC14 / PC15 など）、ユーザーと決める。今は pd の記録を持つ台が無く、
  シンクを実機で確かめたのは X035（充電器）だけ。
- [ ] 上の配線ができたら、USB PD を CH32RV のボード同士で確かめる試験（ユーザー: ボード同士がいちばん良い）。片方を
  USBPDSource（VBUS の切り替えは何もしない形でよい）、もう片方をシンクにして、交渉（Source_Capabilities → Request →
  Accept → PS_RDY）を両側から見る pd-peer の case と、相手の口を書く記録の欄を足す。
- [ ] （優先度低、2026-11-14 ユーザー）UHSIF のスレーブのモード（外のマスターがクロックを出す形。WCH の FPGA 型・SoC 型）。
  レジスタは非公開だが、`libUHSIF.a` の中身は大部分が表（`UhsifTransition_Slave_FPGA` / `_Slave_SOC`、`UhsifWavedata_Slave_*`、
  `UhsifRegValue`）なので、取り出して docs/h417-uhsif.ja.md の読み方で解読できる見込み。未確認は外部クロックの設定（w1 の
  21:14、RCC のクロック源）と出力の向き。実機で確かめるには外からクロックとデータを出すマスターが要り、6-2 のプローブの線は
  UHSIF の端子（VIO18 の PD / PE）にほとんど届いていない。今は文書で未対応と書いている。
- [x] V3F を起動する API（2026-11-14: `CH32RV.startV3F()` / `isV3FRunning()` / `onV3F()`、`setup1()` / `loop1()`、例 DualCoreV3F。nano で 200 回の起動と結果、loop1 の数え上げ、V3F の millis / delay を確認。残りは V3F を外から止める仕組みと、V3F から V5F に頼む IPC）。元の項目: `ch32rv_v3f_mailbox` を包む API、その上の `setup1()` / `loop1()`、V3F 用の領域（V5F と別の
  SRAM のバンク、スタック）、V3F を止める仕組み、V3F から使ってよいコアの機能（Serial は不可から）。最悪は今の
  「起こして眠る」だけでもよい（利用者、2026-11-11）。
- [ ] H41x の HSEM・IPC の公開 API と V3F の周辺（2026-11-17、ビルドのみ）: `CH32RV.hsem*()` / `ipc*()`（ch32rv_multicore.h）、
  V3F の関数は割り込み有効、共有レジスタ（RCC・ポートの設定・AFIO・EXTI）はコアのロック（HSEM 31）、例 DualCoreMessages。
  実機: selfapi の test_cores_hsem・test_cores_ipc・test_cores_v3f_serial を H417（CPU Core = V5F と V3F）で。HSEM の 1 ステップの
  ロックの読みの値（RM は 0、EVT は LOCK | CID）を test_cores_hsem の raw で記録する。→ 2026-11-20 に h417qeu6-pico で、
  V5F の版は 4 件（hsem・heap・ipc・v3f_serial）、V3F の版は hsem・ipc が通った（V3F の版の heap と v3f_serial は対象外）。raw は
  どちらも 00000000（RM の読み）。IPC の往復は 4.75 µs。残り: V3F を外から止める仕組み、CPU Core = V3F のときに V5F を 2 つ目のコアにする形（今は眠ったままで IPC は false）。
- [ ] BOOT 域のブートローダー（DFU）から起動したときに V5F が動くことを実機で確かめる（入口は 0x08000400 で渡している）。
- [ ] 低消費電力（lightSleep / deepSleep）、`CH32RV.restart()`、WDT、フラッシュへの書き込み（CH32RV_FLASH_STORE、DFU）を
  V5F で確かめる。2026-11-13: restart()・WDT と CH32RV_FLASH_STORE（flash）は V5F で PASS。2026-11-14: lightSleep()・deepSleep() は
  それまで H41x では未対応を返していた（selfsys は skip）。RTC（F1 型。BDCTLR は 0x24 でビットの位置が違い、BKP のデータ
  レジスタが無いので RAM に置く）とストップ（V3F も待機中はストップ）を入れ、nano で V5F・V3F とも selfsys（RTC、200 ms の
  lightSleep）が通り、2.5 秒の lightSleep で石がストップに入ってクロックを立て直すこと、deepSleep のあとタイマーの起床で
  再起動することを確かめた。残りは端子での起床（system の case、6-2）と DFU。→ 端子での起床は、h417qeu6-pico の system::test_sleep
  （V5F）の「lightSleep pin」が 2026-11-18 に通っていた（core-20261118T004906Z-41e5c45d、プローブが PD4 を動かして起床）。
  残りは DFU（BOOT 域のブートローダーは USB 2.0 の口で、この台の USB は SS の線だけ: 配線待ち）。
- [x] DTCM の使い道（読むだけの定数、頻繁に読む変数）。V5F の HPE のハードウェアスタックは DTCM の先頭を使う（INTSYSCR
  0x0F、CSR 0xBC4）。DMA が要るものは共有 SRAM に残す。→ 2026-11-20: 今は仕組みを足さない。共有 SRAM も V5F から待ち無しで
  （docs/h417-cores-design.ja.md）、置き場で速さは変わらない。差が出るのは V3F とのバンクの取り合いだけで、測った取り合い
  （V5F の待ちのループが同じバンクを読み続ける）は待ちに休みを入れて直してある（wiring_time.c）。DTCM は DMA が届かないので、
  一般の変数の置き場にはしない。取り合いが見える用途が出たら、その変数に付ける印（.dtcm の節）を考える。
- [x] 利用者向けの文書（2026-11-14: README の CPU Core の行に全部あり、V3F を 2 つ目のコアとして使う案内も足した）: CPU Core メニュー、F_CPU と CH32RV_HCLK_HZ の違い、V5F では `mcycle` が読めない、デバッグは
  `--core 1` と `dbg resume --all`。
- [x] ITCM（128 KB）からあふれたコードは共有 SRAM で動き、命令キャッシュに収まらないと 2.5 倍遅い。どのコードを ITCM に
  優先して置くか → `CH32RV_RAMFUNC`（.highcode）が ITCM の先頭、残りはリンクの順（2026-11-20 に確かめて
  docs/h417-cores-design.ja.md に書いた。いちばん大きな例でも ITCM 115 KB・共有 SRAM 36 KB）。

## Clang だけにする（2026-11-12 に方針を決めた）

GCC を外し、ch32rv-toolchain（clang と lld、picolibc、compiler-rt、libc++、GDB）だけにする。中間の状態（両方を選べる、
裏で GCC を使う）は作らない。部品を ch32rv-toolchain で先に作り、揃ったらコアを一気に切り替える。計画の経緯は
docs/libc-runtime-plan.ja.md、評価は docs/clang-evaluation.ja.md。

- [x] コアの書き換え（コンパイラの分岐、newlib の名前、Compiler メニュー、xPack の依存を削除。Optimize に `-Oz`
  （既定）/ `-Os` / `-O2` / `-O3`、Floating Point の newlib の項目を picolibc の項目に）。手元の ch32rv-toolchain で確認。
- [x] ch32rv-toolchain v23.1.5-2（ライブラリ、gdb、割り込みのパッチ入り）を CI で作り、同梱を上げた（2026-11-12）。
- [x] main の CI で例を全部ビルドした。例の profile を全系列（27）に広げ、合わなかった例は要件で外した（ETHERNET、SERIAL1、
  variant の `#define` の名前を要件に書けるようにした）。
- [ ] I2C の FREQ の範囲は 4〜60 MHz（全系列のリファレンスマニュアル。V00x / M030 は 8〜48）。F1 型（V20x / V30x、L103 / M103、V103）と V205・V407 / V467 は、CPU Clock の項目ごとに APB1 を分周して 60 MHz 以下にした（2026-11-12）。タイマーは自分のバスのクロックで数える（`ch32rv_timer_clock_hz()`）。X315・H417・M030 はペリフェラルのバスを分周できない（I2C が HCLK で動く: 240 / 100 / 72 MHz）ので、H417 の EVT と同じく FREQ を上限（60、M030 は 48）で頭打ちにした。X315 / M030 の EVT は HCLK の MHz をそのまま書いて欄からはみ出させている。この 3 系列で I2C が実際に通信できるかは、相手をつないで確かめる。63 MHz を超える APB1 で I2C を実機で試したことはまだない。→ 2026-11-20 に見直し: H417（I2C が HCLK 100 MHz、FREQ 60 で頭打ち）は、h417qeu6-pico の wire で test_wire_output_only・10bit・general_call・slave が通る（2026-11-18 の結果）ので、通信はできる。落ちる 7 件は上の「潰れたクロック」（外付けのプルアップの台で測り直す）。X315・M030 はベンチの線が無い。
- [ ] L103 / M103・V205 の lightSleep() は停止モード 1（LPDS=0）で入る（2026-11-12）。LPDS=1 の停止モード 3 は FLASH_LP を先に決める必要があり、決めずに入ると戻らなかった。モード 3 の小さい電流を使うなら、FLASH_LP を設定して消費電流を測ってから。
- [x] selfsys で見えたこと（2026-11-12）: V103 の analogReadTemp() が 51 ℃ だった。V103 のセンサーは温度とともに電圧が上がる（EVT の TempSensor_Volt_To_Temper。データシートは傾きの向きを書いていない）のに、ほかの系列と同じ向きで換算していた。工場の校正値（EVT の 0x1FFFF720 など。mV と温度の 1 点）も使うようにし、ベンチの 8 台で 20.7〜29.8 ℃（2026-11-13）。M030 の wdtEnable() の拒否は仕様どおり（IWDG が無く WWDG だけ。72 MHz で最長約 29 ms）。
- [x] H417 の V3F ビルド（CPU Core = V3F）で、SerialDMSeq に送った行がときどき崩れる（selfcheck の `ready <nonce>` が `UNKNOWN ready` になる。0.30.5 で 10 回中 2 回、0.30.6 で 4 回中 1 回）。ch32rv の記録では、ホストが送った 8 フレームはどれも DUT が受け取りを返している（2026-11-12、[ch32rv]）。V5F ビルドでは起きない。selfcheck / selfbus は UNKNOWN で行全体と語の数を返すようにした。2026-11-13: 余計に来ていた。monitor を開いた直後の最初の行の先頭に 0xFF が 1 バイト付く（selfcheck・selfbus で計 3 回）。原因は LinkE の AttachChip で、走っている V3F（core 0）の a0 をおよそ 2 回に 1 回 0 にする（V103 の s1、V205 の s0 と同じ種類。[ch32rv] が確認）。available() の入口で止められると this が 0 になり、read() の -1 が行の先頭に入っていた。DUT とコアの側の問題ではない。ch32rv の 1c45634（未リリース）で、core 0 だけが走る H417 は接続のあとにプログラムを再起動するようになった（warning `attach-corrupts-register`）。その版を CH32_TEST_CH32RV で使い、V3F の selfcheck 3 回・selfbus 2 回がすべて通り、0xFF は出なかった（2026-11-13）。同梱を ch32rv 0.30.8 に上げ、V3F の selfcheck 3 回・selfbus、V5F・V205・V103・X035 の selfcheck が通った（2026-11-13）。
- [x] X305 の SPI1 の既定を PA5 / PD3 / PA6 にした（X315 と同じ）。候補の端子を絞るとき、デバッグ・起動設定の端子を後回しにする。同じ規則で H415 / H416 の Wire は I2C1 の PB6 / PB7 になった（H417 は I2C2 のまま）。
- [x] Network ライブラリの `ETH` を CH32V208CBU6 では定義しない（Ethernet が無い、device-data capabilities.csv）。ANY と他の V208 は今までどおり。
- [x] リポジトリの名前を ch32rv-toolchain に変えた（2026-11-12）。コアの tool 名、index、環境変数（`CH32_TOOLCHAIN_BIN`）も合わせた。
- [x] 割り込みは `WCH-Interrupt-fast`（clang のパッチ）にした。naked 関数で HPE に任せる案は、FPU のあるコアで浮動小数点の
  レジスタを壊し、命令も多い。HPE の無い V103 は普通の `interrupt`（`CH32RV_INTERRUPT`、internal/ch32rv_interrupt.h）。
  入れ子は、V4F の 4 段も HWSTKOVEN が 0 なので HPE の 3 段を超えない。
- [x] 実機（手元で作ったパッチ入りの版、2026-11-12）: X035 は 22 件すべて、V003 / V006 / V307 / V203 / L103 / V103 / M030 も
  今回の変更による失敗は無し。L103 の synckit は `-Oz` のまま 47 µs（前は 55 µs で上限 50 µs を超えていた）。
  LinkE の uart の失敗は、console-sdi の SDI のデータが LinkE に残っていたため（ch32rv 0.30.2 の問題。b1148af で直った）。
  V003 の periph の NG は、GCC の版でも出る PC4 の刺激の飛び込み（環境）。
- [ ] リリースの版で、もう一度全部の系列を流す。
- [x] コアが持つ libc の代わりを picolibc と compiler-rt と比べた（2026-11-12）。時刻の関数（picolibc より 1.3〜9.4 KB 小さい）、
  割り算、変換、clz / ctz はコアの方が小さいので残す。引き算は外した。memcpy などは速さ、random() は名前の衝突のために残す。
- [x] printf-float.ja.md と README の大きさの表を clang で測り直した（2026-11-12）。parseFloat の整数の計算の費用だけ GCC の頃の値。
- [x] clang は rv32ec（V003）で定数の掛け算に `__mulsi3` を呼ぶ件を洗い出した（2026-11-12）。割り込みの中では EXTI の処理だけで、
  `__builtin_ctz` を clang が掛け算に開いていた。`internal/ch32rv_bits.h`（掛け算の無いコアでは比較とシフト）に替えた。
  CH32RVSyncKit のプールも同じ。残りは数字の表示と時刻の計算（`-Oz` が呼び出しを選ぶ。UART の 1 文字より短い）。
- [ ] Windows の ch32rv-toolchain が 251 MB（ツールが別々の実行ファイル）。要るツールだけにするか、ハードリンクにできるか。
- [x] 同梱の ch32rv を 0.30.4 に上げた（V103 の接続の修正 e958bbe と 15d9a89、LinkE の SDI が残る件の b1148af が入った版）。
  リリースの版の実機の試験で確かめる。

## PIOC（CH32X035、CH32V205、CH32H41x）

- [x] PIOC を読み込んで動かすライブラリ（2026-11-19、ユーザー: 試作して基本は入れる方向。保留を解除）。プログラムは
  スケッチの .S にコアのマクロ（cores/arduino/ch32rv_pioc.inc）で書き、コアの clang が組む（別のアセンブラは使わない）。
  CH32RVPioc がローダー、X035 / V205 の SRAM の 4 KiB は使うスケッチでだけリンカが空ける。H417・X035・V205 で pioc の試験が通る。
  EVT のバイナリも他のアセンブラの出力も内蔵しない。
- [ ] PIOC の実用的なサンプルを増やす（ユーザー、2026-11-19）。PiocWs2812 を足した（2026-11-19。データは loadSram() で SRAM に、
  タイミングは clockHz() から。H417・X035・V205 で pioc::test_pioc_ws2812 が WireSkein の rgb_led で通る）と PiocUartTx（共有
  レジスタで 24 バイトずつ。9600〜1M bps、pioc::test_pioc_uart_tx が WireSkein の uart で通る）と PiocIrNec（NEC の送信、
  38 kHz の搬送波。pioc::test_pioc_ir_nec が WireSkein の ir で通る）と PiocPulseIn（パルス幅の測定。プローブの PWM の fixture を
  IO0 に plan して pioc::test_pioc_pulse_in）、PiocUartRx（プローブの UART の fixture の TX を IO0 に plan して
  pioc::test_pioc_uart_rx）、PiocSpiTx（SPI の送信、IO0 に SCK・IO1 に MOSI。H417・V205 で pioc::test_pioc_spi_tx が
  WireSkein の spi で通る。2026-11-20）と PiocIrNecRx（NEC の受信。PIOC が 1 フレームを 32 ビットに読み解く。プローブの
  spi-controller が MOSI に受光モジュールの出力の形を出し、H417・X035・V205 で pioc::test_pioc_nec_rx が通る。2026-11-20）を足した。
  PiocOneWire（1-Wire のマスター、DS18B20 の温度。端子を開放ドレインにする `setOpenDrain()` を足した。H417・X035・V205 で、
  デバイスの無い線で pioc::test_pioc_onewire が WireSkein の onewire で通る。2026-11-20）も足した。残り: 実際の DS18B20 での確認（部品が要る）。
  CH32RVPioc の `_pads` が int8_t で、X035 の PC7（135）を負に読み、end() が端子を戻していなかったのも直した。どれも自前で書き起こす（EVT のコードは持ち込まない）。
  README の「使い道と限界」に沿って、PIOC に向いた用途を選ぶ。
- [ ] PIOC の L2 以降のマクロ（待ち時間、構造化、部品、雛形）は設計してから入れる（ユーザー、2026-11-19）。置き場はコアでなく
  アプリ側: スケッチのプロジェクトのファイルとして持ち、版の差や環境の差を無くす（ユーザー、2026-11-19）。案は
  work/pioc-handoff-20261119/macro-spec.ja.md。SFR の名前を C++ と共有する形（#define）もそこで決める。
- [x] **実験済みの機能の導入（2026-11-19 に実験、2026-11-20 に使い方を比べて推奨の形で入れた。ユーザー: API もいろいろ使い方を
  検討してから推奨の形で入れる）**。形と理由は [docs/adr/0018-system-primitives.ja.md](docs/adr/0018-system-primitives.ja.md):
  H41x の Watchdog メニュー、2 コアの malloc のロック（selfapi::test_cores_heap が h417qeu6-pico で通る）、
  実行時の割り込み `ch32rv_irq_attach()`（ベクタ表は写さず Default_Handler から振り分け。X035・V003・V203・H417 で
  selfapi::test_irq_attach）、CH32RVDma（selfapi::test_dma）、PMP は Stack Check メニューの中のスタックの守り。
- [ ] PIOC のエミュレータと pytest の pioc-sim:// は後で検討（ユーザー、2026-11-19。引き継ぎ資料の 5 章）。
- [ ] イベントの payload の持ち方（コピー・借用・所有の移転・共有 pool）と、複数の購読者への配達で誰が解放するか。
- [ ] 代表ユースケース「X035 の USB PD シンクと PPS の操作画面」。PD の充電器・画面・ボタンが要る。

## 永続化

決めてコアに入れた（PERSIST-01、[docs/flash-store.ja.md](docs/flash-store.ja.md)）。

- 残り（必要になったら）: キーと値の保存、内蔵フラッシュ上のファイルシステム（大きい系列だけ）、書き込みツールの既定の消去範囲。

## examples

例を足すときの決まりは [docs/examples-build-rules.ja.md](docs/examples-build-rules.ja.md)。

- [ ] 実機での確認がまだのもの: 第1弾（UART の操作、PWM の波形、ハードウェアの I2C の対向通信・相手不在・復旧、外部 SPI Flash の ID）、
  第2弾（ボタンの押下・長押し・解放、可変抵抗と PWM、USB の再接続時の中立、ゲームパッドの軸・両端・中立帯を PC の確認画面で）。
- [x] X035 のハードウェアの I2C（Wire、ba8dc851 の repeated START を含む）を線で確かめる。→ 2026-11-14、常駐ベンチの x035c8t6-pico（PA11 / PA10、プローブの i2c-target）で wire の 3 件が通る（S W Sr R Sr W P と S W Sr R P S W P を WireSkein で 18 件 OK、受け手も）。以下は前の記録。ベンチの CH32X035C8T6（OEP 4-2）は、
  ロット番号の後ろから 5 桁目が 0 の、I2C と ADC の ch3 / 7 / 11 / 15 が使えない個体だった（データシート、device-data の errata
  `x035-adc-ch-i2c-unavailable`。2026-11-03 に oep が線と PVD で確認。前にここに書いた analogRead が 0 近くになる件のうち、IN15 はこれで説明がつく。A0（IN0）が 3〜9 だったのは説明がつかないが、2026-11-03 には IN2 が 3.3 V で 4044、0 V で 13 と正しく読めた）。
  ロットの後ろから 5 桁目が 0 でない C8T6 / R8T6 を用意して（ユーザーの判断）、PA10 / PA11 をプローブにつなぎ、case wire を流す。
  同じロットの F8U6 では PC16 / PC17（route 2）の I2C が通っていた（2026-09-30）ので、制限は端子によって違うのかもしれない。
- [ ] V103R8T6（d39e、CH549 の Link）が休止の試験の最初の休止から起きず、デバッガも応答しない（2026-10-28）。Link では電源を切れず、
  ハブの口の電源でも戻らない。手で電源を入れ直してから、RTC（LSI の測定・アラーム）を調べる。それまで V103 は RTC の系列から外している。
- [ ] まだ無い例（下の表 A）: `PeripheralRoutes`（配線が要る）、`TlsClient` は既存の HttpsGet（CA の検証・時刻・誤りの表示）が示している。
  2026-11-20: `MidiChannelsAndReceive`・`UsbNcmNetwork`（Network）・`PpsVoltageControl`（CH32RVUsbPd）・`HostHidReports` / `HostHubDevices`（CH32RVUsbHost）・`UsbReconnect` / `VendorBulkPeer`（CH32RVUsbDevice、後者は PC の PyUSB のスクリプトと組）を足した（ビルドだけ）。→ VendorBulkPeer は 2026-11-20 に H417 の SS で 3 つの段とも通った（PC 側のスクリプトの再開の段を直した: 一度に書くとまた止まる）。FirmwareVendor・FirmwareBootMode も足し、FirmwareVendor は H417 の SS で入れ替えと断りを確かめた。同日、H417 の SS で FirmwareCDC・MidiEcho・CompositeCdcMidi・UsbReconnect・ReconfigureVendor・MscCdcBlocks・MidiChannelsAndReceive も確かめた（直したもの: CDC / ベンダーの受信の輪を 1 パケットまで広げる、firmware_cdc.py の再試行、vendor_bulk_peer.py の再開）。残り: UART を使う例（UsbConsole・UsbBringUpCheck・DescriptorDump。この台の Serial の端子にプローブの線が無い）と UsbNcmNetwork（ネットワークの設定に root）、ホストの例（HostHidReports・HostHubDevices）。
  `MutualTlsClient`（NetworkClientSecure、クライアント証明書、手元の Python のサーバーと組）は CH32V307 で確かめた。`AdcBlockStatistics` は既存の AdcStreamStats（最小・最大・平均・
  取りこぼし）、`SensorConsole` は既存の ButtonBlinkConsole（周期・ISR からの通知・コマンド）が同じことを示すので作らない。2026-11-19: `RgbLed` は既にあった。`SerialBridge`（CH32RV）・`I2CEeprom`・
  `SoftI2CRegisterRead`（Wire）・`SpiSharedBus`・`SoftSpiDevice`（SPI）を足した（ビルドだけ確認。部品・配線での動作は未確認）。
- [x] **TLS の 2 つの問題（2026-11-20、CH32V307 で見つけた。どちらも V307 の 128 KB のヒープ不足）**:
  - [x] HttpsGet が `TLS connect failed: -0x2700, verify flags 0x8`（NOT_TRUSTED）→ 直した。試験のサイトの鎖が YR1 → Root YR
    （ISRG Root X1 の相互署名、RSA-4096）に変わり、検証の RSA-4096 が増えた。V307 の RAM 128 KB では TLS の最中の空きヒープが
    6.9 KB で、署名の検証が PSA のメモリー不足（-141）になり、NOT_TRUSTED に見えていた（DUT の上で x509 の検証だけなら通る、
    一時的な検証のコールバックで突き止めた）。V307 / V317 の 128 KB の時だけ lwIP の MEM_SIZE を 32 → 20 KB にして、
    接続前の空きが 47.5 → 59.9 KB、HTTP 200 を確かめた（libraries/Network/src/lwipopts.h）。送受信の速さは変わらない
    （eth の試験を一時的に 192 KB + 128 KB で流して、20 KB でも 32 KB でも TCP 9.49 / 9.41 Mbit/s）。
    ついでに eth の試験の証明書を 2 日前から有効にした（時計を合わせた後に少し遅れた DUT が「まだ有効でない」と断っていた）。
  - [x] `openssl s_server -www` を相手にすると、TLS 1.3 でハンドシェイクのあとの記録の復号が MAC の誤り（-0x7180）になっていた
    （v0.8.0 から）→ 同じヒープ不足だった。上の lwIP の変更のあと、`-www` で返事を読めた（大きなページを 1 つの記録で返す相手）。
  - ついでに分かったこと: V307 で eth の自動の試験は Flash / RAM Split の既定（RAM 64 KB）でヒープ不足として記録されるだけで、
    TLS を確かめていない。クライアント証明書のハンドシェイクはスタックを約 4.2 KB 使う（予約 4 KB より深い。Stack Check を付けると
    panic になる）。
- [ ] 応用の候補: ESP32 の HTTPClient / WebServer や EVT の MQTT を参考にした HTTP 取得・小さな Web server・telemetry、
  SDK の ETH↔UART 中継、audio 録音、camera 表示。TCP / TLS があるだけで HTTP / MQTT の公開 library もあるとは扱わず、
  library の採用とメモリ・接続復旧を先に確認する。
- ESP32のWi-Fi / BLE / ESP-NOW / Matter / Zigbee / OpenThread / RainMaker、ULP、専用のOTA / provisionerは、
  同名例をそろえるためには追加しない。CH32の`EVT(CH32V20x):BLE`も含め、対応ハードウェア・通信stack・公開APIの
  採用を先に判断し、必要な用途が決まったら別項目にする。
- EVTのFreeRTOS / RT-Thread等のOS移植、CoreMarkやボード固有の製品デモは、一般向けexamplesへ一括で取り込まない。
  OS向けのqueue / semaphoreの題材はTaskKit / SyncKitの実行モデルで学べる形へ直し、計測や内部検証は`tests/`側へ置く。

### 比較元と選び方

- このコアの `libraries/*/examples` と公開ヘッダを確認し、同じ機能の例がある場合は、その例で扱わない使い方を候補にした。
  `tests/` の検証コードは参考にできるが、配線・操作・結果を説明する利用者向け examples の代わりとはしない。
- 表中の `ESP:` は、指定された
  `~/.arduino15/internal/esp32_esp32_3.3.12_feadd113249efafd/libraries/` からの相対パス。
  `EVT(CH32系列):` は `~/dev_wch/CH32系列/EVT/EXAM/` からの相対パスで、例のディレクトリを示す。
  調査した SDK は CH32V003 / V006 / V103 / V20x / V205 / V307 / V407 / X035 / X315 / L103 / M030 / H417。
  同じ機能が複数の SDK にあるときは代表例だけを記す。これは全系列で利用可能という意味ではない。
- **A は既存 API で例を組み立てる候補、B は公開 API・ドライバ・ライブラリの採否や拡張も先に検討する候補。**
  SDK に例があるだけで、Arduino の API があるとは扱わない。実装側の検討は
  [FEATURE_CANDIDATES.ja.md](FEATURE_CANDIDATES.ja.md) と対応付け、着手時にソースと対象ハードウェアで確かめる。
  サポート状況・測定値・版ごとの制約はこの一覧へ複製せず、各ライブラリの説明へ集約する。
### A. 既存 API を使って追加する例

| 機能 / スケッチ案 | 既存例に足りない使い方・例で示すこと | 比較元・参考例 |
| --- | --- | --- |
| PWM：`PwmFrequency` / `RgbLed` | `analogWriteFrequency()`で周波数とdutyを別に設定し、同じタイマを共有するピンへの影響を観察する。RGB LEDの配線、反転、明るさ補正を示す。 | `ESP:ESP32/examples/AnalogOut/ledcFrequency`、`ESP:ESP32/examples/AnalogOut/ledcWrite_RGB`、`EVT(CH32V003):TIM/PWM_Output`。LEDC専用APIは移さない。 |
| 複数UART・ピン経路：`SerialBridge` / `PeripheralRoutes` | UART同士の双方向中継と送信詰まりを扱う。`begin(..., rx, tx)`・`setPins()`・`setRoute()`で、選べる経路と不正な組合せを示す。Wire / SPIにも同じ考え方を展開する。 | `ESP:SPI/examples/SPI_Multiple_Buses`、各公開ヘッダのピン経路API。必要な周辺回路数はビルド条件で絞る。 |
| I2C master：`I2CRegisterRead` / `I2CEeprom` | `I2CScanner`の次に、レジスタ番号を書いて`endTransmission(false)`から`requestFrom()`する読み取り、返却長とNACKの確認、EEPROMのページ境界と書き込み完了待ちを示す。 | `ESP:Wire/examples/WireMaster`、`EVT(CH32V003):I2C/I2C_EEPROM`。 |
| SPI実機通信：`SpiFlashId` / `SpiSharedBus` | `SPILoopback`の次に、外部SPI FlashのID等を読み、CS、mode、周波数、ビット順を説明する。複数CSを持つ同一バスと、複数SPIバスを区別し、機器ごとにtransactionを組む。 | `ESP:SPI/examples/SPI_Multiple_Buses`、`EVT(CH32X035):SPI/SPI_FLASH`。最初のFlash例は消去・書き込みを行わない。 |
| ソフトウェアバス：`SoftI2CRegisterRead` / `SoftSpiDevice` | スキャン・ループバックだけでなく、同じ部品をWire / SPI版とソフトウェア版で使う。ピン選択、共有GND、pull-up、CPUを使う転送の違いを比較する。 | 既存 `SoftI2CScanner` / `SoftSPILoopback`、上のI2C / SPI実機通信例。 |
| USB MIDI：`MidiChannelsAndReceive` | MIDIチャンネルとUSB cable番号を区別し、受信メッセージでLED等を操作する。constructorでcable数を設定し、送信先cableを明示してPC上の複数portと対応付ける。 | `ESP:USB/examples/MIDI/MidiMultiChannel`、`ESP:USB/examples/MIDI/ReceiveMidi`、既存 `MIDI` / `MIDIController` / `MIDIInterface`。 |
| USBの継続動作：`UsbReconnect` / `VendorBulkPeer` | 既存のevents例・Host切断通知を踏まえ、送信待ちの扱い、切断後の状態破棄、再接続後の再開まで示す。Vendor転送はPC側の短い送受信例と組にし、部分転送と送信詰まりを再現する。 | `ESP:USB/examples/USBVendor`、既存 `SerialEvents` / `USBVendor` / `VendorBulk` / `DeviceInfo`。単なる通知表示を増やすものにはしない。 |
| USB Host HID：`HostGamepad` / `HostHidReports` | `onGamepad()`で軸・ボタンを読み、consumer / system controlやraw reportを機器ごとに識別する。既存Keyboard / Mouse例の外にあるreport descriptor、output / feature reportの用途も示す。 | `ESP:USB/examples/USBHostGamepad`、`ESP:USB/examples/USBHostHIDCombo`、既存HostのHID公開API。 |
| USB Hostの複数機器：`HostHubDevices` / `HostComposite` | hub経由の接続、複合機器の複数interface、機器ごとのaddressとportを表示し、片方を抜いても他方を扱える例にする。VBUSの供給条件、対象とするhub構成をREADMEに書く。 | `ESP:USB/examples/USBHostCDC_MSC_HID`、既存 `DeviceInfo`と`getDevices()` / `getHubInfo()`。 |
| USB PD：`PpsVoltageControl` | `ListProfiles`・`CycleVoltages`の次に、PPSの範囲を確認して`requestProfile()`へ電圧・電流を渡す。拒否、PPSのない充電器、抜き差しを扱う。入力処理を状態機械にしても、契約要求API自体は待つ呼び出しである点を説明する。 | `EVT(CH32X035):USBPD/USBPD_SNK`、既存PD例と公開API。電圧が実際に変わるので、負荷と基板の許容範囲を先に明示する。 |
| ネットワーク導入：`EthernetDhcp` / `UsbNcmNetwork` | `Network`の入口として`ETH`またはUSB NCMでリンクとIP取得を確認し、固定IP、DHCP、切断・再接続、待ち時間の上限を示す。`UsbNetworkPing`の手作りARP / ICMPから、IPスタックを使う例へつなぐ。 | `ESP:Ethernet/examples/ETH_LAN8720`、`EVT(CH32V307):ETH/DHCP`、既存 `UsbNetworkPing`。ESP32向けPHY設定ではなくCH32側の接続方法で書く。 |
| TLS：`TlsClient` / `MutualTlsClient` | `NetworkClientSecure`でCA検証、ホスト名、接続前の時刻確認、エラー表示を示す。次段階でクライアント証明書を扱い、証明書・鍵の寿命と保管先を説明する。固定の外部サービスや期限付き証明書への依存を避け、ローカル相手の準備手順を付ける。 | `ESP:NetworkClientSecure/examples/WiFiClientSecure`、既存`NetworkClientSecure`の公開API。通信路とメモリの実機確認を前提にし、未検証の組合せを動作済み例として配らない。 |
| 取り込みデータの利用：`AdcBlockStatistics` / `LogicPulseDecode` | `HsAdcStream` / `LogicStream`の取得APIを使い、平均・最小最大やパルス幅の解析へ進む。取得側と処理側を分け、バッファの所有期間、処理落ちの検出、停止・再開を示す。 | 既存 `HsAdcRead` / `HsAdcStream` / `LogicBurst` / `LogicStream`、`EVT(CH32H417):HSADC/HSADC`。 |
| タスク・同期の応用：`SensorConsole` | 個々のプリミティブの例から、周期計測・コマンド受付・表示の組合せへ進む。ISRから通知して、短い処理を順に進め、キュー満杯時にも止まらない例にする。 | `ESP:ESP32/examples/FreeRTOS/Queue`、既存TaskKit / SyncKit例。FreeRTOSのスレッドモデルを移すのではなく、このコアの実行モデルで構成する。 |

済み（表から外した）: `ButtonDebounce` / `AnalogInputToPwm`、`SystemInfo`、`ResetReason` / `WatchdogRecovery`、`InternalAnalogRead`、`PwmFrequency`、`SerialLineReceiver` / `SerialPacketReceiver`、`I2CRegisterRead`、`I2CSlaveRegister`、`I2CTimeoutRecovery`、`SpiFlashId`、`ServoPulseControl`、`ButtonKeyboard` / `ButtonMouse` / `AnalogGamepad`、`EthernetStatus`、`TcpEchoServer` / `UdpEcho` / `HttpGet`、`NetworkTime` / `MdnsWebServer`、`LogicPulseDecode`。

### B. API・ドライバ・ライブラリ整備と組にする例

ここは「SDKにあるのに例がない」機能を見落とさないための候補。採用を決めたものから公開APIを設計し、
Arduinoのピン指定・タイマ所有・割り込み・転送バッファの規約に合わせる。
SDKの初期化をそのまま貼り付けて、コアの管理する周辺回路を上書きするスケッチにはしない。

| 機能 | 追加したい例と先に決めること | 比較元・参考例 |
| --- | --- | --- |
| ADCのハードウェア連携 | タイマ / 外部入力による開始、injected変換、analog watchdogのしきい値通知、dual ADC。トリガ・DMA・タイマの取得と解放を定義する。 | `EVT(CH32V307):ADC/TIM_Trigger`、`EVT(CH32V003):ADC/AnalogWatchdog`、`EVT(CH32V307):ADC/DualADC_RegSimul`。 |
| ADC前段・OPA・CMP | 入力増幅、比較器のしきい値、比較結果の割り込み・PWM停止。入力電圧範囲、出力経路、対応するアナログ回路のAPIを決める。 | `EVT(CH32L103):ADC/ADC_PGA`、`EVT(CH32V006):OPA/OPA_PGA`、`EVT(CH32V006):OPA/CMP_BKIN`。 |
| DAC | 直流出力からタイマ同期の波形出力・DMAへ進む。PWMとの違い、更新率、バッファの寿命を示す公開APIを用意する。 | `EVT(CH32V307):DAC/DAC_Normal_OUT`、`EVT(CH32V307):DAC/DAC_DMA`、`EVT(CH32V307):DAC/DualDAC_SineWave`。 |
| タイマ出力・同期 | one-pulse、output compare、複数タイマ同期、相補PWM・dead time・break。周期callbackは既存`TimerResource`を入口にし、簡易Tickerが必要かは別途判断する。 | `ESP:Ticker/examples/TickerBasic`、`EVT(CH32V003):TIM/One_Pulse`、`EVT(CH32V003):TIM/ComplementaryOutput_DeadTime`、`EVT(CH32V003):TIM/Synchro_Timer`。 |
| 低消費電力タイマ | LPTIMの低電力中の周期起床、PWM / one-pulse。通常タイマ、`millis()`、復帰後の時間の関係を定義する。 | `EVT(CH32L103):LPTIM/LPTIM_LP_WakeUp`、`EVT(CH32L103):LPTIM/PWM_OnePulse_SingleMode`。 |
| UARTの高速受信 | DMA + idleによる可変長受信、二重バッファ、エラーの通知。受信callbackの実行文脈、未処理データの所有権を先に決める。→ 2026-11-17 実装（ビルドのみ）: `setRxBufferSize()`・`setTxBufferSize()`（ESP32 の名前）のバッファを DMA が運ぶ。callback は今の`onReceive()` と同じく loop() の後、受信の DMA は循環で、あふれたら古いものから上書きして UART_BUFFER_FULL_ERROR。例は SerialDmaBuffers。二重バッファの公開 API は作らない（バッファは Serial のもの）。 | `ESP:ESP32/examples/Serial/OnReceiveError_BREAK_Demo`、`EVT(CH32V307):USART/USART_DMA`、`EVT(CH32V307):USART/USART_Idle_Recv`。 |
| UARTの専用モード | RTS / CTS、RS-485の送信方向制御、half-duplex / single-wire、同期転送。外付けtransceiverと電気的な接続を分けて説明し、必要なAPIを選ぶ。 | `ESP:ESP32/examples/Serial/HardwareFlowControl_Demo`、`ESP:ESP32/examples/Serial/RS485_Echo_Demo`、`EVT(CH32V003):USART/USART_HalfDuplex`、`EVT(CH32V307):USART/USART_SynchronousMode`。 |
| ソフトウェアUART | 短い送受信と他処理との共存。採用するライブラリ、タイミング制約、受信割り込み・タイマ競合を検討する。 | `EVT(CH32V003):APPLICATION/SoftUART`。 |
| SPIの専用モード | slave受信とmaster側の対になる例、DMA、hardware NSS、half-duplex、CRC。既存SPI APIの拡張か別APIか、対応範囲を決める。 | `EVT(CH32V003):SPI/SPI_DMA`、`EVT(CH32V003):SPI/FullDuplex_HardNSS`、`EVT(CH32V003):SPI/1Lines_half-duplex`、`EVT(CH32V307):SPI/SPI_CRC`。 |
| I2Cの追加モード | DMA、10 bit address、PEC、必要なら複数master。通常Wireとの相互運用・未対応時の扱いを決め、ニーズのあるモードから例にする。 | `EVT(CH32V003):I2C/I2C_DMA`、`EVT(CH32V003):I2C/I2C_10bit_Mode`、`EVT(CH32V307):I2C/I2C_PEC`。 |
| I3C | controllerによる機器列挙・転送、対応する場合のIBI / target。I2Cの例と分け、専用driverと公開APIを検討する。 | `EVT(CH32V407):I3C/I3C_DMA_Controller`、`EVT(CH32H417):I3C/I3C_Controller_Check_ZeroIBI`、`EVT(CH32H417):I3C/I3C_DMA_Target`。 |
| CAN / CAN FD | 2台での送受信、filter、bus-offからの復帰。transceiver・終端抵抗と対応形式を説明する。共通`HardwareCAN`の型だけでなく実際のdriverが必要。 | `ESP:ESP32/examples/TWAI/TWAItransmit`、`ESP:ESP32/examples/TWAI/TWAIreceive`、`EVT(CH32V307):CAN/Networking`、`EVT(CH32L103):CAN/CANFD`。 |
| I2S / SAI / DFSDM | tone出力、mic収録、codec loopback、デジタルfilter。USB Audioの生成データと区別し、DMAとaudio bufferのAPIを設計する。 | `ESP:ESP_I2S/examples/Simple_tone`、`ESP:ESP_I2S/examples/Record_to_WAV`、`EVT(CH32V307):I2S/I2S_DMA`、`EVT(CH32H417):SAI/SAI_Play`、`EVT(CH32H417):DFSDM/DFSDM_I2S_Audio`。 |
| ファイルシステム・SD | ファイル作成・追記・読み戻し、ディレクトリ、取り外し、満杯時の扱い。SPI / SDIO / SDMMC / eMMCのblock転送と、ファイルシステムの採用を分けて決める。 | `ESP:SD/examples/SD_Test`、`ESP:LittleFS/examples/LITTLEFS_test`、`EVT(CH32V307):SDIO/SDIO_SD_FATFS`、`EVT(CH32H417):APPLICATION/FATFS_eMMC`。既存`MSCSdCard`はファイル操作の例ではない。 |
| USBストレージとファイルの共有 | Host MSCからファイルを読む、SDをDevice MSCとして公開する際の所有権切替。PCとスケッチが同時に同じFSを書き換えない構成を決める。 | `ESP:USB/examples/USBHostMSC_Test`、`ESP:SD_MMC/examples/SD2USBMSC`、既存 `MSCBlockDump` / `MSCSdCard`。FSとHost側block deviceの接続が前提。 |
| 更新・bootloader連携 | ファイルからの更新、USB DFU / Firmware MSC、失敗時の復旧。書き込み範囲、イメージ検証、boot手順を確定してから例にする。 | `ESP:USB/examples/FirmwareMSC`、`ESP:Update/examples/SD_Update`、`EVT(CH32V003):USART_IAP`。既存`DfuRuntime`による更新側への移行と、実際の書き換え処理を区別する。 |
| 外部メモリ | QSPI FlashのID・読み出しからmemory mapへ、FMC / FSMCのSRAM / SDRAM、PSRAMへ進む。起動時設定・リンカ・DMAが参照できる領域を決める。 | `EVT(CH32V205):QSPI/QSPI_FLASH`、`EVT(CH32H417):QSPI/QSPI_MemoryMap`、`EVT(CH32H417):FMC/SDRAM_16bit_DMA`、`EVT(CH32V407):PSRAM/PSRAM`。 |
| カメラ・表示 | DVPからのframe取得、SPI LCD、LTDC表示、GPHAによる描画。sensor / panel driver、frame bufferとメモリ要件を定義し、その後USB Videoへ実映像を渡す応用を作る。 | `ESP:ESP_Video/examples/dvp_camera`、`EVT(CH32V307):DVP/DVP_TFTLCD`、`EVT(CH32V103):SPI/SPI_LCD`、`EVT(CH32H417):LTDC/LTDC_Display`、`EVT(CH32H417):GPHA/GPHA_M2M_WithLCD`。既存`VideoCamera`はテスト画像の例。 |
| 精密な波形入出力・PIOC・ARGB | WS2812、IRの送受信、1-wireを題材にし、PIOC実行codeや専用driver、DMA / タイマとの関係を整理する。ESP32 RMTのAPIの移植とは分ける。 | `ESP:ESP32/examples/RMT/RMTWrite_RGB_LED`、`EVT(CH32X035):APPLICATION/WS2812_LED`、`EVT(CH32X035):PIOC/PIOC_NEC`、`EVT(CH32X035):PIOC/1_Wire`、`EVT(CH32X315):ARGB/Gradient`。 |
| CRC・乱数・hash | CRCの既知データ検算、RNGの失敗処理、採用した暗号libraryによるhash計算。`random()`と鍵生成用乱数を混同しない公開窓口を検討する。 | `ESP:Hash/examples/SHA2`、`EVT(CH32V307):CRC/CRC_Calculation`、`EVT(CH32V307):RNG/RNG`、既存 `RandomNumbers`。CRC周辺回路だけで暗号hashを実装できるとは扱わない。 |
| 電源・保護・診断 | PVD通知、電源低下からの復帰、WWDG、Flash / RAM ECC診断。通常のIWDGやリセット理由の例と分け、保護設定の寿命と回復手順を設計する。 | `EVT(CH32V003):PWR/PVD_VoltageJudger`、`EVT(CH32V003):WWDG`、`EVT(CH32M030):FLASH/FLASH_ECC`、`EVT(CH32H417):ECDC/RAM_Block`。 |
| clock・RAM実行・高性能コアの資源 | clock出力・校正、RAM実行、cacheとDMAの整合、IPC / HSEMによるコア間データ交換。コアの起動・クロック設定との境界、共有メモリと同期APIが前提。単独コアのTaskKit例で代用しない。 | `EVT(CH32V003):RCC/MCO`、`EVT(CH32M030):RCC/HSI_Calibration`、`EVT(CH32X035):RunInRam/RunInRAM_Select`、`EVT(CH32H417):CPU/ICache`、`EVT(CH32H417):CPU/IPC`、`EVT(CH32H417):CPU/HSEM`。 |
| 特殊な高速・専用インタフェース | UHSIF転送、SerDes、SWPMIの最小通信と相手側の例。必要なボード・相手機器が用意できる用途から、専用driverとAPIを検討する。 | `EVT(CH32H417):UHSIF/UHSIF_SLAVE`、`EVT(CH32H417):SerDes/FullDuxTrans`、`EVT(CH32H417):SWPMI/SWPMI_SwpCard`。 |

済み（表から外した）: 通常 ADC の連続取得（CH32RVAdcStream）、タイマ入力（CH32RVTimerInput）、RTC、sleep / standby、フラッシュへの設定保存（`FlashSettings`）、touch 入力（`TouchRead`）。

## USB

- [x] **記録の usb を、口ごとに書ける形にする（2026-11-13、ユーザー）** → 2026-11-14: `{"full_speed": {...}, "super_speed": {...}}` の形（RECORDS 4.3。キーはコアで口を選ぶときの名前）、case は相手の口で流れ、run_hardware がスケッチに `USB_TEST_CONTROLLER` / `USB_PEER_HOST_PORT` を渡す。bench_record.py `--usb-port`。6-2 の FS の Pico はまだつないでいない。元の記述:: 今の `usb` は相手を 1 つしか書けない。H417 の公式 EVT
  （6-2、oep://e8f60ae84dc2/h417qeu6-pico）は FS（PA11/PA12）に Pico、SS に PC の 2 つになる。V103 も FS が 2 系統あり（→ 誤り: device-data で V103 の USB は
  USBHD の 1 つ。2 つの Full Speed は V20x の USBFS と USBD）、どちらに
  つないだかを書けるとよい。案: `usb` を口（コントローラ）ごとの対応にする（例 `{"fs": {"to": "rp2040", ...}, "ss": {"to": "host"}}`）。
  case はどの口で流すかを、その口の相手で決める。FS の Pico はまだつないでいない（nano の FS につないでもよい、とユーザー）。
- [x] **6-2 の H417（公式 EVT）の selfcheck**: [oep] が PB8 / PA14 を素の pad にラベルし直し、`vio18=3v3` で 9 件通った
  （2026-11-13）。試験は石のデバッグの端子（pinout の SDI）を線として使わない（run_hardware の debug_pads）。
- [x] **H417 の SS の列挙が約半分で乱れる（'error -110' / 'unable to get BOS descriptor'）**: SWD のせいではなかった
  （モニターなしでも 4 回中 2 回）。USBSS のドライバが、制御 IN の status の段でホストの status OUT の受け口を消していた。
  直して 6 回中 0 回（2026-11-14）。
- [x] **Debug Speed のメニュー**: 足した理由（SWD が SS の列挙を乱す）は実測で否定されたが、アップロードとモニターの
  SWD の速さを変える一般の機能として残す（ユーザー、2026-11-14）。
- [x] **H417 の USBSS で、ドライバの読み込みの要求に答えない（usbhid -110）**: スタックが、新しい SETUP と前の要求の
  完了を同じ回に受けると、前の status を新しい要求のデータの段として出していた（usbmon で並びを確かめた）。前の要求の完了を
  先に片付け、新しい SETUP が来ていたら EP0 に何も出さないようにした。6-2 の usb と usb-classes が通る（2026-11-14）。
- [ ] **H417 6-2 の usb-media（SS のオーディオ）**: 等時のパケットが 125 µs ごとなのに FIFO が 4 パケット（0.5 ms）だった
  のを 4 ms にし、試験もパケットの数を速さに合わせた。「スピーカーの再生のあと固まる」は試験の正規表現の誤りだった
  （AUDIO の 5 つの数に合っていなかった。OEP 越しの core 1 の dbg regs で、loop が回っていることを確かめた）。今は 3 回中
  2 回通る。残り: マイクのランプが 200 ms に 0〜5 か所、4 サンプル（1 パケット）だけ戻ってまた進む形で途切れる。同じパケットを
  2 回送った形。USBSS の等時の IN は task から 1 パケットずつ準備するので、125 µs に間に合わない瞬間に前のバッファーが送り
  直されている疑い。直すなら、オーディオが chain（最大 16 パケット）で先の数パケットを並べて準備する形にする。
  → 2026-11-14 の続き: 原因は 2 つだった。(1) USBSS は等時 IN の完了を DMA がデータを読む前に知らせることがあり、1 面の
  送信バッファーを次のサンプルで上書きしていた（重複の形）。オーディオの送信を 2 面の交互にして消えた。(2) 残りは「ホストが
  Transaction Error（-EPROTO）と報告する 0 バイト＋1 パケット分の抜け」で、間のハブの GET_PORT_ERR_COUNT と完全に相関する
  リンクのビットエラーだった（起動ごとのリンクの学習で、悪い回は毎秒数十）。デエンファシスを EVT の 0x300 から 3.5 dB に
  し、等時の IN で ERDY_AUTO と EOB_MODE を外して（RM・EVT どおり）、悪い学習は 30 回中 0、抜けは 1 回あたり約 0.1 に
  なった。試験は、ホストがエラーと報告したパケットの所だけ抜けを許す（1 % まで、数は audio_capture_bad_packets）。
  残り: 準備が遅れた瞬間の競合（loop に 150 µs の待ちを入れると増える）は、2 段の予約（Core の transfer と全 DCD）が要る。
  begin() は成功するのに lastError が DescriptorTooLarge のまま残る件は直した（9f24011b。インターフェースを開く走査の打ち切りを
  Video が失敗と読んでいた。6-2 で usb-media のあと None）。
- [x] **H417 の USBSS で、書き込みやリセットのあとホストが新しいデバイスを 10〜30 秒見ない**: Linux は待機中のデバイスを
  U3 にサスペンドする。このコントローラーのリンクはデバッガーの停止とシステムリセットを終端を付けたまま越え、新しいファームが
  ホストの U3 中の終端検出（100 ms ごと）より早く張り直すので、ホストは入れ替わりに気づかず、タイムアウトとハブのリセットの
  あとで列挙していた（12 秒休ませてのリセットで 8 回に 1 回）。接続のはじめに 250 ms、終端を外した Disabled に置くようにした。
  10 回とも約 1.2 秒で付き直す（2026-11-14）。
- [x] **CH549 一体型の WCH-Link（3-1・4-3）の UART の橋渡しが、DUT のバイトをいつも 79 バイト遅れで USB へ出していた**
  （2026-11-14）。uart が 2 台とも TIMEOUT（前の回のバイトが来て、今回の返りは来ない）。11-13 のコードでも同じだったので Link の
  状態。baud を替えると（SET_LINE_CODING）作り直されて揃う。試験は 9600 で開いてから 115200 で開く（89c79b9a）。
  [pytest-embedded-arduino-cli-ch32rv] が ch32_uart.open() の中に同じ回避を入れた（03d5389、開いたまま速さを替える形。
  ずれた状態に再び出会ったら、それで直るかを確かめてから試験側の回避を外す）。
- [x] **5-1 の spi のスレーブが WireSkein で NG（2026-11-14）**: ファームではなく、V203 のスレーブの MISO のホールドが数 ns で、
  40 MS/s の取り込みで 0 サンプルになった区間が「どのモードでも読めない」になっていた（[oep] の解析）。V103 と同じく、エッジと同じ
  サンプルの変化をエッジの後とみなす（edge_sample before）を V20x にも広げた。2 回とも PASS。
- [ ] **6-2（H417）の SPI の線でビットが化ける（11-13 から）**: 原因の 1 つは直した。CS が SCK の隣の線で、SCK の縁を拾って
  Low の途中に短い High が入っていた（softspi で 40 ビットが 16 + 23 に割れる）。CS をプローブの上で SCK からいちばん遠い空き線に
  して直った（8004aa8c）。残り: spi の契約で、9 区間のうち 1〜3 区間で 3〜4 バイト目からの MISO が線の上で化ける（DUT は線の
  とおりに読んでいる、WireSkein）。モード・ビット順によらず、1 ワード目の後から崩れて MOSI のバイトが混じる形が多い。プローブの
  spi-target の既知の問題（[oep]、2026-11-14）: DMA を使わない形で、送りと受けが同じバッファ W0〜W15 を使う競合が一番の候補。
  DMA の形は configure で panic して、まだ試せていない。arm のあとの待ちは効かない（[oep] の数え直しで 0 ms と 50 ms が同じ程度）
  ので外した。SCK のグリッチフィルタも効果なし。
  → 2026-11-14 夕: 原因は CS の 12.5 ns 以下のひげ（[oep]）。spi-target の CS のフィルタ（25 ns）は oep-probe の main に入った
  （次のリリースから）。その版で DUT の読みは 2 回とも正しく、残りは取り込みの解析だけ。oep-client-python に取り込みの
  glitch_filter_ns が入った（f667aa5、未リリース）ので、リリースされたら ws_capture.stream に 50 ns を渡し、6-2 と他の台で確かめる。
- [x] **入れ替えで加わった口（2026-11-14）**: V208・V003・V407・V006・V103 の EVT を足して全台に加えた。直したもの: V003 の
  periph（Serial1 の無い系列で uart2 が何もしなかった。試験のスケッチ）、V103 の EVT の selfcheck の PWM（Serial.end() のあと
  USART が PA9 に Low を出し続け TIM1 CH2 を押さえていた。end() で USART のクロックも止める）、selfsys（リセット直後の
  STARTUP の行の頭をコンソールが取りこぼす。`why` で問い合わせる）。V006 の signals の PC0 は 1 回だけで、2 回とも PASS。
- [ ] **X035 の USB ホストで、Pico の HID の 2 つ目のレポートを 2 秒読みに来ないことがある**（usb-peer-host）。ホストは押下を受け
  取っている（`H CONSUMER 233 p`）のに、相手の送り終わり（ACK）が 2 秒来ない形。相手の待ちを 2 秒にした後で 1 回（11-14 04:48）、
  そのあと 18 回続けて PASS（14 回のくり返しを含む、2026-11-14）。HCD のトグル違い・NAK・無応答・ポーリングの止まりを数える計測
  （割り込み IN の結果の種類ごとのカウンタと `hcddbg`）で 14 回回したが出なかった。次に出たら同じ計測で、止まった 2 秒に何が
  増えるか（トグル違い: ホストの ACK が届いていない、NAK: 相手側、増えない: ホストがポーリングしていない）を見る。
- [x] **nanoCH32H417 の SD（2026-11-14）**: `vio18=3v3` のスケッチは今は止まらない（2026-11-13 に見た止まりは、
  H41x の HSE とスタックの修正のあとは出ていない）。そのうえで 3 つ直して、sd の case が 2 回続けて通る（SDIO 4 線 20 MHz、
  SoftSPI）。
  - FatFs が、`mkfs.fat -F 32` で作った小さい FAT32（480 MB、32 KB のクラスタ、15330 個）を、クラスタの数で FAT16 と数えて
    断っていた（FR_NO_FILESYSTEM）。BPB が FAT32（FATSz16 が 0、ルートディレクトリの欄が 0）なら数によらず FAT32 にした
    （Linux・Windows と同じ。ff.c の手元の変更）。
  - カードは、答えたバスのモードを電源が切れるまで続ける。SPI の後は SDIO に答えず、SDIO の後は CS Low の CMD0 にも
    答えない（両方を実機で確認）。カードの電源は基板のものなので、WCH-Link の後ろの SDIO スロットでは、試験ごとに Link の
    USB の口を切り入れする（test_sd_contract の fresh_card）。
  - SDIO のドライバ（SD_MMC）は直す所がなかった。実機で動かしたのはこれが初めて。

CH32RVUsbDevice / CH32RVUsbHost は自作のスタック（[ADR-0012](docs/adr/0012-usb-stack.ja.md)）。全系列のドライバはあり、実機で確かめたのは X035、H417、V407（HS）。

- [x] **USBHS で、リセットの後の速さを読む時期**（PR #1、ladyada。PR は取り込まず、実験してから独自に直した。2026-11-11）。
  - 測った結果（V407、第2世代 USBHS、HS のホストは ESP32-P4）: BUS_RST の割り込みでは `MIS_ST` の HS ビットが 0（full speed）。約 30 ms 後の LINK_RDY で 1 になり、最初の SETUP のときは HS。2 回目のリセットも同じ順。
  - 直し方: コアは、リセットで仮に読み、リセット後の最初の SETUP で 1 回だけ読み直す（`Core::take_speed()`）。V407 で、HS のバルクが 64 → 512 になり、usb-peer が HS で通る。FS（X035 の S3 と Pico の組）の usb-peer も通る。
  - 残り: 第1世代（V30x の USBHS1）と H417 の USBSS は、HS / SS の相手がまだ無いので未確認。
- [x] **V407 の HS で落ちていたデバイスの case**（ESP32-P4 の相手、2026-11-11）。6 件とも HS で通る。原因は次の 3 つと、試験の側の FS の前提。
  - 相手の P4 の Serial（USB-Serial/JTAG）の受信は既定で 256 バイト。長いコマンドの行の残りと改行が落ちていた（input の 128 バイト）。`setRxBufferSize(1024)` にした。
  - V407 の USBHS の PLL が HSI から作られていた（CPU のクロックが HSI のままのとき HSE は止まっている）。列挙と短いパケットは通るが、512 バイトの IN はホストで通信エラーになっていた（MSC、NCM、FAT）。ドライバが HSE を起こすようにした。HSE が無い基板では HSI のまま。
  - isochronous の IN は、転送が終わっても最後のパケットを TX_LEN のまま返し続けていた（映像の最後のペイロードが毎マイクロフレーム届く）。転送が無いときは TX_LEN を 0 にして、長さ 0 のパケットを返す。
  - 試験の側: CCID の自動の大きさは HS で 512（ライブラリの決めたとおり）。Borrowed の受け皿は HS のパケットの倍数が要る（スケッチの 64 → `CH32RV_USB_MAX_PACKET`）。DFU_DETACH で工場のブートローダーが出るのは X035 だけ。
- [ ] 新しい系列の実機確認（ベンチの DUT の USB 端子を配線してから）。HSI から作る 48 MHz の精度、V103 の EP4〜7 の
  自動トグル、HS の PHY と HSE。第2世代 USBHS（V467、X305、V205）と X315 の USBSS はビルドだけ（V407 は HS で確認済み。X315 の USBHS は 2026-11-20 に Pico と usb-peer / usb-peer-host が通った。HS ではなく Pico の FS）。
- H417:
  - [x] USBSS のデバイスの実機の続き（列挙と CDC、DFU までは確認済み）。→ 2026-11-20、h417qeu6-pico の SS の口（PC）で
    usb・usb-classes・usb-media の case と、usb-ss-iso（等時の Mult、ベンダーのバルクの halt・リセット・大きさ・再起動後の列挙）が
    通る（等時の Mult>0 の部分の間隔のばらつきは上の usb-ss-iso の項目）。
  - [x] SS の列挙が、リセットの後に 4〜5 回に 1 回ほど失敗する（2026-11-11、HS の線を切った後のベンチ。SWD の書き込み
    3 回とリセット 5 回のうち 2 回）。カーネルは `device descriptor read/all, error -71` や
    `unable to get BOS descriptor set` / `unable to read config index 0 descriptor/start: -71` を出し、UsbFallback が FS に
    移る。`s` で SS に戻すと列挙する。SS で列挙している最中の SWD の書き込みは 4 回とも通った。
    → 2026-11-20、h417qeu6-pico で再現しない: usb-ss-iso::test_usb_ss_enumeration_after_restart（CH32RV.restart() を 10 回）で
    10 回とも SS で列挙してバルクの往復が通り、この日の usb-ss-iso の約 20 回の実行（毎回 SWD で書いてリセット）も全部 SS
    だった。また出たら、その試験の speeds で拾える。
  - [ ] USBHS のドライバ。PB8 / PB9 が SWD と兼用なので、デバッガを外した状態での確認が要る。
  - [ ] UART ISP（PB6/PB7）: ソフトのリセットでは入らなかった（docs/h417-bench.ja.md）。電源の入れ直しは未確認。入れるかどうかは別に決める。
  - OEP の probe にする件は、コアの側は入れた（84b57b0）。oep-probe-arduino への依頼は 2026-10-21 に dev-oep のセッションへ送った（向こうの状況は未確認）。
  - [ ] UHSIF（docs/h417-uhsif.ja.md）: CH32RVUhsif と CH32RVLogicCapture の `begin(UHSIF_DATA, rate)`。SRAM へは
    200 MS/s まで抜け無し。PC へ USB SS で流すと 133 MS/s まで抜け無し、200 MS/s は約 1 割落ちる（USB の処理が
    100 MHz の V3F、2026-11-11）。残り: H417 の V5F への移行のあとに測り直す、CH32RVLogicCapture に PC へ流すための
    深いリング、速い信号での確かさ、外部クロック・トリガー・出力・8 / 32 ビット幅、自動の試験。
  - [ ] WeAct LogicAnalyzerV2（CH32H417、https://github.com/WeActStudio/WeActStudio.LogicAnalyzerV2）で、このコアの
    スケッチを動かす。入手予定（2026-11-11 時点）。どちらの形にするかは未定。
    - 入力の D0〜D15 は UHSIF の PORT16〜31（RM の表 9-33）。GPIO の DMA ではなく UHSIF で取り込んでいる。WeAct は WCH の
      `libUHSIF.a` を使うが、このコアでは自前のレジスタ設定で取り込めた（上の UHSIF の項）。
    - **案 A: ユーザー領域（slot 2）に乗せる。** WeAct の IAP（0x0〜0x6000）が、電源を入れるたびに slot を選んで飛ぶ。
      ロジアナ（slot 0）と DAP（slot 1）は残る。要ること:
      - リンク先を 0x09E000（V3F は 104 KB まで。V5F の分を使わないなら 304 KB）にするボードの定義。
      - CH32RV_FLASH_STORE を slot の中に置く。今は FLASH の末尾で、WeAct の設定の領域（0x0EA000〜）と予約の領域を壊す。
      - VIO18 の内部の LDO を触らない（外の電源で 1.0〜3.3 V を作っている。例のアプリは VSEL_VIO18_MODE5）。
      - PC6 は SWPMI の TBYP を立てる。
      - 抜け道をコアの側で持つ: KEY（PE1）の 2 秒長押しで 0x2017FFF0 に `0x736C7400 | slot` を書いてリセット、
        CDC のボーレート 2048 で `0x69617021`（IAP に入る）。RAM の末尾 16 バイトは空ける。
      - 書き込みは WeAct の CLI ツール（`-f <bin> -b 0809E000`）。IAP の手順は非公開。H417 の SWD は USB 2.0 の線
        （PB8 / PB9）と兼用で、アプリも SWD を切るので、実際には使えない。
      - IAP の領域、特に 0x5FF4 の UID の写しには触らない（不一致で IAP が自分を消す）。
    - **案 B: チップ全体を乗っ取る。** フラッシュも RAM も全部使え、このコアの普通の書き込み経路（DFU、USB）で回せる。
      要ること:
      - SWD で書く。PB8 / PB9 が USB 2.0 の線なので、評価ボードと同じく線を切るなどの手が要る。
      - 元に戻せるように、最初に SWD でフラッシュ全体（960 KB）を読んで保存しておく。WeAct のリポジトリにあるのは
        各 APP の bin だけで、IAP の bin は無い。同じチップへ書き戻すなら UID の写しも一致する。
    - 判断の材料: PC 側アプリ（U3LogicAnalyzer）の調査が wireskein の docs/instruments/weact-u3-logic-analyzer.ja.md
      にある（2026-11-11）。sigrok の改変版（GPL-3.0）、USB は 1a86:5537 のバルクで、生のサンプルが流れる。保存は普通の
      .sr で、WireSkein で読める。ファームの更新は HID（1a86:fe17）。
- **USBSS の host（H417）を実装する**（2026-10-28 にユーザーの依頼。後回し）。常設ベンチ（tests/BENCH.ja.md 7 章）で、SS の試験を
  H417 どうしで組むために要る（DUT が device のとき相手の H417 が host、DUT が host のとき相手の H417 が device）。
  - 材料: EVT `USBSS/HOST/Host_SS_Access_Sector`（ほかにハブ経由の 2 例）。`Common/ch32h417_usbss_host.c` がリンクの初期化と
    LMP の割り込み（PORT_CAP → PORT_CFG → PORT_CFG_RES で接続、RX_DET で切断、warm reset で初期化し直し）、
    `ch32h417_enum.c` の `U30HostCtrlTransfer` が SETUP / データ / STATUS の各段（`UH_TX_CTRL` / `UH_RX_CTRL`、
    シーケンス番号と burst の数、NRDY のあと ERDY を待って送り直す、STATUS の前に 1 ms の待ち）。ソースで読める
    （`libRV3UFI.a` は U ディスクのファイルシステムで、使わない）。
  - 要るもの: (1) 新しい HCD（`CH32RVUsbHcd` の 2 つ目。今は USBFS だけ）。リンクの割り込み、転送、シーケンス番号、
    ERDY / NRDY。(2) host の列挙の SS 対応（EP0 の 512 バイト、BOS、SET_SEL、SET_ISOCH_DELAY、エンドポイントの
    companion 記述子と burst、速さの値 SuperSpeed）。(3) USBHS の host（SS の口の USB 2.0 側。今は無い）は別の項目にする。
  - 確かめ方（2026-10-28 に決定）: 作る間は市販の USB 3.0 のメモリで手作業の試験（host の MSC クラスで読む。EVT と同じ形）。
    安定したら、H417 どうしのピア試験にする（H417 は 2 台を追加で確保中）。H417 の HS の線を切り離す部品も確保中で、
    切り離せば SWD で書き換えられる（SS の口を挿したままでも DFU に頼らない）。
- [ ] ファイルシステム（docs/filesystem.ja.md）の残り: FAT を NOR に載せる摩耗分散の層、
  littlefs の設定（block_cycles、媒体の大きさ）を変えたときの値。
- [ ] IP（docs/network-stack.ja.md）の残り: V317 の実機。V203RB は WCH-Link 一体型のボード（wchlink://BC5F4B9807F7）で eth が通った（2026-11-13、tests/.env の記録、今の `OEP_PORT_MEMO_<serial>`）。V208 の USB(ベンチの暫定配線では PC につながっていない)。
  V307 のチップのリビジョン 0x20 の回避策。対象の系列の USB デバイスのドライバで NCM を実機で確かめる。HTTP サーバー、MQTT / HTTP のクライアント（FEATURE_CANDIDATES の NET-03）。
- [ ] スタックの予約（Stack Size メニュー、既定は RAM の 16 分の 1、2026-11-14）より深く使うコードは、ヒープを黙って上書きする（Stack Check メニューを On にすると、はみ出しでリセットし、次の起動の resetReason() が panic になる）。TLS は、セッションの間 `_sbrk` に sp から 6 KB の余白を求めさせて直した（2026-11-03。V307 の 224k_96k の MAC エラーは、きれいなメモリー不足に変わった。H417 の HTTPS はそのまま通る）。ほかに深いスタックを使うライブラリが出たら、同じ `ch32rv_heap_stack_margin` を使う。
  V307 の eth の試験の HTTPS は、224k_96k でも 192k_128k でもメモリーが足りない（試験のスケッチが大きい）。HttpsGet・MutualTlsClient は
  192k_128k で通る（2026-11-20 に lwIP の送信用のヒープを 20 KB にしてから）。試験のスケッチを収める工夫（`MBEDTLS_SSL_IN_CONTENT_LEN` の縮小と
  max_fragment_length）は、必要になったら。

- [ ] EspUsbDevice との差の残り（Info の例 3 本は 2026-11-20 に済み: UsbBringUpCheck・UsbConsole を移植、DescriptorDump は既にあった。MidiEcho・CompositeCdcMidi・ReconfigureVendor・MscCdcBlocks・FirmwareCDC も移植。FirmwareHTTP（ESP の WebServer と
  HTTPUpdateServer に頼る）の代わりに、HTTP から取ってくる Update の HttpUpdate を足した（V307 で確かめた）。AudioPlayback（形を複数）はこちらが 1 つの形なので AudioSpeaker で足りる。CdcConsole と VendorEcho は
  SerialUSBEcho と VendorBulkPeer が同じことを示す。M5 と P4 の例は板に固有）:ホストの Audio / Video / Network、複数リスナー、キュー、64 ビット LBA、CCID の追加 API。
  → 2026-11-20: 例を数え直した。EspUsbDevice の 45 本のうち、こちらに無いのは M5 の 3 本・P4 の 2 本（板に固有）、AudioPlayback・CdcConsole・
  VendorEcho・FirmwareHTTP（上の理由）、UsbNetwork（Network の UsbNcmNetwork）だけになった（FirmwareVendor・FirmwareBootMode を足した）。
  複数リスナーとキューは、下の「イベントの payload の持ち方」（ユーザーの判断待ち）と同じ設計の問題。64 ビット LBA は 2 TB を超える媒体が要る。
- DFU_DETACH の応答がホスト側で STALL になる原因を調べる。見込み（2026-10-21）: ブートローダーへ移る前の 20 ms の
  待ちが、印に `millis() | 1` を使っていたため 0 ms になることがあり、ステータス段の応答の前に抜けていた
  （85f96bb で直した）。H417 では、その後の DETACH → DFU を 10 回ほど続けて通した。PC ホストの X035 で確かめれば閉じられる。
- ホストで未対応: アイソクロナス（オーディオ・ビデオ）、USB ネットワーク（NCM / ECM）、MSC のファイルシステム、
  ハブ越しの低速デバイス（PRE PID の実機確認）、split の CLEAR_TT_BUFFER、第 1 世代 USBHS の split。V103（旧 USBFS）と
  第 1 世代 USBHS（V30x）のホスト、HS のハブの先の FS・LS（split）は 2026-11-17 に実装（ビルドのみ）。第 2 世代 USBHS の
  ホストは V407 で HS を確認済み（2026-11-11）。
- [ ] usb-peer-host-input / -storage を ESP32-P4（HS）でも流す。EspUsbDevice 3.1.0 で GET_REPORT（`setFeatureReport()`）、
  任意のレポート記述子（`EspUsbDeviceHidCustom`）、CDC 2 ポートは入った。boot キーボードの専用インターフェース
  （`keyboard.enableOwnInterface()`）は EspUsbDevice 3.2.0 で入った（2026-11-11 時点でリリース済み）。
  次は input の P4 用の相手を書く。MSC の複数 LUN は向こうで保留になったので、storage は Pico だけのまま。ESP32-S3 側の癖は tests/peer/usb/README.ja.md。
- UIAPduino のソフトウェア USB（rv003usb）をアプリモードでも動かす件は FEATURE_CANDIDATES の USB-09。

## USB PD ソース（CH32RVUsbPd）の実機確認

（2026-10-28）PD の試験は基本は手作業。常設ベンチに置くなら X035 どうしで、片方の CC の抵抗を外してソースにし、シンクとソースのピア試験にする（tests/BENCH.ja.md 7 章）。

ソース（`USBPDSource`、`CH32RVUsbPdSource`）は実装済み（ビルドとフレームの単体試験まで）。残りは実機での確認。
- X035 どうしの対向（シンク `USBPD` ⇔ ソース `USBPDSource`）。ソースにする WeAct の板は CC の 5.1 kΩ（R4 / R5）を
  外す（回路図では SB2 / SB3 で PC14 / PC15 につながる Rd。実物で確認）。試験のスケッチは両方のコールバックを
  登録して明示的に true を返す。
- 確かめること: Rd の検出と向き（表裏）、固定と PPS の契約、Get_Source_Cap、Soft_Reset / Hard_Reset、PPS の途絶
  （15 秒）、Reject（名乗っていない電流）、取り外し、コールバックが false を返したとき（VBUS: 接続しない、電圧:
  Hard Reset で 5 V に戻る）。
- 実際の電圧が出ているかは範囲外（基板側の仕事）。

## 系列で限られているもの

[docs/peripheral-support.ja.md](docs/peripheral-support.ja.md) の ○ / ◐ の中身と、[docs/peripheral-plan.ja.md](docs/peripheral-plan.ja.md) の B（後で）。

- [x] WFI の間に DMA が進むか（ch32-hal の README）。2026-11-20 に測った: V003・V203・X035 は止まり、V006・L103 は進む。コアは
  WFI で DMA を待たず、`lightSleep()` は HCLK ごと止めるので影響なし。結果は [docs/vs-ch32-hal.ja.md](docs/vs-ch32-hal.ja.md)。

- [ ] X305 / X315 の Wire（プルアップの配線が要る）と、既定以外の route。V205 は常駐ベンチの v205rct6-pico（PB9 / PB8）で wire の 3 件が通る（2026-11-14）。V205 は LinkE の接続で s0 が壊れるので、結果はリセットごとに 1 回だけ読む。
- [ ] V20x / V30x の ADC の校正値（EVT の Get_CalibrationValue）。V307 ではその値が 1728 で、当てると VDD が 2.64 V になり不自然（当てない今は 3.19 V）。外部の電圧計と照合して決める。
- [x] CPU Clock の全部の項目を、ベンチの DUT で selfcheck（時計・Serial・SPI・GPIO・PWM・ADC）で確かめた（2026-11-13）。
  V006 の 8 MHz は、SystemInit() が flash の待ちを先に 0 へ下げてから HPRE /3 を書いていたので、リセットの直後の HSI 24 MHz を
  待ち 0 で走り、SysTick が止まったり起動しなかったりした（待ちは上げるときだけ先に、下げるのは分周のあと。CFGR0 も 1 回で書く）。
  X035 の 8 MHz は ADC のクロックが下限（3 MHz）に届かず変換が終わらない。項目を「no ADC」にし、ADC の呼び出しをビルドエラーにした。
- [ ] クロック: H41x と X305 の CPU Clock のメニュー、HSE、USB を各項目で確かめること。→ 2026-11-17: H41x に CPU Clock（V5F / V3F が 400/100・200/50・25/25 MHz。F_CPU は CPU Core の選ぶコアのもの）、X305 に X315 と同じ 240 / 20 MHz、M030・X3x5・V4x7 に Clock Source（HSE）を入れた（ビルドのみ）。実機で selfcheck（time_rate・clocks・clock_source）とselfsys を各項目で流すこと。→ 2026-11-14: H41x（CPU Clock のメニューは無く、CPU Core × Clock Source）は nano で V5F / V3F × HSE / HSI の 4 つとも selfcheck・selfsys が PASS。USB は nano に相手が無いので未確認。→ 2026-11-20: h417qeu6-pico の USB SS（PC）で、V5F / V3F × HSE / HSI の 4 つとも usb-ss-iso のベンダーの 3 件（halt・
  ポートのリセット・大きさ）と等時の alt 1〜3 が通る。48 パケットの alt 4 / 5 は V3F（100 MHz）で間隔が空になる・短くなるものが出る。
  （H41x の CPU Clock のメニューは今は 400 MHz の 1 つだけ。）X305 はベンチに無い。L103 の 72 / 96 MHz は、OEP probe（RP2350）が動いている L103 に接続できない（試すイメージには `CH32RV_BOOT_HOLD_MS` を付ける、docs/upload-routes.ja.md）。
- PD の他系列（L103 / M103、V205、X305 / X315、H41x、M030 の USBPD0）: 2026-10-22 に入れた（ビルドのみ）。BMC の
  クロックは F_CPU から計算する（EVT の表は 10〜240 MHz。8 MHz の L103 / V205 / M030 で通るかは実機で確かめる。
  試験は `--case pd`）。M030 の USBPD1 は未対応。
- PWM の remap 先と CHxN。TIM8〜10 の PWM は 2026-10-22 に入れた（V30x・V4x7・H41x、TIM1〜5 の端子は奪わない、ビルドのみ。`--case logic` の `logic_pin` でその端子を指定して確かめられる）。
- [x] GPIO 以外の EXTI（peripheral-plan の B）: `CH32RV.attachExtiLine()` など（2026-11-17）。WWDG は `CH32RV.wwdgEnable()` で入り、2026-11-19 に V203・H417・X035 で system::test_window_watchdog が通った。
- [ ] SPI のスレーブ（CH32RVSPISlave）の残り（X035 で、プローブの spi-controller が 100 kHz のマスターになる spi.slave が通る。2026-11-05）: ハードウェアのマスターでの上限のクロック、V407（PA7 が RMII と重なる可能性）・H417 の実機、SPI マスターの LSBFIRST が V003 で正しいか（X035 は 2026-11-03 に case spi で、モード 0〜3 と LSBFIRST の transfer16() まで線で確かめた）。
- [x] ADC の連続取得（CH32RVAdcStream）を一番遅い段より遅い一定のレートで（タイマーのトリガ。系列ごとの EXTSEL の対応が device-data に無い）。
  → 済み: `beginTimer()`（2026-11-17）。トリガの表は device-data の adc_triggers.csv から生成。periph::test_adc_timer_trigger が
  V003・V006・V103・V203・V205・L103・X035・H417 で通る（2026-11-19 の集計）。
- CH32RVAdcStream の複数の端子が 1 つずれていた件（2026-11-05〜07、X035・V003）: begin() の measure() が、数えた DMA を使い切った
  あとも ADC に連続変換をさせて止めていたので、DMA が受け取らなかった要求が 1 つ残っていた。この要求は ADC の電源を落としても、
  RDATAR を読んでも消えず、次にチャネルを有効にした瞬間（ADC を走らせる前）に古い値を 1 個運んでいた（開始の直前に DMA の残り数が
  128 → 127）。adc_halt() で、ADC の DMA ビットを立てたまま捨てる先へ 1 個受け取らせて消すようにした。プローブ無しの試し
  （scratchpad の adcseq3.ino / adcseq2.ino）と periph（x035・v003evt・v006evt）で、2 本の並びが毎回正しい。periph.adc-stream は、
  2 本目の範囲を 1/4・3/4 に、デューティを最初と最後の立ち上がりの間（周期の整数倍）で数えるようにした（V003 の短いバッファでは端の
  半端な周期でデューティが 0.79 に偏っていた）。
- ADC の値の形（2026-11-06）: `analogRead()` は全系列で変換器の値そのまま（arduino-esp32 と同じ）。L103 / M103 の EVT の
  VDDA/2 オフセット補正は `analogReadCalibrated()` だけがかける（補正なしでは約 620 カウント低い、c6657f40）。AdcStream と
  `analogRead()` は同じ値になる。mV を返す関数は作らない（基準は VDD で、コアは VDD を知らない。Vrefint は ±2.5〜7 %）。
  精度は外部の電圧源でのマニュアル試験に回し、periph.adc は 1/4・3/4 で分かれるかだけを見る（v003evt の PC4 は High で
  908 前後に読めたが、プローブの出力は基準にならない）。L103 の実機で両方の値はまだ見ていない。
- CH32V103（v103evt、OEP）の書き換え直後の止まり（2026-11-06）: 別のスケッチから書き換えたあとの最初の起動が、
  SysTick_Handler の mret で不正な命令の例外になっていた（mepc の命令は新しいイメージの mret そのもの）。プローブのリセットが
  ndmreset で、前のスケッチの周辺（SysTick、USART、RCC）が戻らないまま新しいスケッチが起動していた。OEP の reset op を
  システムリセットにして直った（oep-spec f3709f8、プローブ 0.0.51+c7556e3）。その版で v103evt の一通りは、spi.slave と wire の
  ほかは PASS。
- CH32V103 の spi.slave と wire（2026-11-07、v103evt）: どちらもコアではなかった。spi.slave は、プローブの spi-controller が
  エッジの約 300 ns あとで MISO を読んでいたのと、試験の取り込み（10 MS/s）では V103 のホールド時間 25 ns が 0 に見えて WireSkein が
  「どのモードでも読めない」としていたのの 2 つ。spi-controller はエッジちょうどで読むように直り（[oep]）、slave の取り込みを
  40 MS/s にした（V103・X035 で PASS）。wire の n 3 stop 1 は、プローブの i2c-target が書き込みのあとの Sr を読みと取り違えていた
  （[oep] が向きのビットで直した）。
- CH32V103 の spi.slave（2026-11-07）: V103 のスレーブの MISO は、バイトの境目で読み取りのエッジから 0〜25 ns で変わる（データシートの
  th(SO) 15 ns 以上、tV(SO) 25 ns 以下）。40 MS/s の取り込みではエッジと同じサンプルに入る回があり、WireSkein が「どのモードでも
  読めない」としていた。80 MS/s は、コンソールと同時だとプローブの経路の帯域が足りず使えなかった。wireskein 0.0.25 の
  `spi(..., edge_sample="before")` を V103 にだけ付けて、v103evt で 2 回とも PASS（記録には hold_under_sample が残る）。
  プローブの直し（流し続けの DMA の輪を内部 RAM へ、0.0.51+903f88c）のあとも、broker 越しでコンソールと同時の上限は 35 MB/s
  （docs/guide/capture、4ca4a62）。4 本 80 MS/s（40 MB/s）には足りず、その下で間隔が揃う 53.3 MS/s（18.75 ns）は V103 の th(SO)
  15 ns より粗いので、edge_sample は外せない。40 MS/s のままにする。
- 取り込みの帯域（2026-11-07、利用者の提案。今の試験には要らず、速い SPI の試験を作るときに使う）: 選べる速さ、本数ごとの幅、
  取り方ごとの上限、線ごとの解像度（resolution=）、流し続け中の reset-halt の注意は、oep-probe-arduino の docs/guide/capture.ja.md
  「ESP32-P4 / P4X の速さと本数」が正（034eaf9）。ここには写さない。WireSkein は線ごとの間隔と変化点の線を扱え、粗い CS では
  spi() が窓の始まりを 1 サンプル早めて数える（c9ea14e、次の版）。速い SPI の試験の取り方は、その節を見て、その時に測って決める。
- 既定の端子が USB の D- / D+ を兼ねる件（2026-11-07）: 型番を選ぶと 110 か所あった（X035 の小さいパッケージの I2C1 が PC16 / PC17、
  V205 の Serial1・V407 / V467 の Wire が PB6 / PB7、X305 / X315 の Serial2・Wire1 が PD6 / PD7 など）。後の 3 つはデータシートの標準の
  端子で、一律に既定から外すと標準の Wire / Serial が消える。USB を使うかは variant を生成する時点ではわからず（ボードメニューも
  無い）、使うときだけ外す案も成り立たない。既定はそのままにして、variant の見出しに「USB の D- / D+ も兼ねる、USB を使うときは
  別の端子を名指しする」と書くことにした（generate.py の usb_note、12 系列）。
- 型番ごとの既定の経路（2026-11-06）: 系列の既定の経路がその型番に無いとき（端子が出ていない、またはその型番の NRST・SWD・BOOT に当たる）、
  型番を選んだ組み立てでは、その型番が出している経路から同じ規則で既定を選び直す（なければ既定なし。begin() に端子を渡す）。
  ANY は系列の既定のまま。前は型番の ABSENT（経路が 1 本も無い）しか見ていなかったので、V006E8R6 / E8R7・V005E6R6 の
  `SPI.begin()` は NRST の PC5 に SCK を出し、X035F8U6 などの `Wire.begin()` は出ていない PA10 / PA11 に出ていた。
  15 系列の 61 件（emit_misfit_defaults）。SS も同じ経路の NSS にそろえた。既定なしにする案（B）も試作したが、既定の経路が
  系列で決まらないインスタンスはすでに型番ごとに選び直しているので、それとそろう方（A）にした。
- CH32V003 の DUT が、試験の途中で「ソフトウェアのリセット」で再起動することがある（2026-11-06）。UIAPduino（30eda0ea068b
  の v003）で softspi を 7 回流して 2 回。millis() が 2134 → 65 ms、6390 → 5 ms と戻り、起動時の resetReasonName() は
  software。スケッチはリセットを呼んでいない。リセット後のスケッチは試験が設定した端子を忘れ、端子の無い SoftSPI で答えるので、
  「線に何も出ない、読みが 0」で落ちる（v003evt の最初の流しの softspi も同じ形）。どちらも ws_capture の取り込み中で、一方は
  プローブの spi-target を使う最中。DUT の外から（SWIO のデバッグの線、ch32rv の dmseq やブローカー、プローブ）リセットが入る
  道筋を [oep] と [ch32rv] に調べてもらっている。試験の側は済み（2026-11-06）: スケッチは起動で `STARTUP <理由>` を出し、
  run_hardware.py が落ちた case の dut.log から途中の STARTUP を拾って「the DUT restarted」とエラーの頭に付ける
  （tests/sketch_support/README.ja.md）。
  原因はプローブ（[oep]、2026-11-06）: dmseq のために DMCONTROL を読むとき、キャプチャと USB の DMA の負荷で P4 の CPU が
  0.3〜0.6 µs 止まり、rw のビット（長い low）が縮んで V003 は書き込みと読み、DMCONTROL に 0xFFFFFFFF（ndmreset）が入っていた。
  test_spi_contract と同じ組み合わせで 6 往復目に再現。oep-probe-arduino 695d4dc（SWIO の low を線が下がった時刻から数える）で
  144 往復リセットなし。全プローブを 0.0.51+695d4dc にしたあと、softspi は v003（UIAPduino）でも v003evt でも PASS（同日）。
- Wire の読みのあとの repeated START（2026-11-07）: `requestFrom(addr, n, false)` のあとの転送が、ときどき 5（タイムアウト）に
  なっていた（V003・V006・X035）。読みの終わりで CTLR1 に START を立てて予約したうえで、次の start() がもう一度
  `CTLR1 |= START` を書いていた。周辺が予約の START を出して START を下ろすのと、この読んで書き戻す操作がぶつかると、2 回目の
  START を頼んだことになる。予約したとき（_restart_armed）は start() で立て直さず SB を待つだけにした。UIAPduino の V003 で、
  前は 3 回とも落ちたのが 3 回とも PASS、v003evt・v006evt も PASS。
- `analogReference()` はハードウェアに基準電圧の選択が無い。
