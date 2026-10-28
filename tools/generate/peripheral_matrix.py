#!/usr/bin/env python3
"""Peripheral x series support matrix, from the EVT example trees.

What each family's silicon has is not in ch32-device-data, but WCH ships one
EVT example directory per peripheral per family, and that listing is a fact we
can read: `EVT/EXAM/I2C` exists exactly for the families that have an I2C.
So the presence half of the table is generated, not typed.

The EVT trees are **reference only** (they are never copied into this
repository), and they are not present in CI, so this is a tool a maintainer
runs by hand rather than a test:

  uv run --no-project python tools/generate/peripheral_matrix.py \
      --evt-root ~/dev_wch --tables ~/dev_wch/ch32-device-data --out docs/peripheral-support.ja.md

Our own status is the curated half - it cannot be derived from anything - and
lives in STATUS below, so that regenerating never silently drops it.
"""
import argparse
import pathlib
import sys

# EVT directory -> (display name, group). Directories that are not a
# peripheral (build scaffolding, the shared SRC tree, RTOS ports) are either
# grouped or dropped, so the table stays about hardware.
PERIPHERALS = {
    "GPIO":      ("GPIO", "basic"),
    "EXTI":      ("EXTI (外部割込み)", "basic"),
    "INT":       ("PFIC (割込みコントローラ)", "basic"),
    "SYSTICK":   ("SysTick", "basic"),
    "RCC":       ("RCC (クロック)", "basic"),
    "PWR":       ("PWR (低消費電力)", "basic"),
    "FLASH":     ("FLASH (自己書き換え)", "basic"),
    "USART":     ("USART", "basic"),
    "I2C":       ("I2C", "basic"),
    "SPI":       ("SPI", "basic"),
    "ADC":       ("ADC", "basic"),
    "DAC":       ("DAC", "basic"),
    "TIM":       ("TIM (PWM/tone)", "basic"),
    "LPTIM":     ("LPTIM", "basic"),
    "DMA":       ("DMA", "basic"),
    "IWDG":      ("IWDG", "basic"),
    "WWDG":      ("WWDG", "basic"),
    "RTC":       ("RTC", "basic"),
    "BKP":       ("BKP (バックアップレジスタ)", "basic"),
    "CRC":       ("CRC", "basic"),
    "ECDC":      ("ECDC (AES/SM4)", "basic"),
    "OPA":       ("OPA/コンパレータ", "analog"),
    "TOUCHKEY":  ("TouchKey", "analog"),
    "TKEY":      ("TouchKey", "analog"),
    "TKey":      ("TouchKey", "analog"),
    "HSADC":     ("HSADC (高速ADC)", "analog"),
    "DFSDM":     ("DFSDM (ΣΔフィルタ)", "analog"),
    "PIOC":      ("PIOC (プログラマブルIO)", "analog"),
    "USB":       ("USB (FS device/host)", "usb"),
    "USBFS":     ("USB (FS device/host)", "usb"),
    "USBHS":     ("USB HS", "usb"),
    "USBSS":     ("USB SS", "usb"),
    "USBPD":     ("USB PD", "usb"),
    "CAN":       ("CAN", "comm"),
    "ETH":       ("Ethernet", "comm"),
    "I2S":       ("I2S", "comm"),
    "SDIO":      ("SDIO", "comm"),
    "FSMC":      ("FSMC/FMC", "comm"),
    "FMC":       ("FSMC/FMC", "comm"),
    "SDMMC":     ("SDMMC", "comm"),
    "SAI":       ("SAI (音声)", "comm"),
    "SWPMI":     ("SWPMI", "comm"),
    "UHSIF":     ("UHSIF (高速パラレル)", "comm"),
    "SerDes":    ("SerDes", "comm"),
    "QSPI":      ("QSPI", "comm"),
    "PSRAM":     ("PSRAM", "comm"),
    "I3C":       ("I3C", "comm"),
    "DVP":       ("DVP (カメラ)", "comm"),
    "LTDC":      ("LTDC (LCD)", "comm"),
    "GPHA":      ("GPHA (2D DMA)", "comm"),
    "ARGB":      ("ARGB (LEDドライバ)", "comm"),
    "RNG":       ("RNG", "comm"),
    "BLE":       ("BLE", "comm"),
    "SDI_Printf": ("SDI print (debug出力)", "debug"),
    "FreeRTOS":  ("FreeRTOS", "rtos"),
    "RT-Thread": ("RT-Thread", "rtos"),
    "rt-thread": ("RT-Thread", "rtos"),
    "HarmonyOS": ("HarmonyOS", "rtos"),
    "TencentOS": ("TencentOS", "rtos"),
}

# EVT directories that are not a peripheral at all.
IGNORED = {"SRC", "APPLICATION", "IAP", "USART_IAP", "CPU", "FPU", "PMP",
           "RunInRam", "RunInRam_LP", "VoiceRcgExam"}

# Features WCH shows as an example inside another directory (EXAM/<dir>/<sub>),
# or as one of the directories above that are not a peripheral but are a
# feature of the chip. (directory, name prefixes of the example) -> (row, group).
SUBFEATURES = [
    (("TIM", ("Input_Capture", "Bothedge_Capture")), ("TIM 入力捕捉", "basic")),
    (("TIM", ("Encoder",)), ("TIM エンコーダ", "basic")),
    (("TIM", ("ComplementaryOutput", "Dead_", "PWM_6_Step", "One_Pulse", "Output_Compare")),
     ("TIM 相補PWM・デッドタイム・ワンパルス", "basic")),
    (("ADC", ("ADC_DMA", "ADC_ScanOut", "TIM_Trigger", "SLTIM_Trigger", "ExtLines_Trigger")),
     ("ADC 連続・DMA・トリガ", "analog")),
    (("ADC", ("AnalogWatchdog",)), ("ADC アナログウォッチドッグ", "analog")),
    (("ADC", ("DualADC",)), ("ADC デュアル", "analog")),
    (("OPA", ("CMP",)), ("コンパレータ", "analog")),
    (("PWR", ("PVD",)), ("PVD (電圧検出)", "basic")),
    (("RCC", ("MCO",)), ("MCO (クロック出力)", "basic")),
    (("RCC", ("HSE_CLK",)), ("HSE (外部水晶)", "basic")),
    (("PMP", ("",)), ("PMP (メモリ保護)", "basic")),
    (("CPU", ("PMP",)), ("PMP (メモリ保護)", "basic")),
    (("FPU", ("",)), ("FPU", "basic")),
    (("CPU", ("FPU",)), ("FPU", "basic")),
    (("CPU", ("ICache",)), ("ICache", "basic")),
    (("CPU", ("HSEM", "IPC")), ("V5F (2つ目のコア)/HSEM/IPC", "basic")),
    (("RunInRam", ("",)), ("RAM実行", "basic")),
    (("RunInRam_LP", ("",)), ("RAM実行", "basic")),
]

# The second source: ch32-device-data's register blocks (index/register_blocks.csv,
# type column), for what has registers but no EVT example. type -> row.
BLOCK_ROWS = {
    "ADC": "ADC", "ARGB": "ARGB (LEDドライバ)", "AWU": "AWU (自動起床)", "BKP": "BKP (バックアップレジスタ)",
    "CAN": "CAN", "CRC": "CRC", "DAC": "DAC", "DFSDM_Channel": "DFSDM (ΣΔフィルタ)",
    "DFSDM_FLT": "DFSDM (ΣΔフィルタ)", "DMA": "DMA", "DVP": "DVP (カメラ)", "ECDC": "ECDC (AES/SM4)",
    "ETH": "Ethernet", "ETH10M": "Ethernet", "EXTI": "EXTI (外部割込み)", "FLASH": "FLASH (自己書き換え)",
    "FMC_Bank1": "FSMC/FMC", "FSMC_Bank1": "FSMC/FMC", "GPHA": "GPHA (2D DMA)", "GPIO": "GPIO",
    "HSADC": "HSADC (高速ADC)", "I2C": "I2C", "I3C": "I3C", "IWDG": "IWDG", "LPTIM": "LPTIM",
    "LTDC": "LTDC (LCD)", "LVDS": "LVDS", "OB": "オプションバイト", "OPA": "OPA/コンパレータ",
    "PSRAM": "PSRAM", "PWR": "PWR (低消費電力)", "QSPI": "QSPI", "RCC": "RCC (クロック)", "RNG": "RNG",
    "RTC": "RTC", "SAI_Block": "SAI (音声)", "SDIO": "SDIO", "SDMMC": "SDMMC", "SDS": "SerDes",
    "SPI": "SPI", "SWPMI": "SWPMI", "TIM": "TIM (PWM/tone)", "TKEY": "TouchKey", "UHSIF": "UHSIF (高速パラレル)",
    "USART": "USART", "USBFSD": "USB (FS device/host)", "USBFSH": "USB (FS device/host)",
    "USBHSD": "USB HS", "USBHSH": "USB HS", "USBPD": "USB PD", "USBSSD": "USB SS", "USBSSH": "USB SS",
    "WWDG": "WWDG",
}

GROUPS = [
    ("basic", "基本ペリフェラル"),
    ("analog", "アナログ・タッチ"),
    ("usb", "USB"),
    ("comm", "通信・外部バス"),
    ("debug", "デバッグ出力"),
    ("rtos", "RTOS"),
]

# EVT family directory -> the series this platform builds from it. The EVT
# tree is per family; our board list is per series (SERIES_CONFIG in
# generate.py), and one EVT covers several series.
EVT_FAMILY = [
    ("CH32V003", "V003"),
    ("CH32V006", "V00x/M007"),
    ("CH32V103", "V103"),
    ("CH32V20x", "V203/V208"),
    ("CH32V205", "V205"),
    ("CH32V307", "V303/305/307/317"),
    ("CH32V407", "V407/V467"),
    ("CH32L103", "L103/M103"),
    ("CH32M030", "M030"),
    ("CH32X035", "X033/X035"),
    ("CH32X315", "X305/X315"),
    ("CH32H417", "H415/H416/H417"),
]

# Our side. (状態, 公開API, 備考). Nothing here is derived - it is the
# maintainer's record of implementation coverage.
#
#   実装済     公開APIと実装がある
#   一部実装   備考に記載した範囲だけ実装がある
#   未実装     siliconに機能はあるがcoreの公開API/実装がない
#   対象外     coreの公開APIとして提供しない
STATUS = {
    "GPIO": ("実装済", "pinMode/digitalWrite/digitalRead", ""),
    "EXTI (外部割込み)": ("実装済", "attachInterrupt",
                          "GPIOの線はX033/X035・X305/X315が24本、他は16本。PVD・RTC・USB起床などGPIO以外の線は未対応"),
    "PFIC (割込みコントローラ)": ("実装済", "-", "優先度は全てreset既定のまま"),
    "SysTick": ("実装済", "millis/micros/delay", ""),
    "RCC (クロック)": ("一部実装", "F_CPU",
        "HSI(とHSI由来PLL)。ビルド時にCPU Clockのメニューで選ぶ(項目名に使えるものを書く。取得は`getCpuFrequencyMhz()`/`getApbFrequency()`、実行中に変えるAPIは無い)。既定はV103 72 MHz、V20x/V30x 96 MHz、X033/X035 48 MHz、V00x/M007 24 MHz、L103/M103 8 MHz、M030 72 MHz・V205 160 MHz(実機で確認してEVTの既定に。L103/M103のPLLの項目は未確認)。X315は20 MHz(240 MHzも選べる。HSIからUSBHS PLL、EVTの手順の順に書く。X315MCU6で実機確認)。メニューの無いもの: X305 20 MHz、H415/H416/H417 100 MHz(V3F。SYSCLK は HSI×16 の 400 MHz、V5F は 400 MHz で停止のまま)。HSEは未実装(USB HSとH417のUSB SSのみHSEを使う)"),
    "PWR (低消費電力)": ("一部実装", "CH32RV.lightSleep / deepSleep",
        "V003・V00x・X033/X035はAWU、V20x・V205・V30x・L103/M103はRTCのアラームで起こす。RAMを残す深い休止(V00x・X035のスタンバイ、RTCの系列のストップ)、タイマーと端子(EXTI)で起床、millis()は測った分を足す(RTCの系列は端子での起床も測れる)、WDTは休止中は止まって見える(延ばして間で餌)、deepSleep()はRTCの系列でタイマーだけ・WDTなしなら本物のスタンバイ、ほかは休止のあと再起動。V103・M030・X315・V4x7・H41xは未対応。設計と実測はdocs/peripheral-a-design.ja.md 5.1"),
    "FLASH (自己書き換え)": ("実装済", "ch32rv_flash.h",
                            "全系列で同じ関数(消す・書く・消して書く、範囲と1単位)とCH32RV_FLASH_STORE(末尾の確保)。"
                            "上級者向け。推奨は外部のSD・EEPROM。EEPROM.hは無い。例はFlashSettings・FlashSettingsTwoBanks。"
                            "H415/H416/H417は自分のイメージも書き換えられる(DFU runtimeが使う)。docs/flash-store.ja.md"),
    "USART": ("実装済", "Serial",
        "全インスタンス(V205/V30x/V407/V467のUSART6〜10も、Serial9まで)。V205・X305/X315は端子ごとのAF番号を書く(V205RCT6・X315MCU6で既定のTXの波形を確認)。X033/X035は送信をポーリングで行う(割込み送信の不具合の回避)"),
    "I2C": ("実装済", "Wire",
        "controllerはpolling、peripheral(slave)は割込み駆動。V205・X305/X315は端子ごとのAF番号を書く(未確認。プルアップの配線が要る)"),
    "SPI": ("一部実装", "SPI",
        "controllerはSPI。peripheral(slave)はCH32RVSPISlave(CS単位の転送、CSは任意の端子、1バイトごとに割込み。実機はV003/V103/V205/V307/X035/X315/L103、自分の端子をGPIOで叩く自己試験)。V205・X305/X315は端子ごとのAF番号を書く(V205・X315でSCKの波形とスレーブの自己試験を確認)。M103はSPI1のみ"),
    "ADC": ("実装済", "analogRead",
            "X305/X315はADC1〜4がpadを分け合うため`CH32RV_PIN_TO_ADC_INSTANCE`で切替。"
            "内部チャネルは`analogReadVref()`・`analogReadTemp()`・`CH32RV.getVddMillivolts()`"
            "(M030はシリコンに無い(RMに記載なし)、温度センサはV103/V20x/V205/V30x/V4x7/L103/M103/H41xのみ、"
            "校正なしの典型値)。L103/M103はEVTと同じVDDA/2のオフセット補正をかける。"
            "基準電圧は選べない(`analogReference()`は何もしない)。V003は10 bit"),
    "DAC": ("実装済", "analogWrite(CH32RV_DACn_PIN)",
            "V303/V305/V307/V317/V407/V467のみ。padはdevice-data由来"),
    "TIM (PWM/tone)": ("実装済", "analogWrite/tone",
        "`tone()`はtimer割込みでpinをtoggle。使うtimerはvariantが選ぶ(`CH32RV_TONE_TIMER`)。V00x/X035/M030は空きが無く**PWMと共有**。PWMはTIM1〜5とTIM8〜10(V30x・V4x7・H41x。TIM1〜5の端子は奪わない、ビルドのみ)の既定ピン(tone/Servoが使うtimerは除く。remap先とCHxNは未対応)、V205・X305/X315は端子ごとのAF番号で出す(V205のPA0・X315のPA4でTIM2のPWMを確認)。timer管理はTIM1〜10(V30xのTIM8〜10、V4x7のTIM8)。入力捕捉・エンコーダはCH32RVTimerInput(library)。ETR・DMAバーストは未実装。CH32RVLogicCapture(TIM+DMAでポートを取り込む)は全系列、実機はH415/H416/H417・V205・X315"),
    "LPTIM": ("対象外", "-", "L103のみ"),
    "DMA": ("対象外", "-",
        "Arduino APIには露出しない。CH32RVLogicCapture(全系列、実機はH41xのみ)、CH32RVHsAdc(H41x)、CH32RVAdcStream(ADC1の連続取り込み。全系列、V4x7はDMA1のチャネル12、H41xはDMAMUXでDMA1のチャネル1。実機はX035/V307/V103/V205/V407/X315/H417)が使う"),
    "IWDG": ("実装済", "`CH32RV.wdtEnable/wdtFeed`",
             "X033/X035はLSIが無くHSI/1024で動く(X035・L103・V103・V307で実機確認)。M030はIWDGが無い"),
    "WWDG": ("未実装", "-", ""),
    "RTC": ("一部実装", "time() / settimeofday()",
        "V20x・V205・V30x・L103/M103: time()とsettimeofday()がRTC(LSIを測って1 Hz、カウンタはUNIX秒)を読み書きし、リセットとスタンバイをまたいで時刻が残る。休止のアラームにも使う。V20x・V205・V307・L103で実機確認(2026-10-28)。V103は同じRTCだが休止から起きなかったので外している。LSEは未対応。H41x・V4x7・X315は未実装"),
    "BKP (バックアップレジスタ)": ("一部実装", "-",
        "RTCの系列でDR1〜DR4をコアが使う(RTCの印、分周、settimeofday()のずれ)。DR5以降はスケッチが直接使える(APIは無い)"),
    "CRC": ("対象外", "-", ""),
    "ECDC (AES/SM4)": ("未実装", "-", "AES-128/192/256とSM4、ECBとCTRのみ。TLS(mbedTLS)はsoftwareで計算している"),
    "OPA/コンパレータ": ("対象外", "-", "CH32固有"),
    "TouchKey": ("一部実装", "touchRead / touchSetCycles",
        "arduino-esp32と同じ名前で生の数値。ADC1のTKey(CTLR1 bit24)、最短のサンプル時間とEXTTRIG切りで測る(EVTの長い設定のままでは4095か0)。何もつながない端子で範囲の半分前後になる充電時間を既定にした。V005〜V007/M007・X033/X035・V20x・V30x(X035・V006・V203・V307で値を確認、指で触った向きは未確認)。L103は同じ手順で読めず、V205・H41xは別のTKEYブロックで未実装、V103は未確認"),
    "PIOC (プログラマブルIO)": ("対象外", "-", "X035/V205固有"),
    "HSADC (高速ADC)": ("一部実装", "CH32RVHsAdc(library)",
                        "H415/H416/H417。DMAで連続取り込み、USBで流す(HsAdcStream)"),
    "DFSDM (ΣΔフィルタ)": ("未実装", "-", "PDMマイク、ΣΔ変調器の入力。H417"),
    "USB (FS device/host)": ("一部実装", "`CH32RVUsbDevice` / `CH32RVUsbHost`",
                             "stackは自前(TinyUSBは2026-10-20に撤去)。deviceはX035/X033とH417で実機確認、"
                             "V103・V20x(USBFSとUSBD)・V30x・V205・L103/M103・M030はドライバがありビルドのみ"
                             "(コントローラとドライバの一覧はdocs/usb.ja.md)。L103/M103・V20x/V30xはCPU ClockがUSBの作れる周波数のときだけ"
                             "(メニューの項目に書いてある)。hostはX035(R8T6/C8T6/G8U6/G8R6、F8U6などはシリコンに無い)と"
                             "USBOTG_FSの系列(V20x・V30x・V205・L103/M103・M030・H41x)のUSBFS。"
                             "X035 C8T6でESP32-S3相手に実機確認、ほかはビルドのみ。工場ISPへの切り替えはX033/X035(実機確認)とX305/X315(ビルドのみ)。ほかはBOOT_MODEが無いかH41xのように動かない"),
    "USB HS": ("一部実装", "`CH32RVUsbDevice`(`CH32RVUsbController::HighSpeed`)",
               "第1世代USBHS(V305/V307/V317、HSEが要る)と第2世代USBHS(V205・V407/V467・X305/X315・H41x)の"
               "deviceドライバがあり、どれもビルドのみ。V303はUSBHSが無い。H41xのUSBHSはSWDと端子を共有する"),
    "USB SS": ("一部実装", "`CH32RVUsbDevice`(`CH32RVUsbController::SuperSpeed`)",
               "H417/H416のUSBSS device。H417で実機確認(DFU、vendor bulkで194〜246 MB/s)。"
               "X315のdeviceはビルドのみ。hostは未実装"),
    "USB PD": ("一部実装", "`CH32RVUsbPd`(library: `USBPD`、`USBPDSource`)",
               "sinkとsource。X035のsinkは実機確認、L103/M103・V205・X305/X315・H41x・M030はビルドのみ"
               "(CCの端子と設定はlibraries/CH32RVUsbPd/README.ja.md)。X033はCCの端子が無い。"
               "M030はUSBPD0(PA0/PA1)だけ(比較器のビットが別。USBPD1は未対応)"),
    "CAN": ("対象外", "-", ""),
    "Ethernet": ("一部実装", "`ETH`(Network library)",
                 "H417の内蔵MAC+10/100 PHY(`NetworkEthernet.h`)。DHCP〜HTTPSまで実機確認"
                 "(TCP 20/24 Mbit/s、docs/network-stack.ja.md)。H415/H416はETHが無い。"
                 "V407/V467・V307・V317・V208/V203RBはEVTから移してビルドのみ(V208/V203RBはHCLK 60/120 MHzが要り、"
                 "今の96 MHzでは`begin()`がfalse)"),
    "I2S": ("対象外", "-", ""),
    "SDIO": ("対象外", "-", "`SD`libraryの下地にはなる"),
    "FSMC/FMC": ("対象外", "-", "H417のFMCはSDRAMも扱う"),
    "SDMMC": ("対象外", "-", "H417。host(HS200/HS400)とSDIO slave"),
    "SAI (音声)": ("対象外", "-", "TDM、SPDIF出力"),
    "SWPMI": ("対象外", "-", "NFC/UICCのSWP master"),
    "UHSIF (高速パラレル)": ("未実装", "-", "8〜32 bitの同期パラレル(FPGA・USB3ブリッジ)。レジスタは非公開(EVTは閉じたライブラリ)。"
                             "docs/unused-peripherals.ja.md"),
    "SerDes": ("対象外", "-", "1.5 Gbpsの差動リンク"),
    "QSPI": ("対象外", "-", ""),
    "PSRAM": ("対象外", "-", ""),
    "I3C": ("対象外", "-", ""),
    "DVP (カメラ)": ("未実装", "-", "パラレル入力(8〜12 bit、外部PCLK)の候補。docs/unused-peripherals.ja.md"),
    "LTDC (LCD)": ("対象外", "-", ""),
    "GPHA (2D DMA)": ("対象外", "-", ""),
    "ARGB (LEDドライバ)": ("対象外", "-", ""),
    "RNG": ("一部実装", "-", "TLS(mbedTLS)とTCPの乱数にコア内部で使う(`ch32rv_rng_read`)。公開APIは無い。"
                     "`random()`はsoftware実装"),
    "BLE": ("対象外", "-", "V208のみ。専用stackが要る"),
    "SDI print (debug出力)": ("実装済", "SerialSDI(library)", "送信のみ"),
    "RTT (debug出力)": ("実装済", "SerialRTT(library)", "双方向。RAM ring bufferを使用"),
    "DMDATA mailbox (debug出力)": ("実装済", "SerialDMDATA(library)",
                                      "双方向。SerialSDIと同時使用不可"),
    "DMSEQ console (debug出力)": ("実装済", "SerialDMSeq(library)",
                                    "双方向。通し番号とCRC-8を使用"),
    "V5F (2つ目のコア)/HSEM/IPC": ("未実装", "-",
                                  "H417。起動、共有メモリ、HSEM、原子操作、GPIOの読み取りを実機で測った"
                                  "(docs/h417-multicore.ja.md)。設計は未"),
    "FreeRTOS": ("対象外", "-", "coreはbare metal。ADR-0006"),
    "RT-Thread": ("対象外", "-", "coreはbare metal。ADR-0006"),
    "HarmonyOS": ("対象外", "-", "coreはbare metal。ADR-0006"),
    "TencentOS": ("対象外", "-", "coreはbare metal。ADR-0006"),
    "TIM 入力捕捉": ("実装済", "CH32RVTimerInput(library)",
        "`CH32RVPulseCapture`: PWM入力モードで周期とHighの時間(割込みなし、`read()`で最新の1周期)。端子はtimerのCH1/CH2(既定の経路)。エッジごとの時刻・通知は未実装。実機はV003/V006/V103/V203/V205/V307/L103/X035/X315/H417(同じ端子をGPIO・tone()・Servoで動かす自己試験)。`pulseIn()`はsoftwareのまま"),
    "TIM エンコーダ": ("実装済", "CH32RVTimerInput(library)",
        "`CH32RVEncoder`: エンコーダモードでx4/x2/x1、64ビットの数(桁あふれは更新割込みで数える)。名前はESP32Encoderに合わせた。timerのCH1/CH2の端子だけ。実機は入力捕捉と同じ10系列(V205はEVTに例が無く欄は空だが、実機で動いた)"),
    "TIM 相補PWM・デッドタイム・ワンパルス": ("未実装", "-",
        "TIM1/TIM8の相補出力(CHxN)、デッドタイム、ブレーキ、ワンパルス、出力比較"),
    "ADC 連続・DMA・トリガ": ("一部実装", "CH32RVAdcStream(library)",
        "自走の連続変換をDMAの2面バッファで受ける(1〜6本、全系列)。タイマー・外部のトリガ、注入変換は未実装"),
    "ADC アナログウォッチドッグ": ("未実装", "-",
        ""),
    "ADC デュアル": ("未実装", "-",
        "ADC1とADC2の同時・交互変換"),
    "コンパレータ": ("未実装", "-",
        "OPAの中の比較器(CMP)。割込み・タイマーのブレーキ・起床に使える"),
    "PVD (電圧検出)": ("未実装", "-",
        "電源電圧の低下の割込み・起床"),
    "MCO (クロック出力)": ("未実装", "-",
        "クロックを端子に出す。ベンチでクロックを測るのにも使える"),
    "HSE (外部水晶)": ("一部実装", "Clock Sourceメニュー",
        "`clocksrc=hse`でCPU Clockの同じ周波数をEVTの前提の水晶(V00x 24 MHz、V103/V20x/V205/V30x/L103 8 MHz)から作る。水晶が立たなければHSIで同じ周波数。`CH32RV.clockSource()`。V003/V006/V103/V203/V205/V307/L103で実機確認(2026-10-28)。V208・M030・X315・V407・H41xは未対応(H417のUSB SSはライブラリが自分でHSEを使う)。X035にHSEは無い"),
    "PMP (メモリ保護)": ("未実装", "-",
        ""),
    "FPU": ("実装済", "float/double",
        "単精度のFPU。`-march`のfでコンパイラが使う"),
    "ICache": ("実装済", "-",
        "起動時にEVTと同じCORECFGRを書く"),
    "RAM実行": ("実装済", "CH32RV_RAMFUNC",
        "関数に付けるとRAMに置く(.highcode、起動時に.dataと一緒に写す)。その大きさ分のRAMを使う。H41xは全部RAMで動く"),
    "AWU (自動起床)": ("実装済", "CH32RV.enableTimerWakeup",
        "休止からのタイマー起床に使う(V00xはPWRの中、X035は別のブロック)。V00xのAWUはLSIで、起動後最初の休止で速さを測る"),
    "LVDS": ("未実装", "-",
        "V205。用途は未確認(ch32-device-dataに問い合わせ中)"),
    "オプションバイト": ("未実装", "-",
        "読み出し保護、NRSTの端子をGPIOにする設定など。書き込みツール(ch32rv)の範囲とも重なる"),
}



# The cells. Each family gets a mark for *this core on that family*, not just
# "EVT has an example": a row's state is the default for every family with an
# EVT example, and CELLS overrides it where a family differs (or where EVT has
# no example but the core works anyway, e.g. SysTick on V407).
#
#   ●  使える                 ◐  一部だけ(備考)
#   ○  siliconにあるが未実装  −  coreでは提供しない
#   空欄  EVTに例が無い(siliconに無いとは限らない)
MARK = {"実装済": "●", "一部実装": "◐", "未実装": "○", "対象外": "−"}

ALL = "*"
CELLS = {
    "GPIO": {ALL: "●"},
    "EXTI (外部割込み)": {ALL: "●"},
    "PFIC (割込みコントローラ)": {ALL: "●"},
    "SysTick": {ALL: "●"},
    "RCC (クロック)": {ALL: "◐"},
    "ADC": {ALL: "●"},
    "USB (FS device/host)": {"V103": "◐", "V203/V208": "◐", "V303/305/307/317": "◐", "X033/X035": "●",
                             "V205": "◐", "L103/M103": "◐", "M030": "◐", "H415/H416/H417": "◐"},
    "USB HS": {"V303/305/307/317": "◐", "V205": "◐", "V407/V467": "◐", "X305/X315": "◐", "H415/H416/H417": "◐"},
    "USB PD": {"X033/X035": "◐", "V205": "◐", "L103/M103": "◐", "M030": "◐", "X305/X315": "◐", "H415/H416/H417": "◐"},
    "USB SS": {"H415/H416/H417": "◐", "X305/X315": "◐"},
    "Ethernet": {"H415/H416/H417": "◐", "V203/V208": "◐", "V303/305/307/317": "◐", "V407/V467": "◐"},
    "FLASH (自己書き換え)": {ALL: "●"},
    "RAM実行": {ALL: "●"},
    "RNG": {"V303/305/307/317": "◐", "V407/V467": "◐", "H415/H416/H417": "◐"},
    "HSADC (高速ADC)": {"H415/H416/H417": "◐"},
    "V5F (2つ目のコア)/HSEM/IPC": {"H415/H416/H417": "○"},
    "ADC 連続・DMA・トリガ": {ALL: "◐"},
    # ch32-device-data lists a DAC block for V103, but no V103 part has one (parts.csv; asked upstream).
    "DAC": {"V103": ""},
    "FPU": {"V303/305/307/317": "●", "X305/X315": "●", "H415/H416/H417": "●"},
    "SDI print (debug出力)": {ALL: "●"},
    "RTT (debug出力)": {ALL: "●"},
    "DMDATA mailbox (debug出力)": {ALL: "●"},
    "DMSEQ console (debug出力)": {ALL: "●"},
}

# Rows that are not an EVT directory: features of the core itself.
EXTRA_ROWS = [
    ("RTT (debug出力)", "debug"),
    ("DMDATA mailbox (debug出力)", "debug"),
    ("DMSEQ console (debug出力)", "debug"),
    ("V5F (2つ目のコア)/HSEM/IPC", "basic"),
    # Only in ch32-device-data's register blocks (no EVT example).
    ("AWU (自動起床)", "basic"),
    ("オプションバイト", "basic"),
    ("LVDS", "comm"),
]


def cell(name: str, heading: str, family: str, present: dict) -> str:
    over = CELLS.get(name, {})
    if heading in over:
        return over[heading]
    if ALL in over:
        return over[ALL]
    if not present.get(name, {}).get(family):
        return ""
    state = STATUS.get(name, ("未実装", "-", ""))[0]
    return MARK[state]

def scan(evt_root: pathlib.Path) -> dict:
    """{display name: {evt family: True}} from the EVT example directories."""
    present: dict = {}
    missing = []
    for family, _series in EVT_FAMILY:
        exam = evt_root / family / "EVT" / "EXAM"
        if not exam.is_dir():
            missing.append(str(exam))
            continue
        for entry in sorted(exam.iterdir()):
            if not entry.is_dir():
                continue
            for (where, prefixes), (row, _group) in SUBFEATURES:
                if entry.name != where:
                    continue
                if any(sub.is_dir() and sub.name.startswith(prefixes) for sub in entry.iterdir()):
                    present.setdefault(row, {})[family] = True
            if entry.name in IGNORED:
                continue
            known = PERIPHERALS.get(entry.name)
            if known is None:
                print(f"note: {family}: unclassified EVT directory "
                      f"{entry.name!r}", file=sys.stderr)
                continue
            present.setdefault(known[0], {})[family] = True
    if missing:
        raise SystemExit("EVT trees not found:\n  " + "\n  ".join(missing))
    return present


def scan_blocks(tables: pathlib.Path, present: dict) -> None:
    """Add what ch32-device-data's register blocks show (index/register_blocks.csv)."""
    import csv
    families = {f for f, _s in EVT_FAMILY}
    with open(tables / "index" / "register_blocks.csv", newline="") as fh:
        for r in csv.DictReader(fh):
            row = BLOCK_ROWS.get(r["type"])
            if row and r["family"] in families:
                present.setdefault(row, {})[r["family"]] = True


def render(present: dict) -> str:
    families = [f for f, _s in EVT_FAMILY]
    order = {}
    for name, group in list(PERIPHERALS.values()) + [row for _k, row in SUBFEATURES] + EXTRA_ROWS:
        order.setdefault(group, [])
        if name not in order[group]:
            order[group].append(name)

    out = ["# ペリフェラル対応表(family別)", "",
           "**生成物**: `tools/generate/peripheral_matrix.py`が"
           "EVTの`EXAM/`ディレクトリ一覧から作ります。手で編集しないでください。",
           "",
           "```",
           "uv run --no-project python tools/generate/peripheral_matrix.py \\",
           "    --evt-root ~/dev_wch --tables ~/dev_wch/ch32-device-data --out docs/peripheral-support.ja.md",
           "```", "",
           "列はfamily(EVTの単位)ごとで、セルは**このcoreがそのfamilyで使えるか**です。"
           "siliconにあるかどうかは、EVTに**そのペリフェラルの例があるか**(例の中の"
           "サブフォルダ、たとえば`RCC/MCO`や`TIM/Encoder`も含む)と、"
           "`ch32-device-data`の`index/register_blocks.csv`に**レジスタのブロックがあるか**の"
           "どちらかで判断しています(EVTは**参照のみ**で、このrepositoryには取り込みません)。",
           "family内のseriesで違うところ(V303にUSBHSが無い、X305にSPI1が無いなど)は備考に書きます。",
           "",
           "「状態」と各セルはこのcoreの実装状況で、生成ではなく"
           "`peripheral_matrix.py`の`STATUS`と`CELLS`で維持管理しています。"
           "実機で確かめた範囲は[test-coverage.ja.md](test-coverage.ja.md)を見てください。",
           "",
           "| 記号 | 意味 |", "|---|---|",
           "| ● | 使える |",
           "| ◐ | 一部だけ使える(備考に記載) |",
           "| ○ | siliconにあるが未実装 |",
           "| − | siliconにあるがcoreでは提供しない |",
           "| 空欄 | EVTに例が無く、レジスタのブロックも無い |", "",
           "**空欄は「siliconに無い」とは限りません**。EVTに例が置かれていないだけの"
           "こともあります。実装する前に"
           "reference manualか`ch32-device-data`で裏を取ってください。", "",
           "| 状態 | 意味 |", "|---|---|",
           "| 実装済 | 公開APIと実装がある |",
           "| 一部実装 | 備考に記載した範囲だけ実装がある |",
           "| 未実装 | siliconに機能はあるがcoreの公開API/実装がない |",
           "| 対象外 | coreの公開APIとして提供しない |", ""]

    for group, title in GROUPS:
        names = order.get(group, [])
        if not names:
            continue
        out.append(f"## {title}")
        out.append("")
        out.append("| ペリフェラル | 状態 | Arduino API | " +
                   " | ".join(s for _f, s in EVT_FAMILY) + " | 備考 |")
        out.append("|---|---|---|" + "---|" * len(families) + "---|")
        for name in names:
            state, api, note = STATUS.get(name, ("未実装", "-", ""))
            cells = [cell(name, heading, f, present) for f, heading in EVT_FAMILY]
            out.append(f"| {name} | {state} | {api} | " +
                       " | ".join(cells) + f" | {note} |")
        out.append("")

    out += ["## この表の使いかた", "",
            "- 状態は repository 内の実装範囲を表します。",
            "- series列が空欄のペリフェラルは、そのfamilyの silicon に無いか"
            "EVTに例が無いかのどちらかです。実装するときは"
            "`ch32-device-data`側でも裏を取ってください。", ""]
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--evt-root", type=pathlib.Path, required=True,
                    help="directory holding the per-family EVT trees")
    ap.add_argument("--tables", type=pathlib.Path, required=True,
                    help="a ch32-device-data checkout (index/register_blocks.csv)")
    ap.add_argument("--out", type=pathlib.Path, required=True)
    args = ap.parse_args()
    present = scan(args.evt_root.expanduser())
    scan_blocks(args.tables.expanduser(), present)
    text = render(present)
    args.out.write_text(text, encoding="utf-8")
    print(f"wrote: {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
