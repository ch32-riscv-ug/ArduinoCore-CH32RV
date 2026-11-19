# OEP の配線の望ましい形（ファミリーごと）

この文書は `tests/bench_wiring.py` が書きます（手で直さない）。試験がスキップする条件（「ラベルの付いた線が無い」
など）を、いまプローブに付いているラベルと、ch32-device-data の端子表（タイマー・USART・SPI・I2C・ADC・DAC の
役割と経路）、variant の既定の端子、型番の触らない端子（水晶・USB・デバッグなど）から突き合わせています。

- 線はすべて `<口>@<端子>`（役割の要らない普通の線）で足せます。役割のある線（SD、CAN など）は
  [BENCH.ja.md](BENCH.ja.md) §5 の表です。配線の規則（RVSWD・UART・NRST は + 近く、GND 固定の BOOT は触らない）も
  そちらに従います。
- 「足す端子」は条件ごとの最小の候補で、ほかの条件の候補と重なるものは 1 本で両方を満たします。台ごとに
  重ねずにまとめた一覧が先頭の「足す線の一覧」です。
- デバッグ（2 線の系列は SWDIO・SWCLK、V003 は SWIO）・リセット（NRST）・コンソールの線は、役割のラベル
  （`.swdio@<端子>` など）で付いていれば済とします（NRST は端子の書かれていない `.nrst` でも）。2 線の系列でも
  1 線（`.swio@`）で付いていれば済です（V006・V205・H417 は OEP から 2 線で接続できず 1 線、2026-11-19）。
- OPA・コンパレータ・CAN のループバックは DUT の自己試験なので線は要りません（端子が触らない端子でなければ動く）。
- 1 台の P4 で約 16 本が目安（BENCH.ja.md §5）。足りないときは、上の行（多くの試験が使う条件）から引きます。

## 足す線の一覧（端子順）

条件ごとの表の「足す端子」を台ごとに重ねずにまとめ、端子の順に並べたものです。ラベルの形で書いています
（役割の付く線はデバッグ・リセット・コンソールだけ。ほかは普通の線）。

「ラベルを直す線」は、線はもう引いてあって役割のラベル（`.swclk@` など）が付いていないものです。

| 台 | 足す線 | ラベルを直す線 |
|---|---|---|
| CH32V003F4U6（v003f4u6-uia） | 0 本: なし | なし |
| CH32V003F4P6（v003f4p6） | 0 本: なし | なし |
| CH32V006K8U6（v006k8u6） | 0 本: なし | なし |
| CH32V103R8T6（v103r8t6-evt） | 1 本: `v103r8t6-evt@pa8` | なし |
| CH32V203C8T6（v203c8t6） | 3 本: `v203c8t6@pa8`, `v203c8t6@pa15`, `v203c8t6@pb13` | なし |
| CH32V205RCT6（v205rct6-pico） | 6 本: `v205rct6-pico@pc3`, `v205rct6-pico@pc4`, `v205rct6-pico@pc5`, `v205rct6-pico@pc6`, `v205rct6-pico@pc7`, `v205rct6-pico@pc8` | なし |
| CH32V208WBU6（v208wbu6） | 13 本: `v208wbu6@pa0`, `v208wbu6@pa1`, `v208wbu6@pa2`, `v208wbu6@pa3`, `v208wbu6@pa5`, `v208wbu6@pa6`, `v208wbu6@pa7`, `v208wbu6@pa8`, `v208wbu6@pa15`, `v208wbu6@pb6`, `v208wbu6@pb7`, `v208wbu6@pb13`, `v208wbu6.nrst` | なし |
| CH32V307VCT6（口なし） | 26 本: `<口>@pa0`, `<口>@pa1`, `<口>@pa2`, `<口>@pa4`, `<口>@pa5`, `<口>@pa6`, `<口>@pa7`, `<口>@pa8`, `<口>.rx@pa9`, `<口>.tx@pa10`, `<口>.swdio@pa13`, `<口>.swclk@pa14`, `<口>@pa15`, `<口>@pb6`, `<口>@pb7`, `<口>@pc0`, `<口>@pc1`, `<口>@pc4`, `<口>@pd1`, `<口>@pd3`, `<口>@pd5`, `<口>@pe2`, `<口>@pe3`, `<口>@pe4`, `<口>@pe5`, `<口>.nrst` | なし |
| CH32V407WEU6（v407weu6-yd-p4） | 20 本: `v407weu6-yd-p4@pa0`, `v407weu6-yd-p4@pa1`, `v407weu6-yd-p4@pa2`, `v407weu6-yd-p4@pa4`, `v407weu6-yd-p4@pa5`, `v407weu6-yd-p4@pa6`, `v407weu6-yd-p4@pa7`, `v407weu6-yd-p4@pa8`, `v407weu6-yd-p4.rx@pa9`, `v407weu6-yd-p4.tx@pa10`, `v407weu6-yd-p4@pa15`, `v407weu6-yd-p4@pb6`, `v407weu6-yd-p4@pb7`, `v407weu6-yd-p4@pb8`, `v407weu6-yd-p4@pb9`, `v407weu6-yd-p4@pb13`, `v407weu6-yd-p4@pc4`, `v407weu6-yd-p4@pe0`, `v407weu6-yd-p4@pe1`, `v407weu6-yd-p4.nrst` | なし |
| CH32L103C8T6（l103c8t6-pico） | 0 本: なし | なし |
| CH32X035C8T6（x035c8t6-pico） | 1 本: `x035c8t6-pico@pa13` | なし |
| CH32X315MCU6（口なし） | 17 本: `<口>@pa0`, `<口>@pa1`, `<口>@pa2`, `<口>@pa5`, `<口>@pa6`, `<口>@pa10`, `<口>@pa11`, `<口>.swdio@pa13`, `<口>.swclk@pa14`, `<口>@pa15`, `<口>@pc4`, `<口>@pc5`, `<口>@pc6`, `<口>.nrst@pd2`, `<口>@pd3`, `<口>.rx@pd4`, `<口>.tx@pd5` | なし |
| CH32H417QEU6（h417qeu6-pico） | 9 本: `h417qeu6-pico@pa4`, `h417qeu6-pico@pa5`, `h417qeu6-pico@pb0`, `h417qeu6-pico@pb1`, `h417qeu6-pico@pd15`, `h417qeu6-pico@pe9`, `h417qeu6-pico@pe11`, `h417qeu6-pico@pe13`, `h417qeu6-pico@pf8` | なし |

## CH32V003F4U6（v003f4u6-uia）

- 口: `oep://30eda0ea068b/v003f4u6-uia`
- いまの線: PA1, PA2, PC0, PC1, PC2, PC3, PC4, PC5, PC6, PC7, PD0, PD2
- コンソール（rx / tx）: PD5, PD6
- 触らない端子: PA1, PA2, PD1, PD3, PD4, PD7
- 足す線（0 本、端子順）: なし

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| デバッグ（SWIO） | 書き込み・デバッガ（run_hardware） | 済 |  |
| リセット（NRST） | 書き込み、periph（リセットの理由） | 済 |  |
| コンソールの UART（rx は DUT の TX、tx は DUT の RX） | すべての試験の READY と応答 | 済 |  |
| 製品ボード（uiapduino:pro-micro-ch32v003-v1.4）: 一般の周辺の試験は汎用の台で流す | runtime, hid だけ | 済 |  |

## CH32V003F4P6（v003f4p6）

- 口: `oep://30eda0e34a0e/v003f4p6`
- いまの線: PC0, PC1, PC2, PC3, PC4, PC5, PC6, PC7, PD0, PD2, PD3, PD4
- コンソール（rx / tx）: PD5, PD6
- 触らない端子: PA1, PA2, PD1, PD7
- 足す線（0 本、端子順）: なし

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| デバッグ（SWIO） | 書き込み・デバッガ（run_hardware） | 済 |  |
| リセット（NRST） | 書き込み、periph（リセットの理由） | 済 |  |
| コンソールの UART（rx は DUT の TX、tx は DUT の RX） | すべての試験の READY と応答 | 済 |  |
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 済 |  |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 済 |  |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 済 |  |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 済 |  |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 済 |  |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 済 |  |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 済 |  |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 済 |  |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 済 |  |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 該当なし | この型番に条件を満たす端子が無い |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 済 |  |
| コンソールの USART の CK（同じ経路） | uart-sync::test_uart_synchronous（UsartSPI） | 済 |  |
| Wire の既定の SDA・SCL | wire | 済 |  |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 済 |  |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 済 |  |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 済 |  |

## CH32V006K8U6（v006k8u6）

- 口: `oep://30eda0e343c6/v006k8u6`
- いまの線: PA0, PA3, PA4, PA5, PA6, PB0, PB1, PB2, PB3, PB4, PB5, PB6, PC0, PC1, PC2, PC3, PC4, PC5, PC6, PC7, PD0, PD2, PD3, PD4, PD7
- コンソール（rx / tx）: PD5, PD6
- 触らない端子: PA1, PA2, PA7, PB3, PD1
- 足す線（0 本、端子順）: なし

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| デバッグ（SWDIO・SWCLK）、いまは 1 線（SWIO） | 書き込み・デバッガ（run_hardware） | 済 |  |
| リセット（NRST） | 書き込み、periph（リセットの理由） | 済 |  |
| コンソールの UART（rx は DUT の TX、tx は DUT の RX） | すべての試験の READY と応答 | 済 |  |
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 済 |  |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 済 |  |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 済 |  |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 済 |  |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 済 |  |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 済 |  |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 済 |  |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 済 |  |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 済 |  |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 済 |  |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 済 |  |
| Wire の既定の SDA・SCL | wire | 済 |  |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 済 |  |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 済 |  |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 済 |  |

## CH32V103R8T6（v103r8t6-evt）

- 口: `oep://80f1b2d80a34/v103r8t6-evt`
- いまの線: PA0, PA1, PA2, PA3, PA4, PA5, PA6, PA7, PB0, PB1, PB6, PB7, PB8, PB9, PB10, PB11, PB12, PB13, PB14, PB15, PC0, PC1, PC2, PC3, PC4, PC5, PC6, PC13
- コンソール（rx / tx）: PA9, PA10
- 触らない端子: PA11, PA12, PA13, PA14, PB2, PC14, PC15
- 足す線（1 本、端子順）: `v103r8t6-evt@pa8`

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| デバッグ（SWDIO・SWCLK） | 書き込み・デバッガ（run_hardware） | 済 |  |
| リセット（NRST） | 書き込み、periph（リセットの理由） | 済 |  |
| コンソールの UART（rx は DUT の TX、tx は DUT の RX） | すべての試験の READY と応答 | 済 |  |
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 済 |  |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 済 |  |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 済 |  |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 未 | PA8 |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 該当なし | 候補は触らない端子かコンソールだけ: PA10, PA9 |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 済 |  |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 済 |  |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 済 |  |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 該当なし | この型番に条件を満たす端子が無い |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 済 |  |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 該当なし | 候補は触らない端子かコンソールだけ: PA11, PA12 |
| コンソールの USART の CK（同じ経路） | uart-sync::test_uart_synchronous（UsartSPI） | 未 | PA8 |
| Wire の既定の SDA・SCL | wire | 済 |  |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 済 |  |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 済 |  |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 済 |  |

## CH32V203C8T6（v203c8t6）

- 口: `oep://e8f60ae84e28/v203c8t6`
- いまの線: PA0, PA1, PA2, PA3, PA4, PA5, PA6, PA7, PB0, PB1, PB6, PB7
- コンソール（rx / tx）: PA9, PA10
- 触らない端子: PA11, PA12, PA13, PA14, PB2, PB6, PB7, PC14, PC15
- 足す線（3 本、端子順）: `v203c8t6@pa8`, `v203c8t6@pa15`, `v203c8t6@pb13`

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| デバッグ（SWDIO・SWCLK） | 書き込み・デバッガ（run_hardware） | 済 |  |
| リセット（NRST） | 書き込み、periph（リセットの理由） | 済 |  |
| コンソールの UART（rx は DUT の TX、tx は DUT の RX） | すべての試験の READY と応答 | 済 |  |
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 済 |  |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 済 |  |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 済 |  |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 未 | PA8 |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 該当なし | 候補は触らない端子かコンソールだけ: PA10, PA9 |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 未 | PB13 |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 未 | PA15 |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 済 |  |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 該当なし | この型番に条件を満たす端子が無い |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 済 |  |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 該当なし | 候補は触らない端子かコンソールだけ: PA11, PA12 |
| コンソールの USART の CK（同じ経路） | uart-sync::test_uart_synchronous（UsartSPI） | 未 | PA8 |
| Wire の既定の SDA・SCL | wire | 済 |  |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 済 |  |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 済 |  |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 済 |  |

## CH32V205RCT6（v205rct6-pico）

- 口: `oep://30eda0e31108/v205rct6-pico`
- いまの線: PA2, PA3, PA6, PA7, PA8, PA9, PA10, PA14, PA15, PB2, PB3, PB4, PB5, PB8, PB9, PB10, PB11, PC0
- コンソール（rx / tx）: PB6, PB7
- 触らない端子: PA0, PA1, PA11, PA12, PA13, PA14, PB6, PB7, PC14, PC15
- 足す線（6 本、端子順）: `v205rct6-pico@pc3`, `v205rct6-pico@pc4`, `v205rct6-pico@pc5`, `v205rct6-pico@pc6`, `v205rct6-pico@pc7`, `v205rct6-pico@pc8`

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| デバッグ（SWDIO・SWCLK）、いまは 1 線（SWIO） | 書き込み・デバッガ（run_hardware） | 済 |  |
| リセット（NRST） | 書き込み、periph（リセットの理由） | 済 |  |
| コンソールの UART（rx は DUT の TX、tx は DUT の RX） | すべての試験の READY と応答 | 済 |  |
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 済 |  |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 済 |  |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 済 |  |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 該当なし | この型番に条件を満たす端子が無い |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 未 | PC6, PC3, PC7, PC4, PC8, PC5 |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 未 | PC3 |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 済 |  |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 済 |  |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 該当なし | この型番に条件を満たす端子が無い |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 済 |  |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 該当なし | この型番に条件を満たす端子が無い |
| PIOC の IO0 | pioc::test_pioc_pulses | 済 |  |
| Wire の既定の SDA・SCL | wire | 済 |  |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 済 |  |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 済 |  |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 済 |  |

## CH32V208WBU6（v208wbu6）

- 口: `oep://30eda0e31451/v208wbu6`
- いまの線: なし
- コンソール（rx / tx）: PA9, PA10（未配線: Serial の既定の端子）
- 触らない端子: PA11, PA12, PA13, PA14, PB2, PB6, PB7, PC14, PC15
- 足す線（13 本、端子順）: `v208wbu6@pa0`, `v208wbu6@pa1`, `v208wbu6@pa2`, `v208wbu6@pa3`, `v208wbu6@pa5`, `v208wbu6@pa6`, `v208wbu6@pa7`, `v208wbu6@pa8`, `v208wbu6@pa15`, `v208wbu6@pb6`, `v208wbu6@pb7`, `v208wbu6@pb13`, `v208wbu6.nrst`

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| デバッグ（SWDIO・SWCLK） | 書き込み・デバッガ（run_hardware） | 済 |  |
| リセット（NRST） | 書き込み、periph（リセットの理由） | 未 | NRST (nrst) |
| コンソールの UART（rx は DUT の TX、tx は DUT の RX） | すべての試験の READY と応答 | 済 |  |
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 未 | PA0, PA1 |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 未 | PA0 |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 未 | PA0, PA1 |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 未 | PA8, PA7, PA6 |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 該当なし | 候補は触らない端子かコンソールだけ: PA10, PA9 |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 未 | PB13 |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 未 | PA15 |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 未 | PA0 |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 該当なし | この型番に条件を満たす端子が無い |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 未 | PA2, PA3 |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 該当なし | 候補は触らない端子かコンソールだけ: PA11, PA12 |
| コンソールの USART の CK（同じ経路） | uart-sync::test_uart_synchronous（UsartSPI） | 未 | PA8 |
| Wire の既定の SDA・SCL | wire | 未 | PB7, PB6 |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 未 | PA0, PA1 |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 未 | PA5, PA6, PA7 |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 未 | PA0, PA1, PA15, PA2 |

## CH32V307VCT6（口なし）

- 口: OEP 未接続（WCH-Link のみ）
- いまの線: なし
- コンソール（rx / tx）: PA9, PA10（未配線: Serial の既定の端子）
- 触らない端子: PA11, PA12, PA13, PA14, PB2, PB6, PB7, PC14, PC15
- 足す線（26 本、端子順）: `<口>@pa0`, `<口>@pa1`, `<口>@pa2`, `<口>@pa4`, `<口>@pa5`, `<口>@pa6`, `<口>@pa7`, `<口>@pa8`, `<口>.rx@pa9`, `<口>.tx@pa10`, `<口>.swdio@pa13`, `<口>.swclk@pa14`, `<口>@pa15`, `<口>@pb6`, `<口>@pb7`, `<口>@pc0`, `<口>@pc1`, `<口>@pc4`, `<口>@pd1`, `<口>@pd3`, `<口>@pd5`, `<口>@pe2`, `<口>@pe3`, `<口>@pe4`, `<口>@pe5`, `<口>.nrst`

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| デバッグ（SWDIO・SWCLK） | 書き込み・デバッガ（run_hardware） | 未 | PA13 (swdio), PA14 (swclk) |
| リセット（NRST） | 書き込み、periph（リセットの理由） | 未 | NRST (nrst) |
| コンソールの UART（rx は DUT の TX、tx は DUT の RX） | すべての試験の READY と応答 | 未 | PA9 (rx), PA10 (tx) |
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 未 | PA0, PA1 |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 未 | PD1 |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 未 | PD1, PD3 |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 未 | PD1, PE3, PE2 |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 未 | PD1, PE3, PD3, PE4, PD5, PE5 |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 未 | PA5 |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 未 | PA15 |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 未 | PA0 |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 該当なし | この型番に条件を満たす端子が無い |
| DAC の出力 | periph（analogWrite の DAC） | 未 | PA4 |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 未 | PC0, PC1 |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 未 | PC4 |
| コンソールの USART の CK（同じ経路） | uart-sync::test_uart_synchronous（UsartSPI） | 未 | PA8 |
| Wire の既定の SDA・SCL | wire | 未 | PB7, PB6 |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 未 | PA0, PA1 |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 未 | PA5, PA6, PA7 |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 未 | PA0, PA1, PA15, PA2 |

## CH32V407WEU6（v407weu6-yd-p4）

- 口: `oep://30eda0ea068b/v407weu6-yd-p4`
- いまの線: なし
- コンソール（rx / tx）: PA9, PA10（未配線: Serial の既定の端子）
- 触らない端子: PA11, PA12, PA13, PA14, PB2, PB6, PB7, PC14, PC15, PE12
- 足す線（20 本、端子順）: `v407weu6-yd-p4@pa0`, `v407weu6-yd-p4@pa1`, `v407weu6-yd-p4@pa2`, `v407weu6-yd-p4@pa4`, `v407weu6-yd-p4@pa5`, `v407weu6-yd-p4@pa6`, `v407weu6-yd-p4@pa7`, `v407weu6-yd-p4@pa8`, `v407weu6-yd-p4.rx@pa9`, `v407weu6-yd-p4.tx@pa10`, `v407weu6-yd-p4@pa15`, `v407weu6-yd-p4@pb6`, `v407weu6-yd-p4@pb7`, `v407weu6-yd-p4@pb8`, `v407weu6-yd-p4@pb9`, `v407weu6-yd-p4@pb13`, `v407weu6-yd-p4@pc4`, `v407weu6-yd-p4@pe0`, `v407weu6-yd-p4@pe1`, `v407weu6-yd-p4.nrst`

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| デバッグ（SWDIO・SWCLK）、いまは 1 線（SWIO） | 書き込み・デバッガ（run_hardware） | 済 |  |
| リセット（NRST） | 書き込み、periph（リセットの理由） | 未 | NRST (nrst) |
| コンソールの UART（rx は DUT の TX、tx は DUT の RX） | すべての試験の READY と応答 | 未 | PA9 (rx), PA10 (tx) |
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 未 | PA0, PA1 |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 未 | PA0 |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 未 | PA0, PA1 |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 未 | PB8, PA15, PB9 |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 該当なし | 候補は触らない端子かコンソールだけ: PA10, PA13, PA14, PA9, PB6, PB7 |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 未 | PB13 |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 未 | PA15 |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 未 | PA0 |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 該当なし | この型番に条件を満たす端子が無い |
| DAC の出力 | periph（analogWrite の DAC） | 未 | PA4 |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 未 | PE0, PE1 |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 未 | PC4 |
| コンソールの USART の CK（同じ経路） | uart-sync::test_uart_synchronous（UsartSPI） | 未 | PA8 |
| Wire の既定の SDA・SCL | wire | 未 | PB7, PB6 |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 未 | PA0, PA1 |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 未 | PA5, PA6, PA7 |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 未 | PA0, PA1, PA15, PA2 |
| WS2812 ブロック（ARGB）の端子 | signals::test_rgb_led_hardware | 未 | PA15 |

## CH32L103C8T6（l103c8t6-pico）

- 口: `oep://e8f60ae84dc6/l103c8t6-pico`
- いまの線: PA0, PA1, PA2, PA3, PA4, PA5, PA6, PA7, PA8, PA15, PB6, PB7, PB13
- コンソール（rx / tx）: PA9, PA10
- 触らない端子: PA11, PA12, PA13, PA14, PB2, PB6, PB7, PC14, PC15, PD0, PD1
- 足す線（0 本、端子順）: なし

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| デバッグ（SWDIO・SWCLK） | 書き込み・デバッガ（run_hardware） | 済 |  |
| リセット（NRST） | 書き込み、periph（リセットの理由） | 済 |  |
| コンソールの UART（rx は DUT の TX、tx は DUT の RX） | すべての試験の READY と応答 | 済 |  |
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 済 |  |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 済 |  |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 済 |  |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 済 |  |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 該当なし | 候補は触らない端子かコンソールだけ: PA10, PA14, PA9 |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 済 |  |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 済 |  |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 済 |  |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 該当なし | この型番に条件を満たす端子が無い |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 済 |  |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 済 |  |
| コンソールの USART の CK（同じ経路） | uart-sync::test_uart_synchronous（UsartSPI） | 済 |  |
| Wire の既定の SDA・SCL | wire | 済 |  |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 済 |  |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 済 |  |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 済 |  |

## CH32X035C8T6（x035c8t6-pico）

- 口: `oep://80f1b2d835d6/x035c8t6-pico`
- いまの線: PA1, PA2, PA3, PA4, PA5, PA6, PA7, PA10, PA11, PB0, PB1, PB2, PB3, PB4, PB5, PB6, PB7, PB8, PB9, PC6, PC7
- コンソール（rx / tx）: PB10, PB11
- 触らない端子: PA21, PC14, PC15, PC16, PC17, PC18, PC19
- 足す線（1 本、端子順）: `x035c8t6-pico@pa13`

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| デバッグ（SWDIO・SWCLK） | 書き込み・デバッガ（run_hardware） | 済 |  |
| リセット（NRST） | 書き込み、periph（リセットの理由） | 済 |  |
| コンソールの UART（rx は DUT の TX、tx は DUT の RX） | すべての試験の READY と応答 | 済 |  |
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 済 |  |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 済 |  |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 済 |  |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 済 |  |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 該当なし | 候補は触らない端子かコンソールだけ: PA21, PB10, PB11, PC14, PC15, PC18, PC19 |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 済 |  |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 済 |  |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 済 |  |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 該当なし | この型番に条件を満たす端子が無い |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 済 |  |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 未 | PA13 |
| コンソールの USART の CK（同じ経路） | uart-sync::test_uart_synchronous（UsartSPI） | 済 |  |
| PIOC の IO0 | pioc::test_pioc_pulses | 済 |  |
| Wire の既定の SDA・SCL | wire | 済 |  |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 済 |  |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 済 |  |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 済 |  |

## CH32X315MCU6（口なし）

- 口: OEP 未接続（WCH-Link のみ）
- いまの線: なし
- コンソール（rx / tx）: PD4, PD5（未配線: Serial の既定の端子）
- 触らない端子: PA13, PA14, PD2, PD4, PD5, PD6, PD7
- 足す線（17 本、端子順）: `<口>@pa0`, `<口>@pa1`, `<口>@pa2`, `<口>@pa5`, `<口>@pa6`, `<口>@pa10`, `<口>@pa11`, `<口>.swdio@pa13`, `<口>.swclk@pa14`, `<口>@pa15`, `<口>@pc4`, `<口>@pc5`, `<口>@pc6`, `<口>.nrst@pd2`, `<口>@pd3`, `<口>.rx@pd4`, `<口>.tx@pd5`

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| デバッグ（SWDIO・SWCLK） | 書き込み・デバッガ（run_hardware） | 未 | PA13 (swdio), PA14 (swclk) |
| リセット（NRST） | 書き込み、periph（リセットの理由） | 未 | PD2 (nrst) |
| コンソールの UART（rx は DUT の TX、tx は DUT の RX） | すべての試験の READY と応答 | 未 | PD4 (rx), PD5 (tx) |
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 未 | PA0, PA1 |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 未 | PC4 |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 未 | PC4, PC5 |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 未 | PC4, PA0, PD3 |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 未 | PC4, PA0, PC5, PA1, PC6, PA2 |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 未 | PA0 |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 未 | PA5 |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 未 | PA0 |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 該当なし | この型番に条件を満たす端子が無い |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 未 | PC4, PC5 |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 該当なし | この型番に条件を満たす端子が無い |
| Wire の既定の SDA・SCL | wire | 該当なし | 候補は触らない端子かコンソールだけ: PD4, PD5 |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 未 | PA0, PA1 |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 未 | PA5, PD3, PA6 |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 未 | PA0, PA1, PA10, PA11 |
| WS2812 ブロック（ARGB）の端子 | signals::test_rgb_led_hardware | 未 | PA15 |

## CH32H417QEU6（h417qeu6-pico）

- 口: `oep://e8f60ae84dc2/h417qeu6-pico`
- いまの線: PA0, PA1, PA6, PA7, PA13, PA14, PB8, PB10, PB11, PC0, PD4, PD5, PD6, PD7, PF3, PF5
- コンソール（rx / tx）: PB14, PB15
- 触らない端子: PA11, PA12, PB3, PB4, PB8, PB9, PC14, PC15
- 足す線（9 本、端子順）: `h417qeu6-pico@pa4`, `h417qeu6-pico@pa5`, `h417qeu6-pico@pb0`, `h417qeu6-pico@pb1`, `h417qeu6-pico@pd15`, `h417qeu6-pico@pe9`, `h417qeu6-pico@pe11`, `h417qeu6-pico@pe13`, `h417qeu6-pico@pf8`

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| デバッグ（SWDIO・SWCLK）、いまは 1 線（SWIO） | 書き込み・デバッガ（run_hardware） | 済 |  |
| リセット（NRST） | 書き込み、periph（リセットの理由） | 済 |  |
| コンソールの UART（rx は DUT の TX、tx は DUT の RX） | すべての試験の READY と応答 | 済 |  |
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 済 |  |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 済 |  |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 済 |  |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 未 | PE9 |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 未 | PE9, PE11, PB0, PE13, PB1 |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 未 | PA5 |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 済 |  |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 済 |  |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 該当なし | この型番に条件を満たす端子が無い |
| DAC の出力 | periph（analogWrite の DAC） | 未 | PA4 |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 済 |  |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 未 | PD15 |
| PIOC の IO0 | pioc::test_pioc_pulses | 済 |  |
| Wire の既定の SDA・SCL | wire | 済 |  |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 済 |  |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 済 |  |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 済 |  |
| HSADC の入力（PF8〜PF10、VDDIO） | hsadc | 未 | PF8 |

## M030

CH32M030G8R7 の板はモーター基板で、端子を出力にしません（defect m030-motor-stage）。GPIO を動かす試験の配線は
しません。

