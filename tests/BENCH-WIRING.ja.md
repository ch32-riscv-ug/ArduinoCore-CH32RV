# OEP の配線の望ましい形（ファミリーごと）

この文書は `tests/bench_wiring.py` が書きます（手で直さない）。試験がスキップする条件（「ラベルの付いた線が無い」
など）を、いまプローブに付いているラベルと、ch32-device-data の端子表（タイマー・USART・SPI・I2C・ADC・DAC の
役割と経路）、variant の既定の端子、型番の触らない端子（水晶・USB・デバッグなど）から突き合わせています。

- 線はすべて `<口>@<端子>`（役割の要らない普通の線）で足せます。役割のある線（SD、CAN など）は
  [BENCH.ja.md](BENCH.ja.md) §5 の表です。配線の規則（RVSWD・UART・NRST は + 近く、GND 固定の BOOT は触らない）も
  そちらに従います。
- 「足す端子」は条件ごとの最小の候補で、ほかの条件の候補と重なるものは 1 本で両方を満たします。
  **太字** は複数の条件に出てくる端子です。
- OPA・コンパレータ・CAN のループバックは DUT の自己試験なので線は要りません（端子が触らない端子でなければ動く）。
- 1 台の P4 で約 16 本が目安（BENCH.ja.md §5）。足りないときは、上の行（多くの試験が使う条件）から引きます。

## CH32V003F4U6（CH32V003）

- 口: `oep://30eda0ea068b/v003f4u6`
- いまの線: PA1, PA2, PC0, PC1, PC2, PC3, PC4, PC5, PC6, PC7, PD0, PD2
- コンソール（rx / tx）: PD5, PD6
- 触らない端子: PA1, PA2, PD1, PD7
- 足すとよい端子（0 本）: なし

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
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
| Wire の既定の SDA・SCL | wire | 済 |  |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 済 |  |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 済 |  |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 済 |  |

## CH32V003F4P6（CH32V003）

- 口: `oep://30eda0e34a0e/v003f4p6`
- いまの線: PC0, PC1, PC2, PC3, PC4, PC5, PC6, PC7, PD0, PD2, PD3, PD4
- コンソール（rx / tx）: PD5, PD6
- 触らない端子: PA1, PA2, PD1, PD7
- 足すとよい端子（0 本）: なし

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
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
| Wire の既定の SDA・SCL | wire | 済 |  |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 済 |  |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 済 |  |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 済 |  |

## CH32V006K8U6（CH32V006）

- 口: `oep://30eda0e343c6/v006k8u6`
- いまの線: PA0, PA3, PA4, PA5, PA6, PB0, PB1, PB2, PB3, PB4, PB5, PB6, PC0, PC1, PC2, PC3, PC4, PC5, PC6, PC7, PD0, PD2, PD3, PD4, PD7
- コンソール（rx / tx）: PD5, PD6
- 触らない端子: PA1, PA2, PA7, PB3, PD1
- 足すとよい端子（0 本）: なし

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
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

## CH32V103R8T6（CH32V103）

- 口: `oep://80f1b2d80a34/v103r8t6-evt`
- いまの線: PA0, PA1, PA2, PA3, PA4, PA5, PA6, PA7, PB0, PB1, PB10, PB11, PB12, PB13, PB14, PB15, PB6, PB7, PB8, PB9, PC0, PC1, PC13, PC2, PC3, PC4, PC5, PC6
- コンソール（rx / tx）: PA10, PA9
- 触らない端子: PA11, PA12, PA13, PA14, PB2, PC14, PC15
- 足すとよい端子（1 本）: PA8

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
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
| Wire の既定の SDA・SCL | wire | 済 |  |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 済 |  |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 済 |  |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 済 |  |

## CH32V203C8T6（CH32V203）

- 口: `oep://e8f60ae84e28/v203c8t6`
- いまの線: PA0, PA1, PA2, PA3, PA4, PA5, PA6, PA7, PB0, PB1, PB6, PB7
- コンソール（rx / tx）: PA10, PA9
- 触らない端子: PA11, PA12, PA13, PA14, PB2, PB6, PB7, PC14, PC15
- 足すとよい端子（3 本）: PA15, PA8, PB13

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
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
| Wire の既定の SDA・SCL | wire | 済 |  |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 済 |  |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 済 |  |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 済 |  |

## CH32V205RCT6（CH32V205）

- 口: `oep://30eda0e31108/v205rct6-pico`
- いまの線: PA10, PA14, PA15, PA2, PA3, PA6, PA7, PA8, PA9, PB10, PB11, PB2, PB3, PB4, PB5, PB8, PB9, PC0
- コンソール（rx / tx）: PB6, PB7
- 触らない端子: PA0, PA1, PA11, PA12, PA13, PA14, PB6, PB7, PC14, PC15
- 足すとよい端子（6 本）: **PC3**, PC4, PC5, PC6, PC7, PC8

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 済 |  |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 済 |  |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 済 |  |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 該当なし | この型番に条件を満たす端子が無い |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 未 | PC6, **PC3**, PC7, PC4, PC8, PC5 |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 未 | **PC3** |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 済 |  |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 済 |  |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 該当なし | この型番に条件を満たす端子が無い |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 済 |  |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 該当なし | この型番に条件を満たす端子が無い |
| Wire の既定の SDA・SCL | wire | 済 |  |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 済 |  |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 済 |  |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 済 |  |

## CH32V208WBU6（CH32V208）

- 口: `oep://30eda0e31451/v208wbu6`
- いまの線: なし
- コンソール（rx / tx）: PA10, PA9（未配線: Serial の既定の端子）
- 触らない端子: PA11, PA12, PA13, PA14, PB2, PB6, PB7, PC14, PC15
- 足すとよい端子（12 本）: **PA0**, **PA1**, **PA15**, **PA2**, **PA6**, **PA7**, PA3, PA5, PA8, PB13, PB6, PB7

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 未 | **PA0**, **PA1** |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 未 | **PA0** |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 未 | **PA0**, **PA1** |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 未 | PA8, **PA7**, **PA6** |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 該当なし | 候補は触らない端子かコンソールだけ: PA10, PA9 |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 未 | PB13 |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 未 | **PA15** |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 未 | **PA0** |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 該当なし | この型番に条件を満たす端子が無い |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 未 | **PA2**, PA3 |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 該当なし | 候補は触らない端子かコンソールだけ: PA11, PA12 |
| Wire の既定の SDA・SCL | wire | 未 | PB7, PB6 |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 未 | **PA0**, **PA1** |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 未 | PA5, **PA6**, **PA7** |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 未 | **PA0**, **PA1**, **PA15**, **PA2** |

## CH32V307VCT6（CH32V307）

- 口: OEP 未接続（WCH-Link のみ）
- いまの線: なし
- コンソール（rx / tx）: PA10, PA9（未配線: Serial の既定の端子）
- 触らない端子: PA11, PA12, PA13, PA14, PB2, PB6, PB7, PC14, PC15
- 足すとよい端子（20 本）: **PA0**, **PD1**, **PA1**, **PA15**, **PA5**, **PD3**, **PE3**, PA2, PA4, PA6, PA7, PB6, PB7, PC0, PC1, PC4, PD5, PE2, PE4, PE5

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 未 | **PA0**, **PA1** |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 未 | **PD1** |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 未 | **PD1**, **PD3** |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 未 | **PD1**, **PE3**, PE2 |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 未 | **PD1**, **PE3**, **PD3**, PE4, PD5, PE5 |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 未 | **PA5** |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 未 | **PA15** |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 未 | **PA0** |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 該当なし | この型番に条件を満たす端子が無い |
| DAC の出力 | periph（analogWrite の DAC） | 未 | PA4 |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 未 | PC0, PC1 |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 未 | PC4 |
| Wire の既定の SDA・SCL | wire | 未 | PB7, PB6 |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 未 | **PA0**, **PA1** |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 未 | **PA5**, PA6, PA7 |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 未 | **PA0**, **PA1**, **PA15**, PA2 |

## CH32V407WEU6（CH32V407）

- 口: `oep://30eda0ea068b/v407weu6-p4`
- いまの線: なし
- コンソール（rx / tx）: PA10, PA9（未配線: Serial の既定の端子）
- 触らない端子: PA11, PA12, PA13, PA14, PB2, PB6, PB7, PC14, PC15, PE12
- 足すとよい端子（16 本）: **PA0**, **PA1**, **PA15**, PA2, PA4, PA5, PA6, PA7, PB13, PB6, PB7, PB8, PB9, PC4, PE0, PE1

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 未 | **PA0**, **PA1** |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 未 | **PA0** |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 未 | **PA0**, **PA1** |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 未 | PB8, **PA15**, PB9 |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 該当なし | 候補は触らない端子かコンソールだけ: PA10, PA13, PA14, PA9, PB6, PB7 |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 未 | PB13 |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 未 | **PA15** |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 未 | **PA0** |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 該当なし | この型番に条件を満たす端子が無い |
| DAC の出力 | periph（analogWrite の DAC） | 未 | PA4 |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 未 | PE0, PE1 |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 未 | PC4 |
| Wire の既定の SDA・SCL | wire | 未 | PB7, PB6 |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 未 | **PA0**, **PA1** |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 未 | PA5, PA6, PA7 |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 未 | **PA0**, **PA1**, **PA15**, PA2 |
| WS2812 ブロック（ARGB）の端子 | signals::test_rgb_led_hardware | 未 | **PA15** |

## CH32L103C8T6（CH32L103）

- 口: OEP 未接続（WCH-Link のみ）
- いまの線: なし
- コンソール（rx / tx）: PA10, PA9（未配線: Serial の既定の端子）
- 触らない端子: PA11, PA12, PA13, PA14, PB2, PB6, PB7, PC14, PC15, PD0, PD1
- 足すとよい端子（13 本）: **PA1**, **PA0**, **PA2**, **PA7**, **PA6**, PA15, PA3, PA4, PA5, PA8, PB13, PB6, PB7

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 未 | **PA0**, **PA1** |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 未 | **PA1** |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 未 | **PA1**, **PA7** |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 未 | PA8, **PA7**, **PA6** |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 該当なし | 候補は触らない端子かコンソールだけ: PA10, PA14, PA9, PB6, PB7 |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 未 | PB13 |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 未 | PA4 |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 未 | **PA0** |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 該当なし | この型番に条件を満たす端子が無い |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 未 | **PA2**, PA3 |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 未 | **PA2** |
| Wire の既定の SDA・SCL | wire | 未 | PB7, PB6 |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 未 | **PA0**, **PA1** |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 未 | PA5, **PA6**, **PA7** |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 未 | **PA0**, **PA1**, PA15, **PA2** |

## CH32X035C8T6（CH32X035）

- 口: `oep://80f1b2d835d6/x035c8t6-pico`
- いまの線: PA1, PA10, PA11, PA2, PA3, PA4, PA5, PA6, PA7, PB0, PB1, PB2, PB3, PB4, PB5, PB6, PB7, PB8, PB9, PC6, PC7
- コンソール（rx / tx）: PB10, PB11
- 触らない端子: PA21, PC14, PC15, PC16, PC17, PC18, PC19
- 足すとよい端子（1 本）: PA13

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
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
| Wire の既定の SDA・SCL | wire | 済 |  |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 済 |  |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 済 |  |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 済 |  |

## CH32X315MCU6（CH32X315）

- 口: OEP 未接続（WCH-Link のみ）
- いまの線: なし
- コンソール（rx / tx）: PD4, PD5（未配線: Serial の既定の端子）
- 触らない端子: PA13, PA14, PD2, PD4, PD5, PD6, PD7
- 足すとよい端子（12 本）: **PA0**, **PC4**, **PA1**, **PC5**, **PA5**, **PD3**, PA10, PA11, PA15, PA2, PA6, PC6

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 未 | **PA0**, **PA1** |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 未 | **PC4** |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 未 | **PC4**, **PC5** |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 未 | **PC4**, **PA0**, **PD3** |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 未 | **PC4**, **PA0**, **PC5**, **PA1**, PC6, PA2 |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 未 | **PA0** |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 未 | **PA5** |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 未 | **PA0** |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 該当なし | この型番に条件を満たす端子が無い |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 未 | **PC4**, **PC5** |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 該当なし | この型番に条件を満たす端子が無い |
| Wire の既定の SDA・SCL | wire | 該当なし | 候補は触らない端子かコンソールだけ: PD4, PD5 |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 未 | **PA0**, **PA1** |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 未 | **PA5**, **PD3**, PA6 |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 未 | **PA0**, **PA1**, PA10, PA11 |
| WS2812 ブロック（ARGB）の端子 | signals::test_rgb_led_hardware | 未 | PA15 |

## CH32H417QEU6（CH32H417）

- 口: `oep://e8f60ae84dc2/h417qeu6-pico`
- いまの線: PA0, PA1, PA13, PA14, PA6, PA7, PB10, PB11, PB8, PC0, PD4, PD5, PD6, PD7, PF3, PF5
- コンソール（rx / tx）: PB14, PB15
- 触らない端子: PA11, PA12, PB3, PB4, PB8, PB9, PC14, PC15
- 足すとよい端子（9 本）: **PE9**, PA4, PA5, PB0, PB1, PD15, PE11, PE13, PF8

| 条件 | 使う試験 | いま | 足す端子 |
|---|---|---|---|
| GPIO の線 2 本 | gpio、periph（sleep / watchdog）、SoftwareSerial | 済 |  |
| タイマーの CH1 か CH2 | signals（PWM・捕捉・ワンパルスのトリガ・timerPulseIn） | 済 |  |
| 1 つのタイマー・経路の CH1 と CH2 | signals::test_pwm_timer、test_phase_shift | 済 |  |
| CHx・CHxN・BKIN（同じ経路） | signals::test_complementary（BKIN でブレーキも） | 未 | **PE9** |
| CH1〜CH3 と CH1N〜CH3N（同じ経路） | signals::test_six_step | 未 | **PE9**, PE11, PB0, PE13, PB1 |
| CHxN だけの端子（analogWrite() が CHxN を取る） | signals::test_pwm_complementary | 未 | PA5 |
| remap 先にだけあるタイマーの端子 | signals::test_pwm_route | 済 |  |
| ADC のチャネル | periph（analogRead・ストリーム・ウォッチドッグ）、adc | 済 |  |
| ADC の外部トリガの端子と別のチャネル | adc（beginExternal） | 該当なし | この型番に条件を満たす端子が無い |
| DAC の出力 | periph（analogWrite の DAC） | 未 | PA4 |
| 2 本目の USART（TX と RX） | uart（Serial2 など）、IrDA、RS485 の DE と合わせて | 済 |  |
| コンソールの USART の CTS か RTS | uart::test_uart_flow | 未 | PD15 |
| Wire の既定の SDA・SCL | wire | 済 |  |
| SoftWire の 2 本（Wire の端子以外、EXTI の番号が別） | softwire | 済 |  |
| SPI の既定の SCK・MISO・MOSI と CS（NSS の端子なら setHwCs も） | spi（transferAsync・setHwCs） | 済 |  |
| SoftSPI の 4 本（SCK と CS の EXTI の番号が別） | softspi | 済 |  |
| HSADC の入力（PF8〜PF10、VDDIO） | hsadc | 未 | PF8 |

## M030

CH32M030G8R7 の板はモーター基板で、端子を出力にしません（defect m030-motor-stage）。GPIO を動かす試験の配線は
しません。

