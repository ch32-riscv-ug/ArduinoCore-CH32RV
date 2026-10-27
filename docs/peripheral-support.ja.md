# ペリフェラル対応表(family別)

**生成物**: `tools/generate/peripheral_matrix.py`がEVTの`EXAM/`ディレクトリ一覧から作ります。手で編集しないでください。

```
uv run --no-project python tools/generate/peripheral_matrix.py \
    --evt-root ~/dev_wch --out docs/peripheral-support.ja.md
```

列はfamily(EVTの単位)ごとで、セルは**このcoreがそのfamilyで使えるか**です。siliconにあるかどうかは、EVTに**そのペリフェラルの例があるか**で判断しています(WCHはペリフェラルごとに例を1つ置くので、これが事実上のペリフェラル一覧になります。EVTは**参照のみ**で、このrepositoryには取り込みません)。
family内のseriesで違うところ(V303にUSBHSが無い、X305にSPI1が無いなど)は備考に書きます。

「状態」と各セルはこのcoreの実装状況で、生成ではなく`peripheral_matrix.py`の`STATUS`と`CELLS`で維持管理しています。実機で確かめた範囲は[test-coverage.ja.md](test-coverage.ja.md)を見てください。

| 記号 | 意味 |
|---|---|
| ● | 使える |
| ◐ | 一部だけ使える(備考に記載) |
| ○ | siliconにあるが未実装 |
| − | siliconにあるがcoreでは提供しない |
| 空欄 | EVTに例が無い |

**空欄は「siliconに無い」とは限りません**。EVTに例が置かれていないだけのこともあります。実装する前にreference manualか`ch32-device-data`で裏を取ってください。

| 状態 | 意味 |
|---|---|
| 実装済 | 公開APIと実装がある |
| 一部実装 | 備考に記載した範囲だけ実装がある |
| 未実装 | siliconに機能はあるがcoreの公開API/実装がない |
| 対象外 | coreの公開APIとして提供しない |

## 基本ペリフェラル

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | H415/H416/H417 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| GPIO | 実装済 | pinMode/digitalWrite/digitalRead | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● |  |
| EXTI (外部割込み) | 実装済 | attachInterrupt | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | GPIOの線はX033/X035・X305/X315が24本、他は16本。PVD・RTC・USB起床などGPIO以外の線は未対応 |
| PFIC (割込みコントローラ) | 実装済 | - | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 優先度は全てreset既定のまま |
| SysTick | 実装済 | millis/micros/delay | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● |  |
| RCC (クロック) | 一部実装 | F_CPU | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | HSI(とHSI由来PLL)。ビルド時にCPU Clockのメニューで選ぶ(項目名に使えるものを書く。取得は`getCpuFrequencyMhz()`/`getApbFrequency()`、実行中に変えるAPIは無い)。既定はV103 72 MHz、V20x/V30x 96 MHz、X033/X035 48 MHz、V00x/M007 24 MHz、L103/M103・V205 8 MHz、M030 72 MHz(実機で確認してEVTの既定に。L103/M103・V205のPLLの項目は未確認)。X315は20 MHz(240 MHzも選べる。HSIからUSBHS PLL、EVTの手順の順に書く。実機未確認)。メニューの無いもの: X305 20 MHz、H415/H416/H417 100 MHz(V3F。SYSCLK は HSI×16 の 400 MHz、V5F は 400 MHz で停止のまま)。HSEは未実装(USB HSとH417のUSB SSのみHSEを使う) |
| PWR (低消費電力) | 未実装 | - | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | sleep APIは提供していない |
| FLASH (自己書き換え) | 一部実装 | - | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ◐ | 公開APIとEEPROM emulationは無い。H415/H416/H417はコア内部の`ch32rv_flash_erase/write`(DFU runtimeが使う)。他は未実装。実機の数字はdocs/flash-raw.ja.md、保存の案はdocs/persistence-spec.ja.md(下書き) |
| USART | 実装済 | Serial | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 全インスタンス(V205/V30x/V407/V467のUSART6〜10も、Serial9まで)。V205・X305/X315は端子ごとのAF番号を書く(ビルドのみ確認)。X033/X035は送信をポーリングで行う(割込み送信の不具合の回避) |
| I2C | 実装済 | Wire | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | controllerはpolling、peripheral(slave)は割込み駆動。V205・X305/X315は端子ごとのAF番号を書く(ビルドのみ確認) |
| SPI | 一部実装 | SPI | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | controllerはSPI。peripheral(slave)はCH32RVSPISlave(CS単位の転送、CSは任意の端子、1バイトごとに割込み。実機はV003/V103/V307/X035/L103、自分の端子をGPIOで叩く自己試験)。V205・X305/X315は端子ごとのAF番号を書く(ビルドのみ確認)。M103はSPI1のみ |
| ADC | 実装済 | analogRead | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | X305/X315はADC1〜4がpadを分け合うため`CH32RV_PIN_TO_ADC_INSTANCE`で切替。内部チャネルは`analogReadVref()`・`analogReadTemp()`・`CH32RV.getVddMillivolts()`(M030はシリコンに無い(RMに記載なし)、温度センサはV103/V20x/V205/V30x/V4x7/L103/M103/H41xのみ、校正なしの典型値)。L103/M103はEVTと同じVDDA/2のオフセット補正をかける。基準電圧は選べない(`analogReference()`は何もしない)。V003は10 bit |
| DAC | 実装済 | analogWrite(CH32RV_DACn_PIN) |  |  |  |  |  | ● | ● |  |  |  |  | ● | V303/V305/V307/V317/V407/V467のみ。padはdevice-data由来 |
| TIM (PWM/tone) | 実装済 | analogWrite/tone | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | `tone()`はtimer割込みでpinをtoggle。使うtimerはvariantが選ぶ(`CH32RV_TONE_TIMER`)。V00x/X035/M030は空きが無く**PWMと共有**。PWMはTIM1〜5とTIM8〜10(V30x・V4x7・H41x。TIM1〜5の端子は奪わない、ビルドのみ)の既定ピン(tone/Servoが使うtimerは除く。remap先とCHxNは未対応)、V205・X305/X315は端子ごとのAF番号で出す(ビルドのみ確認)。timer管理はTIM1〜10(V30xのTIM8〜10、V4x7のTIM8)。入力捕捉・エンコーダ・ETR・DMAバーストは未実装(`pulseIn()`はsoftwareで数える)。CH32RVLogicCapture(TIM+DMAでポートを取り込む)は全系列、実機はH415/H416/H417のみ |
| LPTIM | 対象外 | - |  |  |  |  |  |  |  | − |  |  |  | − | L103のみ |
| DMA | 対象外 | - | − | − | − | − | − | − | − | − | − | − | − | − | Arduino APIには露出しない。CH32RVLogicCapture(全系列、実機はH41xのみ)、CH32RVHsAdc(H41x)、CH32RVAdcStream(ADC1の連続取り込み。全系列、V4x7はDMA1のチャネル12、H41xはDMAMUXでDMA1のチャネル1。実機はX035/V307/V103/V407/H417)が使う |
| IWDG | 実装済 | `CH32RV.wdtEnable/wdtFeed` | ● | ● | ● | ● | ● | ● | ● | ● |  | ● | ● | ● | X033/X035はLSIが無くHSI/1024で動く(X035・L103・V103・V307で実機確認)。M030はIWDGが無い |
| WWDG | 未実装 | - | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ |  |
| RTC | 未実装 | - |  |  | ○ | ○ | ○ | ○ | ○ | ○ |  |  | ○ | ○ |  |
| BKP (バックアップレジスタ) | 対象外 | - |  |  | − | − | − | − | − | − |  |  |  |  |  |
| CRC | 対象外 | - |  |  | − | − | − | − | − | − |  |  | − | − |  |
| ECDC (AES/SM4) | 未実装 | - |  |  |  |  |  |  |  |  |  |  |  | ○ | AES-128/192/256とSM4、ECBとCTRのみ。TLS(mbedTLS)はsoftwareで計算している |
| V5F (2つ目のコア)/HSEM/IPC | 未実装 | - |  |  |  |  |  |  |  |  |  |  |  | ○ | H417。起動、共有メモリ、HSEM、原子操作、GPIOの読み取りを実機で測った(docs/h417-multicore.ja.md)。設計は未 |

## アナログ・タッチ

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | H415/H416/H417 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| OPA/コンパレータ | 対象外 | - | − | − |  | − | − | − | − | − | − | − |  | − | CH32固有 |
| TouchKey | 対象外 | - |  | − | − | − | − | − |  | − |  | − |  | − | CH32固有 |
| HSADC (高速ADC) | 一部実装 | CH32RVHsAdc(library) |  |  |  |  |  |  |  |  |  |  |  | ◐ | H415/H416/H417。DMAで連続取り込み、USBで流す(HsAdcStream) |
| DFSDM (ΣΔフィルタ) | 未実装 | - |  |  |  |  |  |  |  |  |  |  |  | ○ | PDMマイク、ΣΔ変調器の入力。H417 |
| PIOC (プログラマブルIO) | 対象外 | - |  |  |  |  | − |  |  |  |  | − |  | − | X035/V205固有 |

## USB

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | H415/H416/H417 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| USB (FS device/host) | 一部実装 | `CH32RVUsbDevice` / `CH32RVUsbHost` |  |  | ◐ | ◐ | ◐ | ◐ |  | ◐ | ◐ | ● |  | ◐ | stackは自前(TinyUSBは2026-10-20に撤去)。deviceはX035/X033とH417で実機確認、V103・V20x(USBFSとUSBD)・V30x・V205・L103/M103・M030はドライバがありビルドのみ(コントローラとドライバの一覧はdocs/usb.ja.md)。L103/M103・V20x/V30xはCPU ClockがUSBの作れる周波数のときだけ(メニューの項目に書いてある)。hostはX035(R8T6/C8T6/G8U6/G8R6、F8U6などはシリコンに無い)とUSBOTG_FSの系列(V20x・V30x・V205・L103/M103・M030・H41x)のUSBFS。X035 C8T6でESP32-S3相手に実機確認、ほかはビルドのみ。工場ISPへの切り替えはX033/X035(実機確認)とX305/X315(ビルドのみ)。ほかはBOOT_MODEが無いかH41xのように動かない |
| USB HS | 一部実装 | `CH32RVUsbDevice`(`CH32RVUsbController::HighSpeed`) |  |  |  |  | ◐ | ◐ | ◐ |  |  |  | ◐ | ◐ | 第1世代USBHS(V305/V307/V317、HSEが要る)と第2世代USBHS(V205・V407/V467・X305/X315・H41x)のdeviceドライバがあり、どれもビルドのみ。V303はUSBHSが無い。H41xのUSBHSはSWDと端子を共有する |
| USB SS | 一部実装 | `CH32RVUsbDevice`(`CH32RVUsbController::SuperSpeed`) |  |  |  |  |  |  |  |  |  |  | ◐ | ◐ | H417/H416のUSBSS device。H417で実機確認(DFU、vendor bulkで194〜246 MB/s)。X315のdeviceはビルドのみ。hostは未実装 |
| USB PD | 一部実装 | `CH32RVUsbPd`(library: `USBPD`、`USBPDSource`) |  |  |  |  | ◐ |  |  | ◐ | ◐ | ◐ | ◐ | ◐ | sinkとsource。X035のsinkは実機確認、L103/M103・V205・X305/X315・H41x・M030はビルドのみ(CCの端子と設定はlibraries/CH32RVUsbPd/README.ja.md)。X033はCCの端子が無い。M030はUSBPD0(PA0/PA1)だけ(比較器のビットが別。USBPD1は未対応) |

## 通信・外部バス

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | H415/H416/H417 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CAN | 対象外 | - |  |  |  | − | − | − | − | − |  |  |  | − |  |
| Ethernet | 一部実装 | `ETH`(Network library) |  |  |  | ◐ |  | ◐ | ◐ |  |  |  |  | ◐ | H417の内蔵MAC+10/100 PHY(`NetworkEthernet.h`)。DHCP〜HTTPSまで実機確認(TCP 20/24 Mbit/s、docs/network-stack.ja.md)。H415/H416はETHが無い。V407/V467・V307・V317・V208/V203RBはEVTから移してビルドのみ(V208/V203RBはHCLK 60/120 MHzが要り、今の96 MHzでは`begin()`がfalse) |
| I2S | 対象外 | - |  |  |  |  |  | − | − |  |  |  |  | − |  |
| SDIO | 対象外 | - |  |  |  |  |  | − | − |  |  |  |  | − | `SD`libraryの下地にはなる |
| FSMC/FMC | 対象外 | - |  |  |  |  | − | − | − |  |  |  |  | − | H417のFMCはSDRAMも扱う |
| SDMMC | 対象外 | - |  |  |  |  |  |  |  |  |  |  |  | − | H417。host(HS200/HS400)とSDIO slave |
| SAI (音声) | 対象外 | - |  |  |  |  |  |  |  |  |  |  |  | − | TDM、SPDIF出力 |
| SWPMI | 対象外 | - |  |  |  |  |  |  |  |  |  |  |  | − | NFC/UICCのSWP master |
| UHSIF (高速パラレル) | 未実装 | - |  |  |  |  |  |  |  |  |  |  |  | ○ | 8〜32 bitの同期パラレル(FPGA・USB3ブリッジ)。レジスタは非公開(EVTは閉じたライブラリ)。docs/unused-peripherals.ja.md |
| SerDes | 対象外 | - |  |  |  |  |  |  |  |  |  |  |  | − | 1.5 Gbpsの差動リンク |
| QSPI | 対象外 | - |  |  |  |  | − |  |  |  |  |  |  | − |  |
| PSRAM | 対象外 | - |  |  |  |  |  |  | − |  |  |  |  |  |  |
| I3C | 対象外 | - |  |  |  |  |  |  | − |  |  |  |  | − |  |
| DVP (カメラ) | 未実装 | - |  |  |  |  |  | ○ | ○ |  |  |  |  | ○ | パラレル入力(8〜12 bit、外部PCLK)の候補。docs/unused-peripherals.ja.md |
| LTDC (LCD) | 対象外 | - |  |  |  |  |  |  | − |  |  |  |  | − |  |
| GPHA (2D DMA) | 対象外 | - |  |  |  |  |  |  |  |  |  |  |  | − |  |
| ARGB (LEDドライバ) | 対象外 | - |  |  |  |  |  |  | − |  |  |  | − |  |  |
| RNG | 一部実装 | - |  |  |  |  |  | ◐ | ◐ |  |  |  |  | ◐ | TLS(mbedTLS)とTCPの乱数にコア内部で使う(`ch32rv_rng_read`)。公開APIは無い。`random()`はsoftware実装 |
| BLE | 対象外 | - |  |  |  | − |  |  |  |  |  |  |  |  | V208のみ。専用stackが要る |

## デバッグ出力

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | H415/H416/H417 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SDI print (debug出力) | 実装済 | SerialSDI(library) | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 送信のみ |
| RTT (debug出力) | 実装済 | SerialRTT(library) | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 双方向。RAM ring bufferを使用 |
| DMDATA mailbox (debug出力) | 実装済 | SerialDMDATA(library) | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 双方向。SerialSDIと同時使用不可 |
| DMSEQ console (debug出力) | 実装済 | SerialDMSeq(library) | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 双方向。通し番号とCRC-8を使用 |

## RTOS

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | H415/H416/H417 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| FreeRTOS | 対象外 | - |  |  | − | − | − | − |  | − | − | − |  |  | coreはbare metal。ADR-0006 |
| RT-Thread | 対象外 | - |  |  | − | − | − | − |  | − | − | − |  |  | coreはbare metal。ADR-0006 |
| HarmonyOS | 対象外 | - |  |  | − | − | − | − |  | − | − | − |  |  | coreはbare metal。ADR-0006 |
| TencentOS | 対象外 | - |  |  | − | − | − | − |  | − | − | − |  |  | coreはbare metal。ADR-0006 |

## この表の使いかた

- 状態は repository 内の実装範囲を表します。
- series列が空欄のペリフェラルは、そのfamilyの silicon に無いかEVTに例が無いかのどちらかです。実装するときは`ch32-device-data`側でも裏を取ってください。
