# Changelog / 変更履歴

## Unreleased

## 0.0.2
- (EN) The bundled ch32rv is now 0.12.0: gdb joins the same broker as the monitor, so a debugger and a debug-module monitor work at once, on WCH-Link and OEP probes alike; gdb no longer leaves an ebreak in flash, connects to RV32E (CH32V00x) ELFs, and no longer stops an OEP probe's console.
- (JA) 同梱の ch32rv を 0.12.0 にした: gdb が monitor と同じブローカーにつながるので、debugger と debug module の monitor を同時に使える（WCH-Link でも OEP の probe でも）。gdb が flash に ebreak を焼き残さない、RV32E（CH32V00x）の ELF でつながる、OEP の probe の console を止めない、の 3 つも直っている。
- (EN) The GitHub Pages site gets a top page next to the Board Manager index: the URL with a copy button, how to install and upload, and the versions and boards read from the index.
- (JA) GitHub Pages に、Board Manager の index の隣にトップページを置いた: URL（コピーのボタン付き）、install と書き込みの手順、index から読む版とボードの一覧。

## 0.0.1
- (EN) First Board Manager release, a beta: pre-release, with breaking changes still expected. The platform installs from the package index with its RISC-V toolchain (xPack GCC 14.3.0-1) and one bundled uploader, ch32rv 0.11.0.
- (JA) 最初の Board Manager のリリース（β）。本番前で、破壊的変更はまだ入る。package index から RISC-V の toolchain（xPack GCC 14.3.0-1）と、同梱の書き込みツール ch32rv 0.11.0 ごと入る。
- (EN) Upload by picking a port: a WCH-Link's serial port or `wchlink://<serial>`, an OEP probe's `oep://<probe>/<slot>` or its plain serial port, or the UIAPduino bootloader's `hid://`. No programmer menu is needed; `--programmer wch-link` still works from the command line.
- (JA) port を選ぶだけで書ける: WCH-Link の serial port か `wchlink://<serial>`、OEP の probe の `oep://<probe>/<slot>` かその素の serial port、UIAPduino のブートローダーの `hid://`。programmer の選択は要らない（コマンドラインの `--programmer wch-link` もそのまま使える）。
- (EN) ch32rv's discovery lists every WCH-Link and every OEP probe slot, and every monitor is ch32rv's: the `source` setting reads the target's UART, SDI, DMDATA, DMSeq, RTT or an OEP probe's fixture UART, and the board's `chip` keeps a debug-module source off a board that is not the one attached.
- (JA) ch32rv の discovery が WCH-Link と OEP の probe のスロットを並べ、monitor はすべて ch32rv。`source` の設定で target の UART / SDI / DMDATA / DMSeq / RTT / OEP の probe の fixture UART を選び、板の `chip` で、つながっていない板に debug module の source を開かない。
- (EN) The core puts the clock back within a millisecond when a WCH-LinkE attach rewrites RCC (V203, L103, V006, V307), so the UART baud rate, `millis()` and timers stay right while a debugger or a debug-module monitor is attached.
- (JA) WCH-LinkE の attach が RCC を書き換えても（V203 / L103 / V006 / V307）、core が 1 ms 以内にクロックを戻す。debugger や debug module の monitor がつながっている間も、UART の baud・`millis()`・timer が狂わない。
- (EN) SerialDMSeq recovers at once when a probe attach leaves a foreign word in DATA0, so reopening a monitor on V006 / X035 streams immediately instead of stalling for seconds.
- (JA) probe の attach が DATA0 に別の語を残しても SerialDMSeq がすぐ立て直すので、V006 / X035 で monitor を開き直しても数秒止まらずに流れる。
