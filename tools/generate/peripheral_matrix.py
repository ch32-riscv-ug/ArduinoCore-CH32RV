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
# maintainer's record, and it is deliberately explicit about what has been
# decided and what is only proposed.
#
#   実装済     コードがあり、少なくとも1枚の実機で確認した
#   実装中     作業中
#   予定(初回) 初回releaseに入れる (ユーザ指示のあるものは「決定」と書く)
#   要判断     方針が決まっていない
#   対象外     初回releaseでは扱わない
STATUS = {
    "GPIO": ("実装済", "pinMode/digitalWrite/digitalRead", ""),
    "EXTI (外部割込み)": ("実装済", "attachInterrupt", "X033/X035のEXTI16-23は未対応"),
    "PFIC (割込みコントローラ)": ("実装済", "-", "優先度は全てreset既定のまま"),
    "SysTick": ("実装済", "millis/micros/delay", ""),
    "RCC (クロック)": ("実装済", "F_CPU", "HSIのみ。PLL/HSEは将来"),
    "PWR (低消費電力)": ("要判断", "-", "Arduino標準APIが無い。sleep系をどう見せるか"),
    "FLASH (自己書き換え)": ("要判断", "EEPROM相当",
                            "Arduinoでは`EEPROM`が定番。**page消去単位と書き込み粒度のデータが無い**"
                            "(R-20のD-3相当)。products.csvにあるのはflash容量だけ"),
    "USART": ("実装済", "Serial", ""),
    "I2C": ("実装済", "Wire", "master専用。X035実機で配線なしの自己検査11項目pass。"
            "**slave(onReceive/onRequest)は未実装**、実デバイス相手の確認はこれから"),
    "SPI": ("実装済", "SPI", "controller専用。X035実機で配線なしの自己検査9項目pass。"
            "**peripheral(slave)は未実装**、実デバイス相手の確認はこれから"),
    "ADC": ("実装済", "analogRead", "分解能は実機未確認"),
    "DAC": ("実装済", "analogWrite(CH32_DACn_PIN)",
            "V303/V305/V307/V317/V407/V467のみ。padはdevice-data由来。**実機未確認**"),
    "TIM (PWM/tone/入力捕捉)": ("実装済", "analogWrite/tone",
                               "`tone()`はtimer割込みでpinをtoggle。使うtimerはvariantが選ぶ"
                               "(`CH32_TONE_TIMER`)。V003/X035/M030は空きが無く**PWMと共有**"),
    "LPTIM": ("対象外", "-", "L103のみ"),
    "DMA": ("対象外", "-", "Arduino APIに露出しない。内部最適化として将来"),
    "IWDG": ("要判断", "-", "Arduino標準APIが無い"),
    "WWDG": ("要判断", "-", "同上"),
    "RTC": ("要判断", "-", "libraryとして出す例が多い"),
    "BKP (バックアップレジスタ)": ("対象外", "-", ""),
    "CRC": ("対象外", "-", ""),
    "OPA/コンパレータ": ("対象外", "-", "CH32固有"),
    "TouchKey": ("対象外", "-", "CH32固有"),
    "PIOC (プログラマブルIO)": ("対象外", "-", "X035/V205固有"),
    "USB (FS device/host)": ("予定(初回)", "TinyUSB",
                             "**TinyUSB採用が決定**(ADR-0012)。上流の対応はV103/V20x/V30xのみで、"
                             "X033/X035はPR未マージ、L103/M030/V205は未対応。"
                             "**X035以外はPLLが先**"),
    "USB HS": ("予定(初回)", "TinyUSB",
               "V30x配置は上流済み。V205/V407/X3x5は別配置で未対応(3 seriesで共通)"),
    "USB SS": ("対象外", "-", "X315のみ。当面扱わない"),
    "USB PD": ("予定(初回・決定)", "未定", "**必ず載せる**(ユーザ指示)"),
    "CAN": ("対象外", "-", "libraryとして将来"),
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
    "SDI print (debug出力)": ("実装済", "SerialSDI",
                              "送信のみ。spikeで受信まで実機確認済み(class実装後の実機確認は未)。"
                              "**probe側の対応chipはV003/V00x/V103/V20x/V30x/X035/L103のみ**"),
    "FreeRTOS": ("対象外", "-", "初回release対象外(ユーザ指示)"),
    "RT-Thread": ("対象外", "-", "同上"),
    "HarmonyOS": ("対象外", "-", "同上"),
    "TencentOS": ("対象外", "-", "同上"),
}


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
    for name, group in PERIPHERALS.values():
        order.setdefault(group, [])
        if name not in order[group]:
            order[group].append(name)

    out = ["# ペリフェラル対応表(series別)", "",
           "**生成物**: `tools/generate/peripheral_matrix.py`が"
           "EVTの`EXAM/`ディレクトリ一覧から作ります。手で編集しないでください。",
           "",
           "```",
           "uv run --no-project python tools/generate/peripheral_matrix.py \\",
           "    --evt-root ~/dev_wch --out docs/peripheral-support.ja.md",
           "```", "",
           "「有無」の列はEVTに**そのペリフェラルの例があるか**です。"
           "WCHはペリフェラルごとに例を1つ置くので、これが事実上のペリフェラル一覧になります"
           "(EVTは**参照のみ**で、このrepositoryには取り込みません)。",
           "",
           "「状態」はこのcoreの実装状況で、生成ではなく維持管理している列です。",
           "", "凡例: ○=EVTに例がある / 空欄=無い", "",
           "**空欄は「siliconに無い」とは限りません**。EVTに例が置かれていないだけの"
           "こともあります(V407のSysTick、V103のINTなど)。実装する前に"
           "reference manualか`ch32-device-data`で裏を取ってください。", "",
           "| 状態 | 意味 |", "|---|---|",
           "| 実装済 | コードがあり、少なくとも1枚の実機で確認した |",
           "| 実装中 | 作業中 |",
           "| 一部 | 一部だけ動く(備考を参照) |",
           "| 予定(初回) | 初回releaseに入れる |",
           "| 要判断 | 方針が未決。**勝手に決めない** |",
           "| 対象外 | 初回releaseでは扱わない |", ""]

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
            state, api, note = STATUS.get(name, ("要判断", "-", ""))
            cells = ["○" if present.get(name, {}).get(f) else ""
                     for f in families]
            out.append(f"| {name} | {state} | {api} | " +
                       " | ".join(cells) + f" | {note} |")
        out.append("")

    out += ["## この表の使いかた", "",
            "- 「基本ペリフェラル」で`対象外`/`要判断`のものが、"
            "初回releaseの範囲を決める議論の対象です。",
            "- USB PDは**載せると決まっています**(ユーザ指示)。"
            "USB host/deviceは範囲が未決なので`要判断`のままにしてあります。",
            "- RTOSは一覧には載せますが初回release対象外です(ユーザ指示)。",
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
