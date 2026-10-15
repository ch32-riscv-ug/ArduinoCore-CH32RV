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

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| GPIO | 実装済 | pinMode/digitalWrite/digitalRead | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● |  |
| EXTI (外部割込み) | 実装済 | attachInterrupt | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | GPIOの線はX033/X035・X305/X315が24本、他は16本。PVD・RTC・USB起床などGPIO以外の線は未対応 |
| PFIC (割込みコントローラ) | 実装済 | - | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 優先度は全てreset既定のまま |
| SysTick | 実装済 | millis/micros/delay | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● |  |
| RCC (クロック) | 一部実装 | F_CPU | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | HSI(とHSI由来PLL)で系列ごとに1つの固定周波数。V103 72 MHz、V20x/V30x 96 MHz、X033/X035 48 MHz、V00x/M007 24 MHz、V407/V467・X305/X315 20 MHz、L103/M103・V205・M030 8 MHz(PLL未使用)。HSEは未実装(USB HSのみHSEを使う) |
| PWR (低消費電力) | 未実装 | - | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | sleep APIは提供していない |
| FLASH (自己書き換え) | 未実装 | - | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | 自己書き換えとEEPROM emulationのAPIは提供していない |
| USART | 実装済 | Serial | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 全インスタンス(V205/V30x/V407/V467のUSART6〜10も、Serial9まで)。V205・X305/X315は端子ごとのAF番号を書く(ビルドのみ確認)。X033/X035は送信をポーリングで行う(割込み送信の不具合の回避) |
| I2C | 実装済 | Wire | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | controllerはpolling、peripheral(slave)は割込み駆動。V205・X305/X315は端子ごとのAF番号を書く(ビルドのみ確認) |
| SPI | 一部実装 | SPI | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | ◐ | controllerのみ。peripheral(slave)は未実装。V205・X305/X315は端子ごとのAF番号を書く(ビルドのみ確認)。M103はSPI1のみ |
| ADC | 実装済 | analogRead | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | X305/X315はADC1〜4がpadを分け合うため`CH32RV_PIN_TO_ADC_INSTANCE`で切替。内部チャネルは`analogReadVref()`・`analogReadTemp()`・`CH32RV.getVddMillivolts()`(M030はdevice-dataに無く未対応、温度センサはV103/V20x/V205/V30x/V4x7/L103のみ、校正なしの典型値)。L103/M103はEVTと同じVDDA/2のオフセット補正をかける。基準電圧は選べない(`analogReference()`は何もしない)。V003は10 bit |
| DAC | 実装済 | analogWrite(CH32RV_DACn_PIN) |  |  |  |  |  | ● | ● |  |  |  |  | V303/V305/V307/V317/V407/V467のみ。padはdevice-data由来 |
| TIM (PWM/tone/入力捕捉) | 実装済 | analogWrite/tone | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | `tone()`はtimer割込みでpinをtoggle。使うtimerはvariantが選ぶ(`CH32RV_TONE_TIMER`)。V00x/X035/M030は空きが無く**PWMと共有**。PWMはTIM1〜5の既定ピン(tone/Servoが使うtimerは除く。remap先とCHxNは未対応)、V205・X305/X315は端子ごとのAF番号で出す(ビルドのみ確認)。timer管理はTIM1〜10(V30xのTIM8〜10、V4x7のTIM8) |
| LPTIM | 対象外 | - |  |  |  |  |  |  |  | − |  |  |  | L103のみ |
| DMA | 対象外 | - | − | − | − | − | − | − | − | − | − | − | − | Arduino APIには露出しない |
| IWDG | 実装済 | `CH32RV.wdtEnable/wdtFeed` | ● | ● | ● | ● | ● | ● | ● | ● |  | ● | ● | X033/X035はLSIが無くHSI/1024で動く(X035・L103・V103・V307で実機確認)。M030はIWDGが無い |
| WWDG | 未実装 | - | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ | ○ |  |
| RTC | 未実装 | - |  |  | ○ | ○ | ○ | ○ | ○ | ○ |  |  | ○ |  |
| BKP (バックアップレジスタ) | 対象外 | - |  |  | − | − | − | − | − | − |  |  |  |  |
| CRC | 対象外 | - |  |  | − | − | − | − | − | − |  |  | − |  |

## アナログ・タッチ

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| OPA/コンパレータ | 対象外 | - | − | − |  | − | − | − | − | − | − | − |  | CH32固有 |
| TouchKey | 対象外 | - |  | − | − | − | − | − |  | − |  | − |  | CH32固有 |
| PIOC (プログラマブルIO) | 対象外 | - |  |  |  |  | − |  |  |  |  | − |  | X035/V205固有 |

## USB

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| USB (FS device/host) | 一部実装 | `CH32RVUsbDevice` / `CH32RVUsbHost` |  |  | ● | ● | ○ | ● |  | ○ | ○ | ● |  | 採用stackはTinyUSB。deviceはX035で実機確認(ホストはPCとESP32-S3)。V20xはUSBFSとUSBD(fsdev)を実行時に選ぶ。V103/V20x/V30xはビルドのみ。hostはX035(R8T6/C8T6/G8U6/G8R6、F8U6などはシリコンに無い)・V20x・V30xのUSBFS(HCDは自前)。X035 C8T6でESP32-S3相手に実機確認、V20x/V30xはビルドのみ。L103/M103・V205・M030はクロック方針の決定待ちで未実装。工場ISPへの切り替えはX033/X035のみ(docs/usb.ja.md) |
| USB HS | 一部実装 | `CH32RVUsbDevice`(`UsbPort::HighSpeed`) |  |  |  |  | ○ | ● | ○ |  |  |  | ○ | V305/V307/V317(第1世代USBHS、HSEが要る)。ビルドのみ。V303はUSBHSが無い。V205・V407/V467・X305/X315の第2世代USBHSは新しいドライバが要り未実装 |
| USB SS | 対象外 | - |  |  |  |  |  |  |  |  |  |  | − | X315のみ。当面扱わない |
| USB PD | 一部実装 | `CH32RVUsbPd`(library: `USBPD`、`USBPDSource`) |  |  |  |  | ○ |  |  | ○ | ○ | ● | ○ | sink(X035で実機確認)とsource(ビルドのみ)をX033/X035で実装。他のUSBPD搭載seriesは`begin()`がfalseを返す |

## 通信・外部バス

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| CAN | 対象外 | - |  |  |  | − | − | − | − | − |  |  |  |  |
| Ethernet | 対象外 | - |  |  |  | − |  | − | − |  |  |  |  |  |
| I2S | 対象外 | - |  |  |  |  |  | − | − |  |  |  |  |  |
| SDIO | 対象外 | - |  |  |  |  |  | − | − |  |  |  |  | `SD`libraryの下地にはなる |
| FSMC | 対象外 | - |  |  |  |  | − | − | − |  |  |  |  |  |
| QSPI | 対象外 | - |  |  |  |  | − |  |  |  |  |  |  |  |
| PSRAM | 対象外 | - |  |  |  |  |  |  | − |  |  |  |  |  |
| I3C | 対象外 | - |  |  |  |  |  |  | − |  |  |  |  |  |
| DVP (カメラ) | 対象外 | - |  |  |  |  |  | − | − |  |  |  |  |  |
| LTDC (LCD) | 対象外 | - |  |  |  |  |  |  | − |  |  |  |  |  |
| ARGB (LEDドライバ) | 対象外 | - |  |  |  |  |  |  | − |  |  |  | − |  |
| RNG | 対象外 | - |  |  |  |  |  | − | − |  |  |  |  | `random()`はsoftware実装で足りている |
| BLE | 対象外 | - |  |  |  | − |  |  |  |  |  |  |  | V208のみ。専用stackが要る |

## デバッグ出力

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SDI print (debug出力) | 実装済 | SerialSDI(library) | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 送信のみ |
| RTT (debug出力) | 実装済 | SerialRTT(library) | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 双方向。RAM ring bufferを使用 |
| DMDATA mailbox (debug出力) | 実装済 | SerialDMDATA(library) | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 双方向。SerialSDIと同時使用不可 |
| DMSEQ console (debug出力) | 実装済 | SerialDMSeq(library) | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | ● | 双方向。通し番号とCRC-8を使用 |

## RTOS

| ペリフェラル | 状態 | Arduino API | V003 | V00x/M007 | V103 | V203/V208 | V205 | V303/305/307/317 | V407/V467 | L103/M103 | M030 | X033/X035 | X305/X315 | 備考 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| FreeRTOS | 対象外 | - |  |  | − | − | − | − |  | − | − | − |  | coreはbare metal。ADR-0006 |
| RT-Thread | 対象外 | - |  |  | − | − | − | − |  | − | − | − |  | coreはbare metal。ADR-0006 |
| HarmonyOS | 対象外 | - |  |  | − | − | − | − |  | − | − | − |  | coreはbare metal。ADR-0006 |
| TencentOS | 対象外 | - |  |  | − | − | − | − |  | − | − | − |  | coreはbare metal。ADR-0006 |

## この表の使いかた

- 状態は repository 内の実装範囲を表します。
- series列が空欄のペリフェラルは、そのfamilyの silicon に無いかEVTに例が無いかのどちらかです。実装するときは`ch32-device-data`側でも裏を取ってください。
