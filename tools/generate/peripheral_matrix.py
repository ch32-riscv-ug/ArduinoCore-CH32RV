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
      --evt-root ~/dev_wch --out docs/peripheral-support.ja.md

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
    "TIM":       ("TIM (PWM/tone/入力捕捉)", "basic"),
    "LPTIM":     ("LPTIM", "basic"),
    "DMA":       ("DMA", "basic"),
    "IWDG":      ("IWDG", "basic"),
    "WWDG":      ("WWDG", "basic"),
    "RTC":       ("RTC", "basic"),
    "BKP":       ("BKP (バックアップレジスタ)", "basic"),
    "CRC":       ("CRC", "basic"),
    "OPA":       ("OPA/コンパレータ", "analog"),
    "TOUCHKEY":  ("TouchKey", "analog"),
    "TKEY":      ("TouchKey", "analog"),
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
    "FSMC":      ("FSMC", "comm"),
    "QSPI":      ("QSPI", "comm"),
    "PSRAM":     ("PSRAM", "comm"),
    "I3C":       ("I3C", "comm"),
    "DVP":       ("DVP (カメラ)", "comm"),
    "LTDC":      ("LTDC (LCD)", "comm"),
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
                       "HSI(とHSI由来PLL)で系列ごとに1つの固定周波数。V103 72 MHz、V20x/V30x 96 MHz、"
                       "X033/X035 48 MHz、V00x/M007 24 MHz、V407/V467・X305/X315 20 MHz、"
                       "L103/M103・V205・M030 8 MHz(PLL未使用)。HSEは未実装(USB HSのみHSEを使う)"),
    "PWR (低消費電力)": ("未実装", "-", "sleep APIは提供していない"),
    "FLASH (自己書き換え)": ("未実装", "-",
                            "自己書き換えとEEPROM emulationのAPIは提供していない"),
    "USART": ("実装済", "Serial",
              "全インスタンス(V205/V30x/V407/V467のUSART6〜10も、Serial9まで)。V205・X305/X315は"
              "端子ごとのAF番号を書く(ビルドのみ確認)。X033/X035は送信をポーリングで行う(割込み送信の不具合の回避)"),
    "I2C": ("実装済", "Wire",
            "controllerはpolling、peripheral(slave)は割込み駆動。V205・X305/X315は端子ごとのAF番号を書く(ビルドのみ確認)"),
    "SPI": ("一部実装", "SPI",
            "controllerのみ。peripheral(slave)は未実装。V205・X305/X315は端子ごとのAF番号を書く(ビルドのみ確認)。"
            "M103はSPI1のみ"),
    "ADC": ("実装済", "analogRead",
            "X305/X315はADC1〜4がpadを分け合うため`CH32RV_PIN_TO_ADC_INSTANCE`で切替。"
            "内部チャネルは`analogReadVref()`・`analogReadTemp()`・`CH32RV.getVddMillivolts()`"
            "(M030はdevice-dataに無く未対応、温度センサはV103/V20x/V205/V30x/V4x7/L103のみ、"
            "校正なしの典型値)。L103/M103はEVTと同じVDDA/2のオフセット補正をかける。"
            "基準電圧は選べない(`analogReference()`は何もしない)。V003は10 bit"),
    "DAC": ("実装済", "analogWrite(CH32RV_DACn_PIN)",
            "V303/V305/V307/V317/V407/V467のみ。padはdevice-data由来"),
    "TIM (PWM/tone/入力捕捉)": ("実装済", "analogWrite/tone",
                               "`tone()`はtimer割込みでpinをtoggle。使うtimerはvariantが選ぶ"
                               "(`CH32RV_TONE_TIMER`)。V00x/X035/M030は空きが無く**PWMと共有**。"
                               "PWMはTIM1〜5の既定ピン(tone/Servoが使うtimerは除く。remap先とCHxNは未対応)、"
                               "V205・X305/X315は端子ごとのAF番号で出す(ビルドのみ確認)。timer管理はTIM1〜10"
                               "(V30xのTIM8〜10、V4x7のTIM8)"),
    "LPTIM": ("対象外", "-", "L103のみ"),
    "DMA": ("対象外", "-", "Arduino APIには露出しない"),
    "IWDG": ("実装済", "`CH32RV.wdtEnable/wdtFeed`",
             "X033/X035はLSIが無くHSI/1024で動く(X035・L103・V103・V307で実機確認)。M030はIWDGが無い"),
    "WWDG": ("未実装", "-", ""),
    "RTC": ("未実装", "-", ""),
    "BKP (バックアップレジスタ)": ("対象外", "-", ""),
    "CRC": ("対象外", "-", ""),
    "OPA/コンパレータ": ("対象外", "-", "CH32固有"),
    "TouchKey": ("対象外", "-", "CH32固有"),
    "PIOC (プログラマブルIO)": ("対象外", "-", "X035/V205固有"),
    "USB (FS device/host)": ("一部実装", "`CH32RVUsbDevice` / `CH32RVUsbHost`",
                             "採用stackはTinyUSB。deviceはX035で実機確認(ホストはPCとESP32-S3)。V20xはUSBFSと"
                             "USBD(fsdev)を実行時に選ぶ。V103/V20x/V30xはビルドのみ。hostはX035(R8T6/C8T6/G8U6/G8R6、"
                             "F8U6などはシリコンに無い)・V20x・V30xのUSBFS(HCDは自前)。X035 C8T6でESP32-S3相手に実機確認、V20x/V30xはビルドのみ。"
                             "L103/M103・V205・M030はクロック方針の決定待ちで未実装。"
                             "工場ISPへの切り替えはX033/X035のみ(docs/usb.ja.md)"),
    "USB HS": ("一部実装", "`CH32RVUsbDevice`(`UsbPort::HighSpeed`)",
               "V305/V307/V317(第1世代USBHS、HSEが要る)。ビルドのみ。V303はUSBHSが無い。"
               "V205・V407/V467・X305/X315の第2世代USBHSは新しいドライバが要り未実装"),
    "USB SS": ("対象外", "-", "X315のみ。当面扱わない"),
    "USB PD": ("一部実装", "`CH32RVUsbPd`(library: `USBPD`、`USBPDSource`)",
               "sink(X035で実機確認)とsource(ビルドのみ)をX033/X035で実装。"
               "他のUSBPD搭載seriesは`begin()`がfalseを返す"),
    "CAN": ("対象外", "-", ""),
    "Ethernet": ("対象外", "-", ""),
    "I2S": ("対象外", "-", ""),
    "SDIO": ("対象外", "-", "`SD`libraryの下地にはなる"),
    "FSMC": ("対象外", "-", ""),
    "QSPI": ("対象外", "-", ""),
    "PSRAM": ("対象外", "-", ""),
    "I3C": ("対象外", "-", ""),
    "DVP (カメラ)": ("対象外", "-", ""),
    "LTDC (LCD)": ("対象外", "-", ""),
    "ARGB (LEDドライバ)": ("対象外", "-", ""),
    "RNG": ("対象外", "-", "`random()`はsoftware実装で足りている"),
    "BLE": ("対象外", "-", "V208のみ。専用stackが要る"),
    "SDI print (debug出力)": ("実装済", "SerialSDI(library)", "送信のみ"),
    "RTT (debug出力)": ("実装済", "SerialRTT(library)", "双方向。RAM ring bufferを使用"),
    "DMDATA mailbox (debug出力)": ("実装済", "SerialDMDATA(library)",
                                      "双方向。SerialSDIと同時使用不可"),
    "DMSEQ console (debug出力)": ("実装済", "SerialDMSeq(library)",
                                    "双方向。通し番号とCRC-8を使用"),
    "FreeRTOS": ("対象外", "-", "coreはbare metal。ADR-0006"),
    "RT-Thread": ("対象外", "-", "coreはbare metal。ADR-0006"),
    "HarmonyOS": ("対象外", "-", "coreはbare metal。ADR-0006"),
    "TencentOS": ("対象外", "-", "coreはbare metal。ADR-0006"),
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
    "USB (FS device/host)": {"V103": "●", "V203/V208": "●", "V303/305/307/317": "●",
                             "X033/X035": "●", "V205": "○", "L103/M103": "○", "M030": "○"},
    "USB HS": {"V303/305/307/317": "●", "V205": "○", "V407/V467": "○", "X305/X315": "○"},
    "USB PD": {"X033/X035": "●", "V205": "○", "L103/M103": "○", "M030": "○", "X305/X315": "○"},
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
            if not entry.is_dir() or entry.name in IGNORED:
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


def render(present: dict) -> str:
    families = [f for f, _s in EVT_FAMILY]
    order = {}
    for name, group in list(PERIPHERALS.values()) + EXTRA_ROWS:
        order.setdefault(group, [])
        if name not in order[group]:
            order[group].append(name)

    out = ["# ペリフェラル対応表(family別)", "",
           "**生成物**: `tools/generate/peripheral_matrix.py`が"
           "EVTの`EXAM/`ディレクトリ一覧から作ります。手で編集しないでください。",
           "",
           "```",
           "uv run --no-project python tools/generate/peripheral_matrix.py \\",
           "    --evt-root ~/dev_wch --out docs/peripheral-support.ja.md",
           "```", "",
           "列はfamily(EVTの単位)ごとで、セルは**このcoreがそのfamilyで使えるか**です。"
           "siliconにあるかどうかは、EVTに**そのペリフェラルの例があるか**で判断しています"
           "(WCHはペリフェラルごとに例を1つ置くので、これが事実上のペリフェラル一覧になります。"
           "EVTは**参照のみ**で、このrepositoryには取り込みません)。",
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
           "| 空欄 | EVTに例が無い |", "",
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
    ap.add_argument("--out", type=pathlib.Path, required=True)
    args = ap.parse_args()
    text = render(scan(args.evt_root.expanduser()))
    args.out.write_text(text, encoding="utf-8")
    print(f"wrote: {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
